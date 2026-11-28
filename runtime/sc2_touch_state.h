// SPDX-License-Identifier: GPL-3.0-or-later
// Pure state machine for on-screen touch controls (used by sc2_touch.cpp and unit tests).
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <vector>

namespace sc2_touch {

inline constexpr int kInvalidButton = -1;
inline constexpr int kButtonDpadUp = 11;
inline constexpr int kButtonDpadDown = 12;
inline constexpr int kButtonDpadLeft = 13;
inline constexpr int kButtonDpadRight = 14;
inline constexpr int kButtonCount = 26;

inline constexpr float kStickRadiusPct = 13.0f;
inline constexpr uint64_t kIdleMs = 5000;
inline constexpr unsigned kMinHoldFrames = 4;
// Cap deferred tap-release at ~67 ms so low-FPS throttling (e.g. 17 fps = 58 ms/frame) does not keep Guard held for 230+ ms.
inline constexpr uint64_t kMinHoldMaxMs = 67;

struct TouchInput {
    enum class Kind {
        Down,
        Motion,
        Up,
        Cancel,
        Reset,
    };
    Kind kind;
    uint64_t finger = 0;
    float x = 0.0f; // 0 to 1 across the window
    float y = 0.0f;
};

struct Control {
    const char *label;
    int button;
    float x, y, radius; // in the window's pixels
    uint32_t colour;
    int also = kInvalidButton; // a second button pressed with the first (A+G)
    unsigned pressed_frame = 0;
    uint64_t pressed_ms = 0;
    bool release_due = false;
};

constexpr uint32_t PackRgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    return static_cast<uint32_t>(r) | (static_cast<uint32_t>(g) << 8) |
           (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(a) << 24);
}

class TouchState {
public:
    int width = 0;
    int height = 0;
    float stick_x = 0.0f;
    float stick_y = 0.0f;
    float stick_radius = 0.0f;
    float knob_x = 0.0f;
    float knob_y = 0.0f;
    bool stick_held = false;
    uint8_t direction = 0; // d-pad bits: 1 up, 2 down, 4 left, 8 right
    unsigned frame = 0;
    std::vector<Control> controls;
    std::map<uint64_t, int> fingers; // fingerID -> -1 (stick) or control index
    std::array<bool, kButtonCount> applied_buttons{};

    template <typename Callback>
    void sync_buttons(Callback &&on_button)
    {
        std::array<bool, kButtonCount> desired{};
        for (const auto &entry : fingers) {
            const int idx = entry.second;
            if (idx >= 0 && static_cast<size_t>(idx) < controls.size()) {
                const Control &c = controls[static_cast<size_t>(idx)];
                if (c.button >= 0 && c.button < kButtonCount) desired[static_cast<size_t>(c.button)] = true;
                if (c.also >= 0 && c.also < kButtonCount) desired[static_cast<size_t>(c.also)] = true;
            }
        }
        for (const Control &c : controls) {
            if (!c.release_due) continue;
            if (c.button >= 0 && c.button < kButtonCount) desired[static_cast<size_t>(c.button)] = true;
            if (c.also >= 0 && c.also < kButtonCount) desired[static_cast<size_t>(c.also)] = true;
        }
        if (direction & 1u) desired[kButtonDpadUp] = true;
        if (direction & 2u) desired[kButtonDpadDown] = true;
        if (direction & 4u) desired[kButtonDpadLeft] = true;
        if (direction & 8u) desired[kButtonDpadRight] = true;

        for (int b = 0; b < kButtonCount; ++b) {
            const size_t idx = static_cast<size_t>(b);
            if (desired[idx] != applied_buttons[idx]) {
                applied_buttons[idx] = desired[idx];
                on_button(b, desired[idx]);
            }
        }
    }

    template <typename Callback>
    void reset(Callback &&on_button)
    {
        fingers.clear();
        for (Control &c : controls) c.release_due = false;
        stick_held = false;
        direction = 0;
        knob_x = stick_x;
        knob_y = stick_y;
        sync_buttons(on_button);
    }

    template <typename Callback>
    void layout(int w, int h, int btn_south, int btn_east, int btn_west, int btn_north, int btn_start, int btn_back,
        Callback &&on_button)
    {
        if (w <= 0 || h <= 0) return;
        if (w == width && h == height) return;
        reset(on_button);
        width = w;
        height = h;
        const float unit = static_cast<float>(h) / 100.0f;
        const float side = std::max(0.0f, (w - h * 16.0f / 9.0f) * 0.5f);
        const float margin = 4.0f * unit;
        stick_radius = kStickRadiusPct * unit;
        stick_x = std::max(margin + stick_radius, side * 0.5f);
        stick_y = h - margin - stick_radius - 6.0f * unit;
        knob_x = stick_x;
        knob_y = stick_y;
        const float r = 6.5f * unit, gap = 10.0f * unit;
        const float cx = w - std::max(margin + r + gap, side * 0.5f);
        const float cy = h - margin - r - gap - 6.0f * unit;
        controls = {
            {"B", btn_north, cx, cy - gap, r, PackRgba(240, 190, 60, 255)},
            {"A", btn_west, cx - gap, cy, r, PackRgba(80, 160, 250, 255)},
            {"K", btn_east, cx + gap, cy, r, PackRgba(240, 80, 80, 255)},
            {"G", btn_south, cx, cy + gap, r, PackRgba(90, 210, 120, 255)},
            {"A+G", btn_west, cx - gap * 1.45f, cy + gap * 1.45f, r * 0.8f, PackRgba(170, 140, 240, 255), btn_south},
            {"Start", btn_start, w - margin - 6.0f * unit, margin + 4.0f * unit, 5.0f * unit, PackRgba(200, 200, 210, 255)},
            {"Menu", btn_back, margin + 6.0f * unit, margin + 4.0f * unit, 5.0f * unit, PackRgba(200, 200, 210, 255)},
        };
    }

