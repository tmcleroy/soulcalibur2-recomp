// SPDX-License-Identifier: GPL-3.0-or-later
// Controls on a touch screen (a phone), sc2_touch.h: a stick at the left, the four attack buttons at the right as
// the Xbox pad has them (Y vertical at the top, X horizontal at the left, B kick at the right, A guard at the
// bottom) with a throw (A+G) beside them, Start at the top right and the menu at the top left. They are a virtual SDL gamepad, so the game sees
// them as it sees a pad (the player's button layout, the in-game menu's Back and its d-pad and A and B). The layout
// follows cps2recomp's port/touch.cpp: in the window's side bars where the picture leaves room.
//
// The fingers come on Android's own thread, inside SDL's event queue: there they are only noted (watch). Each frame
// shown (sc2_touch_draw, the presenter's thread) takes them and moves the pad, with nothing of SDL's held.
#include "sc2_touch.h"
#include "sc2_touch_state.h"

#include "imgui.h"

#include <SDL3/SDL.h>

#include <atomic>
#include <cstdio>
#include <mutex>
#include <vector>

static_assert(SDL_GAMEPAD_BUTTON_DPAD_UP == sc2_touch::kButtonDpadUp);
static_assert(SDL_GAMEPAD_BUTTON_DPAD_DOWN == sc2_touch::kButtonDpadDown);
static_assert(SDL_GAMEPAD_BUTTON_DPAD_LEFT == sc2_touch::kButtonDpadLeft);
static_assert(SDL_GAMEPAD_BUTTON_DPAD_RIGHT == sc2_touch::kButtonDpadRight);
static_assert(SDL_GAMEPAD_BUTTON_COUNT <= sc2_touch::kButtonCount);

namespace {

// What the watch notes, under this lock alone
std::mutex lock;
std::vector<sc2_touch::TouchInput> noted;
bool key_pressed = false;
std::atomic<bool> shown{false}, waiting{false}, active_or_pending{false};
std::atomic<uint64_t> touched{0};

// The rest is the presenter thread's
SDL_JoystickID virtual_id = 0;
SDL_Joystick *virtual_pad = nullptr;
sc2_touch::TouchState state;

void set_button(int button, bool down)
{
    if (virtual_pad != nullptr && button >= 0 && button < SDL_GAMEPAD_BUTTON_COUNT) {
        SDL_SetJoystickVirtualButton(virtual_pad, static_cast<SDL_GamepadButton>(button), down);
    }
}

void attach()
{
    if (virtual_pad != nullptr) return;
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    desc.name = "Touch screen";
    virtual_id = SDL_AttachVirtualJoystick(&desc);
    if (virtual_id != 0) virtual_pad = SDL_OpenJoystick(virtual_id);
    if (virtual_pad == nullptr) std::fprintf(stderr, "sc2 touch: no virtual gamepad: %s\n", SDL_GetError());
}

void detach()
{
    state.reset(set_button);
    active_or_pending = false;
    if (virtual_pad == nullptr) return;
    SDL_CloseJoystick(virtual_pad);
    SDL_DetachVirtualJoystick(virtual_id);
    virtual_pad = nullptr;
    virtual_id = 0;
}

bool SDLCALL watch(void *, SDL_Event *event)
{
    using Kind = sc2_touch::TouchInput::Kind;
    switch (event->type) {
    case SDL_EVENT_FINGER_DOWN:
    case SDL_EVENT_FINGER_MOTION:
    case SDL_EVENT_FINGER_UP:
    case SDL_EVENT_FINGER_CANCELED: {
        Kind kind = Kind::Down;
        if (event->type == SDL_EVENT_FINGER_MOTION) kind = Kind::Motion;
        else if (event->type == SDL_EVENT_FINGER_UP) kind = Kind::Up;
        else if (event->type == SDL_EVENT_FINGER_CANCELED) kind = Kind::Cancel;
        std::lock_guard<std::mutex> hold(lock);
        noted.push_back({kind, static_cast<uint64_t>(event->tfinger.fingerID), event->tfinger.x, event->tfinger.y});
        touched = SDL_GetTicks();
        waiting = true;
        break;
    }
    case SDL_EVENT_WINDOW_FOCUS_LOST:
    case SDL_EVENT_WINDOW_MINIMIZED:
    case SDL_EVENT_WINDOW_HIDDEN:
    case SDL_EVENT_WILL_ENTER_BACKGROUND:
    case SDL_EVENT_DID_ENTER_BACKGROUND:
    case SDL_EVENT_TERMINATING: {
        std::lock_guard<std::mutex> hold(lock);
        noted.push_back({Kind::Reset, 0, 0.0f, 0.0f});
        waiting = true;
        break;
    }
    case SDL_EVENT_KEY_DOWN:
        // (a keyboard: the controls go, and their pad with them; not Android's Back, which opens the menu)
        if (event->key.scancode != SDL_SCANCODE_AC_BACK) {
            std::lock_guard<std::mutex> hold(lock);
            key_pressed = true;
            waiting = true;
        }
        break;
    default:
        break;
    }
    return true;
}

} // namespace

