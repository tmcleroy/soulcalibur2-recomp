/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "free_camera_adapter.h"
#include "sc2_fighter.h"
#include "d3d8_adapter.h"
#include "free_camera.h"
#include "input_host_win32.h"
#include "input_keyboard_win32.h"
#include "input_pulse_source.h"
#include "overlay_text.h"
#include "scene_freeze_adapter.h"
#include "screenshot.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Free camera: RECOMP_FREE_CAMERA=1 lets ` (backquote) or clicking the right
   stick take the game's camera and fly it. The flying itself is
   free_camera.c, shared with the DOA ports; this file is Soulcalibur II's side
   of it, with their controls (copied from DOA2U's adapter).

   SC2 keeps five views of 0x1240 bytes from 0x195FB0; each has a camera
   component at +0x7F0 (0x1967A0 + n * 0x1240, its vtable 0x176CE8 there).
   Every frame the game blends its camera sources into the component: the eye
   at +0x10, pitch, yaw and roll in turns at +0x120, +0x124, +0x128, and a
   focal length at +0x04 (the vertical field of view is 2 atan(224 / (focal
   * the view's +0x618))). The view build (0x29100) then turns it into the view
   matrix through the component's vtable: 0x1CC50 (with roll), and after
   blending again, 0x1CCB0 (without). Both are wrapped here (ECX is the
   component), so the free camera's eye and angles go in before each; the game
   copies them on into its culling and projection. The angles follow the
   game's own look-at (0x1EFE0): with d = target - eye, pitch = -atan2(dy,
   |d.xz|) and yaw = atan2(-dz, dx) - a quarter turn; the world's y points up.

   The game builds one view a frame, taking four of them in turn; the free
   camera goes into whichever is built.
   P, or START while flying, freezes the game (the frame put back each frame
   from a save state, sc2_frame.cpp) and O (or .) or the white button steps a frame;
   H or the black button hides the 2D overlay (life bars, timer, names). The
   controls are DOA2U's: left stick or WASD move, right stick, the arrow keys
   or a right-mouse drag look, the triggers or Q / E go down / up, A or Shift
   is fast, B or Ctrl slow, Z / X zoom (keys only). Y or R returns to the view
   it started from, and the d-pad's left or \\ turns it to the next fighter.
   F12, or the d-pad's right while flying, saves a screenshot. */
enum {
    VIEW_BUILD = 0x0001CC50u,
    VIEW_BUILD_LEVEL = 0x0001CCB0u,
    CAMERA_FIRST = 0x001967A0u,
    CAMERA_STRIDE = 0x1240u,
    CAMERA_COUNT = 5u,
    CAMERA_FOCAL = 0x04u,
    CAMERA_EYE = 0x10u,
    CAMERA_FOCUS = 0x100u,
    CAMERA_ANGLES = 0x120u,
    VIEW_SCALE = 0x618u - 0x7F0u,   /* the view's +0x618, from the component */
    XBOX_DPAD_LEFT = 0x0004u,
    XBOX_DPAD_RIGHT = 0x0008u,
    XBOX_START = 0x0010u,
    XBOX_BACK = 0x0020u,
    XBOX_RIGHT_THUMB = 0x0080u,
    ANALOG_PRESSED = 0x20u,
    STEP_REPEAT_AFTER_MS = 500u,
    /* Each fighter's position (x, y, z) at the player record's +0x2920:
       player 1's record at 0x399220, player 2's 0x10E60 after (sc2_fighter.h). */
    FIGHTER_POSITION = SC2_FIGHTER_RECORD + SC2_FIGHTER_FIELD(0x2920u),
    FIGHTER_STRIDE = SC2_FIGHTER_STRIDE,
    FIGHTERS = 2u,
};

/* The lifted bodies of the two view builds. */
extern void sub_0001CC50(void);
extern void sub_0001CCB0(void);

static const float STICK_DEADZONE = 0.15f;
/* At a field of view of one radian. */
static const float MOUSE_RADIANS_PER_PIXEL = 0.004f;
static const float SPEED_SCALE = 2.0f;
static const float TURN = 6.2831853f;
/* How far above a fighter's position to look. */
static const float FIGHTER_HEIGHT = 1.0f;
static const float FRAME_SECONDS = 1.0f / 60.0f;
static const double MAX_STEP_SECONDS = 0.1;

static struct {
    int enabled;              /* -1 until RECOMP_FREE_CAMERA is read */
    bool active, started;
    bool toggle_held, freeze_held, overlay_held, cycle_held, reset_held, aim_held, step_held, shot_held;
    bool r3_held;             /* the right stick's click, last sample */
    uint64_t step_since;
    uint32_t object;          /* the camera object flown, 0 for the next built */
    double stepped_seconds;   /* when the camera last stepped */
    RecompFreeCamera camera, home;
    RecompFreeCameraControls controls;
    /* Objects built in this frame and the last (bit per object). */
    uint32_t built_frame, built_now, built_last;
    bool cycle_pending, reset_pending;
    int aim_fighter;          /* next fighter to turn to; -1 none pending */
    unsigned aim_next;
    /* The hint lists pad buttons once a pad was last used, keys once a key was. */
    int controller_hints;
    /* Test hooks: RECOMP_FREE_CAMERA_AT=<build> takes the camera up at that
       build of any object; RECOMP_FREE_CAMERA_DRIFT=right,up,forward,turn,pitch
       holds those controls (each -1 to 1) from then on;
       RECOMP_FREE_CAMERA_FREEZE_AT=<build> freezes the scene at that
       build, and with RECOMP_FREE_CAMERA_HIDE=1 hides the 2D overlay;
       RECOMP_FREE_CAMERA_THAW_AT=<build> lets it go again;
       RECOMP_FREE_CAMERA_AIM=<fighter> turns the camera to that fighter. */
    unsigned long take_at, freeze_at, thaw_at, builds;
    bool drifting;
    RecompFreeCameraControls drift;
} state = {.enabled = -1, .aim_fighter = -1, .controller_hints = -1};

static bool enabled(void)
{
    if (state.enabled < 0) {
        const char *text = getenv("RECOMP_FREE_CAMERA");
        state.enabled = text != NULL && *text == '1';
        const char *at = getenv("RECOMP_FREE_CAMERA_AT");
        state.take_at = at ? strtoul(at, NULL, 10) : 0;
        const char *freeze = getenv("RECOMP_FREE_CAMERA_FREEZE_AT");
        state.freeze_at = freeze ? strtoul(freeze, NULL, 10) : 0;
        const char *thaw = getenv("RECOMP_FREE_CAMERA_THAW_AT");
        state.thaw_at = thaw ? strtoul(thaw, NULL, 10) : 0;
        const char *drift = getenv("RECOMP_FREE_CAMERA_DRIFT");
        if (drift) {
            float v[5] = {0};
            sscanf(drift, "%f,%f,%f,%f,%f", &v[0], &v[1], &v[2], &v[3], &v[4]);
            state.drift.move[0] = v[0]; state.drift.move[1] = v[1]; state.drift.move[2] = v[2];
            state.drift.look[0] = v[3]; state.drift.look[1] = v[4];
            state.drifting = true;
        }
    }
    return state.enabled != 0;
}

static void capture(uint32_t port, RecompInputGamepad *g, uint16_t stick_clicks);
static void frozen_frame(uint32_t frame);
static void hint_panel(void);

/* The pause (P, . a frame, H the HUD) is there with the free camera off too: the capture is always installed, and
   only the camera's own controls wait for it to be switched on. */
bool recomp_free_camera_install(void)
{
    recomp_input_host_set_capture_hook(capture);
    recomp_scene_freeze_set_frame_hook(frozen_frame);
    return enabled();
}

static float stick(int16_t value)
{
    float v = value / 32767.0f;
    if (fabsf(v) < STICK_DEADZONE) return 0.0f;
    return v > 1.0f ? 1.0f : v < -1.0f ? -1.0f : v;
}

static bool pressed(const RecompInputGamepad *g, unsigned analog)
{
    return g->analog_buttons[analog] >= ANALOG_PRESSED;
}

static double seconds_now(void)
{
    struct timespec now;
    return timespec_get(&now, TIME_UTC) == 0 ? 0.0 : (double)now.tv_sec + now.tv_nsec / 1e9;
}

static void set_active(bool active)
{
    state.active = active;
    state.started = false;
    state.object = 0;
    state.aim_fighter = -1;
    /* A frozen scene keeps building the camera while it flies. */
    if (!active) {
        recomp_scene_freeze_set(false);
        recomp_scene_freeze_set_overlay_hidden(false);
        RecompOverlayText empty;
        recomp_overlay_clear(&empty);
        recomp_overlay_publish(&empty);
    } else {
        hint_panel();
    }
    fprintf(stderr, "recomp free camera: %s\n", active ? "on" : "off");
}

static void hint_panel(void)
{
    RecompOverlayText text;
    recomp_overlay_clear(&text);
    if (!state.active && recomp_scene_freeze_active() && !recomp_scene_freeze_overlay_hidden()) {
        recomp_overlay_set_title(&text, "Paused");
        recomp_overlay_set_hint(&text, "P go on   O next frame   H hide the HUD   F5 save state   F9 load it");
    }
    if (state.active && !recomp_scene_freeze_overlay_hidden()) {
        recomp_overlay_set_title(&text, recomp_scene_freeze_active() ? "Free camera (paused)" : "Free camera");
        recomp_overlay_set_hint(&text, state.controller_hints > 0
            ? "Sticks fly  LT/RT down/up  A fast  B slow  Start pause  White step  Black hide HUD\n"
              "Y reset  D-pad left fighter  D-pad right screenshot  R3 off"
            : "WASD fly  Q/E down/up  arrows or right-drag look  Z/X zoom  P pause  O step  H hide HUD\n"
              "Shift fast  Ctrl slow  R reset  \\ fighter  F12 screenshot  ` off");
    }
    recomp_overlay_publish(&text);
}

/* The input host presses pad buttons for some keys (A S Z X Q W E R, the
   arrows, Return, Backspace). The camera reads those keys itself, so it
   leaves their buttons out while they are down: otherwise S would press Y
   and reset the view, and A would press X and change camera. */
static void leave_out_keys(RecompInputGamepad *g)
{
    static const struct { RecompInputHostKey key; unsigned analog; } analog_keys[] = {
        {RECOMP_INPUT_HOST_KEY_A, RECOMP_INPUT_ANALOG_X}, {RECOMP_INPUT_HOST_KEY_S, RECOMP_INPUT_ANALOG_Y},
        {RECOMP_INPUT_HOST_KEY_Z, RECOMP_INPUT_ANALOG_A}, {RECOMP_INPUT_HOST_KEY_X, RECOMP_INPUT_ANALOG_B},
        {RECOMP_INPUT_HOST_KEY_Q, RECOMP_INPUT_ANALOG_WHITE}, {RECOMP_INPUT_HOST_KEY_W, RECOMP_INPUT_ANALOG_BLACK},
        {RECOMP_INPUT_HOST_KEY_E, RECOMP_INPUT_ANALOG_LTRIG}, {RECOMP_INPUT_HOST_KEY_R, RECOMP_INPUT_ANALOG_RTRIG},
    };
    static const struct { RecompInputHostKey key; uint16_t button; } button_keys[] = {
        {RECOMP_INPUT_HOST_KEY_UP, 0x0001u}, {RECOMP_INPUT_HOST_KEY_DOWN, 0x0002u},
        {RECOMP_INPUT_HOST_KEY_LEFT, 0x0004u}, {RECOMP_INPUT_HOST_KEY_RIGHT, 0x0008u},
        {RECOMP_INPUT_HOST_KEY_RETURN, XBOX_START}, {RECOMP_INPUT_HOST_KEY_BACKSPACE, XBOX_BACK},
    };
    for (size_t i = 0; i < sizeof analog_keys / sizeof analog_keys[0]; ++i)
        if (recomp_input_host_key(analog_keys[i].key)) g->analog_buttons[analog_keys[i].analog] = 0u;
    for (size_t i = 0; i < sizeof button_keys / sizeof button_keys[0]; ++i)
        if (recomp_input_host_key(button_keys[i].key)) g->buttons &= (uint16_t)~button_keys[i].button;
}

static bool any_key(void)
{
    for (int k = 0; k < RECOMP_INPUT_HOST_KEY_COUNT; ++k)
        if (recomp_input_host_key((RecompInputHostKey)k)) return true;
    return false;
}

/* Every pad and keyboard sample (input_host_win32.c), and once a frame from
   the freeze: the toggle and freeze keys, and while flying the camera's
   controls; the game is then handed a still pad. The right stick's click
   comes apart from the pad, since the input host keeps it from the game. */
static void capture(uint32_t port, RecompInputGamepad *g, uint16_t stick_clicks)
{
    if (port != 0u) {
        if (state.active) memset(g, 0, sizeof *g);
        return;
    }
    /* Not flying, the game still gets the keys' buttons. */
    RecompInputGamepad raw = *g;
    leave_out_keys(g);
    /* SC2's pads go through the player's button layout, which leaves the stick
       clicks out unless they are bound: the click is read from the pad itself. */
    (void)stick_clicks;
    const bool r3 = (recomp_input_host_pad_buttons() & (1ull << SDL_GAMEPAD_BUTTON_RIGHT_STICK)) != 0u;
    bool any_pad = g->buttons != 0u || stick_clicks != 0u || stick(g->thumb_lx) || stick(g->thumb_ly) ||
        stick(g->thumb_rx) || stick(g->thumb_ry);
    for (unsigned i = 0; i < 8u && !any_pad; ++i) any_pad = pressed(g, i);
    if (state.controller_hints < 0) {
        const char *deck = getenv("SteamDeck");
        state.controller_hints = deck != NULL && strcmp(deck, "1") == 0;
    }
    if (any_key()) state.controller_hints = 0;
    else if (any_pad) state.controller_hints = 1;
#define KEY(name) recomp_input_host_key(RECOMP_INPUT_HOST_KEY_##name)

    const bool r3_press = r3 && !state.r3_held;
    state.r3_held = r3;
    /* (F1 is the game's menu here, so only ` and the right stick's click.) */
    const bool toggle_keys = KEY(GRAVE);
    if (enabled() && ((toggle_keys && !state.toggle_held) || r3_press)) set_active(!state.active);
    state.toggle_held = toggle_keys;

    /* P pauses anywhere; START only while flying, since the game pauses with it. */
    const bool freeze = KEY(P) || (state.active && (g->buttons & XBOX_START));
    if (freeze && !state.freeze_held) {
        const bool pause = !recomp_scene_freeze_active();
        recomp_scene_freeze_set(pause);
        if (!pause && !state.active) recomp_scene_freeze_set_overlay_hidden(false);
        hint_panel();
    }
    state.freeze_held = freeze;
    const bool paused = recomp_scene_freeze_active();

    const bool overlay = (state.active || paused) && (KEY(H) || (state.active && pressed(g, RECOMP_INPUT_ANALOG_BLACK)));
    if (overlay && !state.overlay_held) {
        recomp_scene_freeze_set_overlay_hidden(!recomp_scene_freeze_overlay_hidden());
        hint_panel();
    }
    state.overlay_held = overlay;

    /* A step press moves one frame; held, it steps every frame after a while. */
    /* (O as in the other ports, . as this one always had it) */
    const bool step = (state.active || paused) && (KEY(PERIOD) || recomp_input_scancode_held(SDL_SCANCODE_O) ||
        (state.active && pressed(g, RECOMP_INPUT_ANALOG_WHITE)));
    const uint64_t now = SDL_GetTicks();
    if (step && !state.step_held) { recomp_scene_freeze_step(); state.step_since = now; }
    else if (step && now - state.step_since >= STEP_REPEAT_AFTER_MS) recomp_scene_freeze_step();
    state.step_held = step;

    /* F12 anywhere, the d-pad's right while flying (the right arrow, which
       also presses it, is left out above). */
    const bool shot = KEY(F12) || (state.active && (g->buttons & XBOX_DPAD_RIGHT));
    if (shot && !state.shot_held) recomp_screenshot_request();
    state.shot_held = shot;

    /* Sampled while the camera is away too, so a drag never spans it. */
    float dx, dy;
    const bool dragging = recomp_input_host_mouse_drag(&dx, &dy);
    if (!state.active) { *g = raw; return; }

    const bool cycle = KEY(C) || pressed(g, RECOMP_INPUT_ANALOG_X);
    if (cycle && !state.cycle_held) state.cycle_pending = true;
    state.cycle_held = cycle;
    const bool reset = KEY(R) || pressed(g, RECOMP_INPUT_ANALOG_Y);
    if (reset && !state.reset_held) state.reset_pending = true;
    state.reset_held = reset;
    /* \ or the d-pad's left (the left arrow, which also presses it, is left
       out above). */
    const bool aim = KEY(BACKSLASH) || (g->buttons & XBOX_DPAD_LEFT);
    if (aim && !state.aim_held) {
        state.aim_fighter = (int)(state.aim_next++ % FIGHTERS);
        fprintf(stderr, "recomp free camera: turning to fighter %d\n", state.aim_fighter);
    }
    state.aim_held = aim;

    RecompFreeCameraControls c;
    memset(&c, 0, sizeof c);
    c.move[0] = stick(g->thumb_lx);
    c.move[2] = stick(g->thumb_ly);
    c.look[0] = stick(g->thumb_rx);
    c.look[1] = stick(g->thumb_ry);
    c.move[1] = (g->analog_buttons[RECOMP_INPUT_ANALOG_RTRIG] - g->analog_buttons[RECOMP_INPUT_ANALOG_LTRIG]) / 255.0f;
    c.fast = pressed(g, RECOMP_INPUT_ANALOG_A);
    c.slow = pressed(g, RECOMP_INPUT_ANALOG_B);
    if (KEY(D)) c.move[0] += 1.0f;
    if (KEY(A)) c.move[0] -= 1.0f;
    if (KEY(W)) c.move[2] += 1.0f;
    if (KEY(S)) c.move[2] -= 1.0f;
    if (KEY(E)) c.move[1] += 1.0f;
    if (KEY(Q)) c.move[1] -= 1.0f;
    if (KEY(RIGHT)) c.look[0] += 1.0f;
    if (KEY(LEFT)) c.look[0] -= 1.0f;
    if (KEY(UP)) c.look[1] += 1.0f;
    if (KEY(DOWN)) c.look[1] -= 1.0f;
    if (KEY(Z)) c.zoom += 1.0f;
    if (KEY(X)) c.zoom -= 1.0f;
    c.fast |= KEY(SHIFT);
    c.slow |= KEY(CONTROL);
#undef KEY
    /* A right-mouse drag over the game turns the camera, by the pixels moved. */
    if (dragging) {
        const float scale = MOUSE_RADIANS_PER_PIXEL * (state.camera.fov > 0.0f ? state.camera.fov : 1.0f);
        c.turn[0] += dx * scale;
        c.turn[1] -= dy * scale;
    }
    /* Turns from several samples in one frame add up until the camera steps. */
    c.turn[0] += state.controls.turn[0];
    c.turn[1] += state.controls.turn[1];
    state.controls = c;
    memset(g, 0, sizeof *g);
}

/* While frozen the game stops polling; the freeze calls this once a frame. */
static void frozen_frame(uint32_t frame)
{
    (void)frame;
    RecompInputGamepad pad;
    recomp_input_host_sample(0u, &pad);
    hint_panel();
}

/* The game's world has y up, as the shared model's does. */
static void to_model(float v[3]) { (void)v; }

static float view_scale(uint32_t object)
{
    float scale;
    recomp_guest_load(&scale, object + VIEW_SCALE, 4);
    return scale > 0.0f && isfinite(scale) ? scale : 1.0f;
}

/* The component's eye and angles as the game's own look-at would give them
   for an eye and a target (model points), and the focal length for a field
   of view. Called before each view build. */
static void store(uint32_t object, const float eye[3], const float target[3], float fov)
{
    float e[3] = {eye[0], eye[1], eye[2]}, t[3] = {target[0], target[1], target[2]};
    to_model(e);
    to_model(t);
    const float dx = t[0] - e[0], dy = t[1] - e[1], dz = t[2] - e[2];
    float angles[3];
    angles[0] = -atan2f(dy, sqrtf(dx * dx + dz * dz)) / TURN;
    angles[1] = atan2f(-dz, dx) / TURN - 0.25f;
    angles[1] -= floorf(angles[1]);
    angles[2] = 0.0f;
    const float focal = 224.0f / (tanf(fov * 0.5f) * view_scale(object));
    recomp_guest_store(object + CAMERA_EYE, e, 12);
    recomp_guest_store(object + CAMERA_ANGLES, angles, 12);
    recomp_guest_store(object + CAMERA_FOCAL, &focal, 4);
    recomp_guest_store(object + CAMERA_FOCUS, t, 12);
}

/* Test hook (pictures): RECOMP_FREE_CAMERA_ORBIT=degrees,distance,height,
   look_height[,degrees_per_second] puts the camera on a circle round the two
   fighters' midpoint, looking at the midpoint raised by look_height. */
static void orbit_test(float seconds)
{
    static int have = -1;
    static float a, distance, height, look, spin;
    if (have < 0) {
        const char *t = getenv("RECOMP_FREE_CAMERA_ORBIT");
        spin = 0.0f;
        have = t && sscanf(t, "%f,%f,%f,%f,%f", &a, &distance, &height, &look, &spin) >= 4;
    }
    if (!have) return;
    float p[2][3];
    for (unsigned i = 0; i < FIGHTERS; ++i) recomp_guest_load(p[i], FIGHTER_POSITION + i * FIGHTER_STRIDE, 12);
    float m[3];
    for (int i = 0; i < 3; ++i) m[i] = 0.5f * (p[0][i] + p[1][i]);
    if (!isfinite(m[0]) || !isfinite(m[1]) || !isfinite(m[2])) return;
    a += spin * seconds;
    const float r = a * (TURN / 360.0f);
    state.camera.position[0] = m[0] + cosf(r) * distance;
    state.camera.position[1] = m[1] + height;
    state.camera.position[2] = m[2] + sinf(r) * distance;
    const float at[3] = {m[0], m[1] + look, m[2]};
    recomp_free_camera_look_at(&state.camera, at);
    static unsigned logged;
    if (logged++ % 60u == 0u)
        fprintf(stderr, "recomp free camera: orbit fighters (%.2f %.2f %.2f) (%.2f %.2f %.2f) angle %.0f\n",
            p[0][0], p[0][1], p[0][2], p[1][0], p[1][1], p[1][2], a);
}

static bool begin_from(uint32_t object)
{
    float eye[3], focus[3], angles[3], focal;
    recomp_guest_load(eye, object + CAMERA_EYE, 12);
    recomp_guest_load(focus, object + CAMERA_FOCUS, 12);
    recomp_guest_load(angles, object + CAMERA_ANGLES, 12);
    recomp_guest_load(&focal, object + CAMERA_FOCAL, 4);
    if (!(focal > 0.0f) || !isfinite(focal)) return false;
    /* The way the angles look, at the focus point's distance (or 3). */
    const float pitch = angles[0] * TURN, yaw = (angles[1] + 0.25f) * TURN;
    const float forward[3] = {cosf(pitch) * cosf(yaw), -sinf(pitch), -cosf(pitch) * sinf(yaw)};
    const float fx = focus[0] - eye[0], fy = focus[1] - eye[1], fz = focus[2] - eye[2];
    float distance = sqrtf(fx * fx + fy * fy + fz * fz);
    if (!(distance > 0.5f && distance < 50.0f)) distance = 3.0f;
    float target[3];
    for (int i = 0; i < 3; ++i) target[i] = eye[i] + forward[i] * distance;
    to_model(eye);
    to_model(target);
    const float fov = 2.0f * atanf(224.0f / (focal * view_scale(object)));
    if (!recomp_free_camera_begin(&state.camera, eye, target, fov)) return false;
    state.camera.speed *= SPEED_SCALE;
    state.home = state.camera;
    state.started = true;
    state.object = object;
    state.stepped_seconds = seconds_now();
    fprintf(stderr, "recomp free camera: flying camera %u\n", (object - CAMERA_FIRST) / CAMERA_STRIDE);
    return true;
}

static bool ours(uint32_t object)
{
    return object >= CAMERA_FIRST && object < CAMERA_FIRST + CAMERA_COUNT * CAMERA_STRIDE &&
        (object - CAMERA_FIRST) % CAMERA_STRIDE == 0u;
}

/* 0x1CC50: the view matrix of the component in ECX. The camera steps here,
   once a frame. */
static void view_build(void)
{
    const uint32_t object = recomp_runtime.registers.ecx;
    if (ours(object)) {
        ++state.builds;
        if (state.take_at && state.builds == state.take_at && !state.active) {
            set_active(true);
            if (getenv("RECOMP_FREE_CAMERA_AIM")) state.aim_fighter = atoi(getenv("RECOMP_FREE_CAMERA_AIM"));
        }
        if (state.thaw_at && state.builds == state.thaw_at) recomp_scene_freeze_set(false);
        if (state.freeze_at && state.builds == state.freeze_at) {
            recomp_scene_freeze_set(true);
            const char *hide = getenv("RECOMP_FREE_CAMERA_HIDE");
            if (hide && *hide == '1') { recomp_scene_freeze_set_overlay_hidden(true); hint_panel(); }
        }
        /* Test hook: RECOMP_FREE_CAMERA_TEST_AT=<frame> requests a screenshot
           and turns the camera to the next fighter. */
        static long test_at = -2;
        if (test_at == -2) { const char *t = getenv("RECOMP_FREE_CAMERA_TEST_AT"); test_at = t ? atol(t) : -1; }
        if (test_at >= 0 && recomp_d3d8_swap_counter() >= (uint32_t)test_at) {
            test_at = -1;
            recomp_screenshot_request();
            if (state.active) state.aim_fighter = (int)(state.aim_next++ % FIGHTERS);
            fprintf(stderr, "recomp free camera: test screenshot requested\n");
        }
        /* The game builds its views in turn, one a frame: the one camera goes
           into whichever is built. */
        if (state.active && !state.started) begin_from(object);
        if (state.started) state.object = object;
        if (state.active && state.started) {
            if (state.reset_pending) { state.reset_pending = false; state.camera = state.home; }
            if (state.aim_fighter >= 0) {
                float at[3];
                recomp_guest_load(at, FIGHTER_POSITION + (uint32_t)state.aim_fighter * FIGHTER_STRIDE, 12);
                at[1] += FIGHTER_HEIGHT;
                to_model(at);
                if (isfinite(at[0]) && isfinite(at[1]) && isfinite(at[2])) recomp_free_camera_look_at(&state.camera, at);
                state.aim_fighter = -1;
            }
            const double now = seconds_now();
            double seconds = now - state.stepped_seconds;
            if (!(seconds > 0.0) || seconds > MAX_STEP_SECONDS) seconds = FRAME_SECONDS;
            state.stepped_seconds = now;
            recomp_free_camera_step(&state.camera, state.drifting ? &state.drift : &state.controls, (float)seconds);
            orbit_test((float)seconds);
            state.controls.turn[0] = state.controls.turn[1] = 0.0f;
            float target[3];
            recomp_free_camera_target(&state.camera, target);
            store(object, state.camera.position, target, state.camera.fov);
            hint_panel();
        }
    }
    sub_0001CC50();
}

/* 0x1CCB0: the level view matrix, built after the game blends its sources
   into the component again; the free camera's values go back in first. */
static void view_build_level(void)
{
    const uint32_t object = recomp_runtime.registers.ecx;
    if (state.active && state.started && object == state.object) {
        float target[3];
        recomp_free_camera_target(&state.camera, target);
        store(object, state.camera.position, target, state.camera.fov);
    }
    sub_0001CCB0();
}

RecompFunction recomp_free_camera_lookup_manual(uint32_t guest_address)
{
    if (!enabled()) return NULL;
    return guest_address == VIEW_BUILD ? view_build : guest_address == VIEW_BUILD_LEVEL ? view_build_level : NULL;
}