    int hit(float x, float y) const
    {
        int best = -2;
        float best_dist_sq = 0.0f;
        for (size_t i = 0; i < controls.size(); ++i) {
            const float dx = x - controls[i].x;
            const float dy = y - controls[i].y;
            const float r = controls[i].radius * 1.3f;
            const float dist_sq = dx * dx + dy * dy;
            if (dist_sq <= r * r && (best < 0 || dist_sq < best_dist_sq)) {
                best = static_cast<int>(i);
                best_dist_sq = dist_sq;
            }
        }
        if (best >= 0) return best;
        if (x < width * 0.45f && y > height * 0.2f) return -1;
        return -2;
    }

    bool any_finger_on(int control) const
    {
        for (const auto &entry : fingers) {
            if (entry.second == control) return true;
        }
        return false;
    }

    bool has_active_or_pending() const
    {
        if (!fingers.empty()) return true;
        for (const Control &c : controls) {
            if (c.release_due) return true;
        }
        return false;
    }

    bool is_control_held(size_t index) const
    {
        return any_finger_on(static_cast<int>(index)) || controls[index].release_due;
    }

    template <typename Callback>
    void apply(const TouchInput &touch, uint64_t now_ms, Callback &&on_button)
    {
        if (touch.kind == TouchInput::Kind::Reset) {
            reset(on_button);
            return;
        }
        if (width <= 0 || height <= 0) return;
        const float x = touch.x * static_cast<float>(width);
        const float y = touch.y * static_cast<float>(height);

        if (touch.kind == TouchInput::Kind::Down) {
            // Release any stale assignment for the same fingerID before re-evaluating hit().
            if (auto stale = fingers.find(touch.finger); stale != fingers.end()) {
                const int old_control = stale->second;
                fingers.erase(stale);
                release_control(old_control, now_ms, true);
            }
            const int control = hit(x, y);
            if (control == -2) {
                sync_buttons(on_button);
                return;
            }
            fingers[touch.finger] = control;
            if (control == -1) {
                stick_held = true;
                if (std::hypot(x - stick_x, y - stick_y) > stick_radius) {
                    stick_x = std::clamp(x, stick_radius, width * 0.45f);
                    stick_y = std::clamp(y, height * 0.2f + stick_radius * 0.5f, height - stick_radius * 0.5f);
                }
                stick_to(x, y);
            } else {
                Control &c = controls[static_cast<size_t>(control)];
                c.pressed_frame = frame;
                c.pressed_ms = now_ms;
                c.release_due = false;
            }
            sync_buttons(on_button);
            return;
        }

        auto found = fingers.find(touch.finger);
        if (found == fingers.end()) return;
        const int control = found->second;
        if (touch.kind == TouchInput::Kind::Motion) {
            if (control == -1) {
                stick_to(x, y);
                sync_buttons(on_button);
            }
            return;
        }

        fingers.erase(found);
        release_control(control, now_ms, touch.kind == TouchInput::Kind::Cancel);
        sync_buttons(on_button);
    }

    template <typename Callback>
    void finish_frame(uint64_t now_ms, Callback &&on_button)
    {
        for (size_t i = 0; i < controls.size(); ++i) {
            Control &c = controls[i];
            if (!c.release_due) continue;
            if (any_finger_on(static_cast<int>(i))) {
                c.release_due = false;
                continue;
            }
            if (frame > c.pressed_frame &&
                (frame - c.pressed_frame >= kMinHoldFrames || now_ms - c.pressed_ms >= kMinHoldMaxMs)) {
                c.release_due = false;
            }
        }
        sync_buttons(on_button);
    }

private:
    void stick_to(float x, float y)
    {
        float dx = x - stick_x, dy = y - stick_y;
        const float length = std::sqrt(dx * dx + dy * dy);
        if (length > stick_radius && length > 0.0f) {
            dx *= stick_radius / length;
            dy *= stick_radius / length;
        }
        knob_x = stick_x + dx;
        knob_y = stick_y + dy;
        uint8_t now = 0;
        if (length > stick_radius * 0.33f) {
            const float angle = std::atan2(-dy, dx);
            const float eighth = 3.14159265f / 8.0f;
            if (angle > eighth && angle < 7 * eighth) now |= 1;
            if (angle < -eighth && angle > -7 * eighth) now |= 2;
            if (angle > 5 * eighth || angle < -5 * eighth) now |= 4;
            if (angle < 3 * eighth && angle > -3 * eighth) now |= 8;
        }
        direction = now;
    }

    void release_control(int control, uint64_t now_ms, bool immediate)
    {
        if (control == -1) {
            if (!any_finger_on(-1)) {
                stick_held = false;
                stick_to(stick_x, stick_y);
            }
            return;
        }
        if (control < 0 || static_cast<size_t>(control) >= controls.size()) return;
        if (any_finger_on(control)) return;
        Control &c = controls[static_cast<size_t>(control)];
        if (immediate) {
            c.release_due = false;
            return;
        }
        if (frame == c.pressed_frame ||
            (frame - c.pressed_frame < kMinHoldFrames && now_ms - c.pressed_ms < kMinHoldMaxMs)) {
            c.release_due = true;
        } else {
            c.release_due = false;
        }
    }
};

} // namespace sc2_touch