std::atomic<bool> menu_open{false};

void sc2_touch_set_menu(bool open)
{
    menu_open = open;
    std::lock_guard<std::mutex> hold(lock);
    noted.push_back({sc2_touch::TouchInput::Kind::Reset, 0, 0.0f, 0.0f});
    waiting = true;
}

bool sc2_touch_menu(void) { return menu_open; }

void sc2_touch_init(void)
{
    static bool started = false;
    if (started) return;
    started = true;
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0"); // (fingers are read as fingers, not as a mouse)
    SDL_AddEventWatch(watch, nullptr);
}

bool sc2_touch_shown(void)
{
    return waiting || active_or_pending || (shown && SDL_GetTicks() - touched < sc2_touch::kIdleMs);
}

void sc2_touch_draw(float w, float h)
{
    std::vector<sc2_touch::TouchInput> touches;
    bool key = false;
    {
        std::lock_guard<std::mutex> hold(lock);
        touches.swap(noted);
        key = key_pressed;
        key_pressed = false;
        waiting = false;
    }
    ++state.frame;
    const uint64_t now_ms = SDL_GetTicks();
    state.layout(static_cast<int>(w), static_cast<int>(h),
        SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_WEST,
        SDL_GAMEPAD_BUTTON_NORTH, SDL_GAMEPAD_BUTTON_START, SDL_GAMEPAD_BUTTON_BACK,
        set_button);

    bool activate_touch = false;
    for (const sc2_touch::TouchInput &touch : touches) {
        if (touch.kind == sc2_touch::TouchInput::Kind::Down ||
            touch.kind == sc2_touch::TouchInput::Kind::Motion ||
            touch.kind == sc2_touch::TouchInput::Kind::Up) {
            activate_touch = true;
            break;
        }
    }
    if (activate_touch && !shown) {
        shown = true;
        attach();
    }
    for (const sc2_touch::TouchInput &touch : touches) state.apply(touch, now_ms, set_button);
    state.finish_frame(now_ms, set_button);

    const bool active = state.has_active_or_pending();
    if (active) touched = now_ms;
    active_or_pending = active;

    if (key && shown && !active) {
        shown = false;
        detach();
    }
    if (!shown || (now_ms - touched >= sc2_touch::kIdleMs && !active)) return;

    ImDrawList *draw = ImGui::GetForegroundDrawList();
    const ImU32 ring = IM_COL32(255, 255, 255, 110), fill = IM_COL32(20, 25, 35, 90);
    draw->AddCircleFilled(ImVec2(state.stick_x, state.stick_y), state.stick_radius, fill, 48);
    draw->AddCircle(ImVec2(state.stick_x, state.stick_y), state.stick_radius, ring, 48, 3.0f);
    const ImVec2 knob = state.stick_held ? ImVec2(state.knob_x, state.knob_y) : ImVec2(state.stick_x, state.stick_y);
    draw->AddCircleFilled(knob, state.stick_radius * 0.42f,
        IM_COL32(230, 230, 240, state.stick_held ? 170 : 110), 32);

    ImFont *font = ImGui::GetFont();
    for (size_t i = 0; i < state.controls.size(); ++i) {
        const sc2_touch::Control &c = state.controls[i];
        const bool down = state.is_control_held(i);
        const ImU32 base_rgb = static_cast<ImU32>(c.colour) & static_cast<ImU32>(0x00ffffffu);
        const ImU32 colour = base_rgb | static_cast<ImU32>(down ? 0xe0000000u : 0x70000000u);
        draw->AddCircleFilled(ImVec2(c.x, c.y), c.radius, down ? colour : fill, 40);
        draw->AddCircle(ImVec2(c.x, c.y), c.radius, static_cast<ImU32>(c.colour), 40, 3.0f);
        const float size = c.radius * (c.label[1] == '\0' ? 0.9f : 0.5f);
        const ImVec2 text = font->CalcTextSizeA(size, 1e9f, 0.0f, c.label);
        draw->AddText(font, size, ImVec2(c.x - text.x * 0.5f, c.y - text.y * 0.5f),
            IM_COL32(255, 255, 255, 230), c.label);
    }
}
