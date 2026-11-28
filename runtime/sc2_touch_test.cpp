// SPDX-License-Identifier: GPL-3.0-or-later
// Standalone host unit/regression tests for sc2_touch::TouchState (virtual Defend/G and touch input state machine).
// Build on a host with a C++17 compiler: c++ -std=c++17 -O2 -Iruntime runtime/sc2_touch_test.cpp -o sc2_touch_test
#include "sc2_touch_state.h"

#include <array>
#include <cassert>
#include <cstdio>

namespace {

enum TestButton {
    kBtnSouth = 0, // Guard (G) on default Xbox pad mapping
    kBtnEast = 1,  // Kick (K)
    kBtnWest = 2,  // Horizontal (A)
    kBtnNorth = 3, // Vertical (B)
    kBtnBack = 4,
    kBtnStart = 6,
};

struct Harness {
    sc2_touch::TouchState state;
    std::array<bool, sc2_touch::kButtonCount> pad{};
    uint64_t now_ms = 1000;

    Harness()
    {
        state.frame = 1;
        state.layout(1707, 960, kBtnSouth, kBtnEast, kBtnWest, kBtnNorth, kBtnStart, kBtnBack,
            [this](int b, bool down) { pad[static_cast<size_t>(b)] = down; });
    }

    float norm_x(size_t idx) const { return state.controls[idx].x / static_cast<float>(state.width); }
    float norm_y(size_t idx) const { return state.controls[idx].y / static_cast<float>(state.height); }

    void step(std::initializer_list<sc2_touch::TouchInput> events, uint64_t dt_ms = 16)
    {
        now_ms += dt_ms;
        ++state.frame;
        for (const sc2_touch::TouchInput &ev : events) {
            state.apply(ev, now_ms, [this](int b, bool down) { pad[static_cast<size_t>(b)] = down; });
        }
        state.finish_frame(now_ms, [this](int b, bool down) { pad[static_cast<size_t>(b)] = down; });
    }

    void advance_frames(int count, uint64_t dt_ms = 16)
    {
        for (int i = 0; i < count; ++i) step({}, dt_ms);
    }
};

// Control index 3 is "G" (mapped to kBtnSouth), 4 is "A+G" (mapped to kBtnWest + kBtnSouth).
constexpr size_t kIdxB = 0;
constexpr size_t kIdxA = 1;
constexpr size_t kIdxK = 2;
constexpr size_t kIdxG = 3;
constexpr size_t kIdxAG = 4;

using Kind = sc2_touch::TouchInput::Kind;

void test_normal_hold_and_release()
{
    Harness h;
    const float gx = h.norm_x(kIdxG), gy = h.norm_y(kIdxG);
    h.step({{Kind::Down, 1, gx, gy}});
    assert(h.pad[kBtnSouth]);
    assert(h.state.has_active_or_pending());

    h.advance_frames(10, 16);
    assert(h.pad[kBtnSouth]);

    h.step({{Kind::Up, 1, gx, gy}});
    assert(!h.pad[kBtnSouth]);
    assert(!h.state.has_active_or_pending());
}

void test_rapid_tap_repress_must_not_stick_defend()
{
    // Previously, Down -> Up (<4 frames -> release_due=true) -> Down (before 4 frames) overwrote
    // release_due without decrementing held_count[], permanently leaking held_count[kBtnSouth] >= 1.
    Harness h;
    const float gx = h.norm_x(kIdxG), gy = h.norm_y(kIdxG);

    h.step({{Kind::Down, 1, gx, gy}}, 16);
    assert(h.pad[kBtnSouth]);

    // Release 1 frame later -> enters deferred release_due state
    h.step({{Kind::Up, 1, gx, gy}}, 16);
    assert(h.pad[kBtnSouth]);
    assert(h.state.has_active_or_pending());

    // Re-press immediately on the next frame while release_due is still active
    h.step({{Kind::Down, 1, gx, gy}}, 16);
    assert(h.pad[kBtnSouth]);

    // Hold well past kMinHoldFrames and release cleanly
    h.advance_frames(6, 16);
    h.step({{Kind::Up, 1, gx, gy}}, 16);
    assert(!h.pad[kBtnSouth]);
    assert(!h.state.has_active_or_pending());

    // Also test repeated rapid taps in a row
    for (int i = 0; i < 10; ++i) {
        h.step({{Kind::Down, 2, gx, gy}}, 10);
        h.step({{Kind::Up, 2, gx, gy}}, 10);
    }
    h.advance_frames(6, 16);
    assert(!h.pad[kBtnSouth]);
    assert(!h.state.has_active_or_pending());
}

void test_single_frame_same_frame_down_up_and_low_fps_cap()
{
    Harness h;
    const float gx = h.norm_x(kIdxG), gy = h.norm_y(kIdxG);

    // Down and Up arrive within the same frame: deferred hold stays active at least briefly
    h.step({{Kind::Down, 1, gx, gy}, {Kind::Up, 1, gx, gy}}, 16);
    assert(h.pad[kBtnSouth]);
    assert(h.state.has_active_or_pending());

    // Simulate 17 fps (58 ms/frame): 2 frames = 116 ms >= kMinHoldMaxMs (67 ms), so it must NOT linger for 4 frames (232 ms)
    h.step({}, 58);
    assert(h.pad[kBtnSouth]); // 58 ms < 67 ms (frame diff = 1)
    h.step({}, 58);
    assert(!h.pad[kBtnSouth]); // 116 ms >= 67 ms -> released!
    assert(!h.state.has_active_or_pending());
}

void test_two_fingers_on_defend()
{
    Harness h;
    const float gx = h.norm_x(kIdxG), gy = h.norm_y(kIdxG);

    h.step({{Kind::Down, 1, gx, gy}, {Kind::Down, 2, gx, gy}});
    assert(h.pad[kBtnSouth]);

    // Release first finger, then second finger 1 frame later while release_due could conflict
    h.step({{Kind::Up, 1, gx, gy}});
    assert(h.pad[kBtnSouth]);

    h.step({{Kind::Up, 2, gx, gy}});
    h.advance_frames(6);
    assert(!h.pad[kBtnSouth]);
    assert(!h.state.has_active_or_pending());
}

void test_defend_plus_throw_overlap_and_independent_release()
{
    Harness h;
    const float gx = h.norm_x(kIdxG), gy = h.norm_y(kIdxG);
    const float agx = h.norm_x(kIdxAG), agy = h.norm_y(kIdxAG);

    // Holding G while tapping A+G presses kBtnSouth + kBtnWest
    h.step({{Kind::Down, 1, gx, gy}, {Kind::Down, 2, agx, agy}});
    assert(h.pad[kBtnSouth]);
    assert(h.pad[kBtnWest]);

    h.advance_frames(5);
    // Lift G: A+G still needs both West and South
    h.step({{Kind::Up, 1, gx, gy}});
    assert(h.pad[kBtnSouth]);
    assert(h.pad[kBtnWest]);

    // Lift A+G: both release
    h.step({{Kind::Up, 2, agx, agy}});
    assert(!h.pad[kBtnSouth]);
    assert(!h.pad[kBtnWest]);
}

void test_action_cancel_releases_immediately()
{
    Harness h;
    const float gx = h.norm_x(kIdxG), gy = h.norm_y(kIdxG);

    h.step({{Kind::Down, 1, gx, gy}});
    assert(h.pad[kBtnSouth]);

    // Even 1 frame later, Cancel must release immediately (no release_due delay)
    h.step({{Kind::Cancel, 1, gx, gy}});
    assert(!h.pad[kBtnSouth]);
    assert(!h.state.has_active_or_pending());
}

void test_focus_loss_and_menu_reset_release_defend_and_stick()
{
    Harness h;
    const float gx = h.norm_x(kIdxG), gy = h.norm_y(kIdxG);
    const float sx = 0.05f, sy = 0.7f; // left stick area

    h.step({{Kind::Down, 1, gx, gy}, {Kind::Down, 2, sx, sy}, {Kind::Motion, 2, 0.02f, 0.7f}});
    assert(h.pad[kBtnSouth]);
    assert(h.state.stick_held);

    // Window focus loss / notification panel / app background pushes Kind::Reset
    h.step({{Kind::Reset, 0, 0.0f, 0.0f}});
    assert(!h.pad[kBtnSouth]);
    assert(!h.state.stick_held);
    assert(h.state.direction == 0);
    assert(!h.state.has_active_or_pending());
    for (int b = 0; b < sc2_touch::kButtonCount; ++b) assert(!h.pad[static_cast<size_t>(b)]);
}

void test_stale_finger_id_reuse_does_not_leak_previous_button()
{
    Harness h;
    const float gx = h.norm_x(kIdxG), gy = h.norm_y(kIdxG);

    h.step({{Kind::Down, 7, gx, gy}});
    assert(h.pad[kBtnSouth]);

    // Suppose fingerID 7 is reused for empty screen space without an Up event
    h.step({{Kind::Down, 7, 0.5f, 0.5f}});
    assert(!h.pad[kBtnSouth]);
    assert(!h.state.has_active_or_pending());
}

void test_stationary_hold_keeps_active_beyond_idle_timeout()
{
    Harness h;
    const float gx = h.norm_x(kIdxG), gy = h.norm_y(kIdxG);

    h.step({{Kind::Down, 1, gx, gy}});
    assert(h.pad[kBtnSouth]);

    // Advance 6 seconds with no touch events (stationary hold)
    h.advance_frames(10, 600);
    assert(h.pad[kBtnSouth]);
    assert(h.state.has_active_or_pending());

    h.step({{Kind::Up, 1, gx, gy}});
    assert(!h.pad[kBtnSouth]);
    assert(!h.state.has_active_or_pending());
}

void test_other_buttons_and_hit_disambiguation()
{
    Harness h;
    const float bx = h.norm_x(kIdxB), by = h.norm_y(kIdxB);
    const float ax = h.norm_x(kIdxA), ay = h.norm_y(kIdxA);
    const float kx = h.norm_x(kIdxK), ky = h.norm_y(kIdxK);

    h.step({{Kind::Down, 1, bx, by}, {Kind::Down, 2, ax, ay}, {Kind::Down, 3, kx, ky}});
    assert(h.pad[kBtnNorth] && h.pad[kBtnWest] && h.pad[kBtnEast] && !h.pad[kBtnSouth]);

    h.advance_frames(5);
    h.step({{Kind::Up, 1, bx, by}, {Kind::Up, 2, ax, ay}, {Kind::Up, 3, kx, ky}});
    assert(!h.pad[kBtnNorth] && !h.pad[kBtnWest] && !h.pad[kBtnEast] && !h.pad[kBtnSouth]);

    // Inside the A/G overlap region but closer to G: must select G (kBtnSouth), not A (kBtnWest).
    const float gx = h.norm_x(kIdxG), gy = h.norm_y(kIdxG);
    const float near_gx = ax * 0.45f + gx * 0.55f;
    const float near_gy = ay * 0.45f + gy * 0.55f;
    h.step({{Kind::Down, 4, near_gx, near_gy}});
    assert(h.pad[kBtnSouth] && !h.pad[kBtnWest]);
    h.advance_frames(5);
    h.step({{Kind::Up, 4, near_gx, near_gy}});
    assert(!h.pad[kBtnSouth] && !h.pad[kBtnWest]);
}

} // namespace

int main()
{
    test_normal_hold_and_release();
    test_rapid_tap_repress_must_not_stick_defend();
    test_single_frame_same_frame_down_up_and_low_fps_cap();
    test_two_fingers_on_defend();
    test_defend_plus_throw_overlap_and_independent_release();
    test_action_cancel_releases_immediately();
    test_focus_loss_and_menu_reset_release_defend_and_stick();
    test_stale_finger_id_reuse_does_not_leak_previous_button();
    test_stationary_hold_keeps_active_beyond_idle_timeout();
    test_other_buttons_and_hit_disambiguation();
    std::printf("sc2_touch_test: all 10 regression tests passed\n");
    return 0;
}
