/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Training. shortcut: the aids read and write the game's state between its
   frames, at addresses found from RAM dumps of fights (docs/bring-up.md,
   "Training facts"); they move into rewrites of the routines that own that
   state once those are traced (the damage routine at 0xd8760, the round
   timer).

   - A fighter's record: player 1's at 0x399220, player 2's 0x10e60 after.
     Its health is a float at +0x155c, 240 at full (the maximum at +0x1568);
     the frames of hit stun left at +0x1594; a hit's reaction (staggered,
     launched, knocked down) is going on while the word at +0xf8 is not 0;
     its place (x, y, z) at +0x2920; who plays it at +0x50 (1 a controller,
     3 the computer, 5 Practice's dummy).
   - The unlockables: the item states at 0x3bb368, a byte each for 0x116
     items (3: had), kept in the game's save.
   - The mode: the byte at 0x39895e (1 Arcade, 7 Practice).
   - The round: its number at 0x398962 (a byte, from 1); the clock's seconds
     left at 0x3989d4 and the frames of the second at 0x3989d8 (counting
     down from 59 once the clock moves), the round's length (the Options
     setting) at 0x3989e8, -1 for none (Practice). In Practice the fighting
     begins when the float at 0x39d2ac (in player 1's record) turns from 0.
   - The fight's camera: its eye at 0x1967b0 and what it looks at 0x1967a0 +
     0x100; the world's y is up, the picture's x the other way round from the
     camera's right. The projection it is drawn with: pixels a unit at a
     unit's depth across at 0x197370 and down at 0x197384 (across is down's
     times 1.1 in 4:3, about 0.87 in the game's own 16:9 mode).
   - The hit boxes: a fighter's collision shapes, a list from the pointer at
     +0x1a80 (+0x1a7c of them, each linked to the next at +0x18, its size at
     +0x1c, its kind at +0x16): spheres (0) where it can be hit, the world
     centre at +0x30 and the radius at +0x50 (the one of 0.8 or more at the
     waist, its pushing sphere); capsules (1) along the weapon and limbs that
     hit, their ends in the world at +0x50 and +0x60. The game tests them in
     the shapes' own methods (0x76bd0 sphere to sphere) and sets the contact
     masks (+0x1870, read at 0x49e0b). The fighter's attack is live while the
     byte at +0xc1 is 1 (until it connects).
   - Frame data: the byte at +0xa0 is set while a fighter is not free (in a
     move of its own, from its first frame to its last, or reeling from a hit
     or a guard); hit and guard stun both count down at +0x1594, and +0x24c
     is 6 while guarding. A move's startup is its frames up to its first live
     one, its recovery those after its last; the advantage, the frames
     between the attacker's being free again and the other's. */
#include "sc2_training.h"
#include "sc2_panel.h"
#include "sc2_state.h"

extern "C" {
#include "kernel_abi.h"
#include "d3d_vblank.h"
#include "scene_freeze_adapter.h"
#include "runtime.h"
#include "sc2_hooks.h"
}

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace sc2 {

namespace {

enum : uint32_t {
    kScene = 0x00398700u,
    kFightScene = 7u,
    kPlayer1 = 0x00399220u,
    kPlayerStride = 0x00010e60u,
    kHealth = 0x155cu,
    kHealthMax = 0x1568u,
    kHitStun = 0x1594u,
    kReaction = 0x00f8u,
    kPlace = 0x2920u,
    kControl = 0x0050u,
    kController = 1u,
    kComputer = 3u,
    /* The endless battle: how long a touched controller keeps its fighter */
    kTakenFrames = 60u * 5u,
    kPracticeDummy = 5u,
    kRound = 0x00398962u,
    kMode = 0x0039895eu,
    kPractice = 7u,
    kTimerSeconds = 0x003989d4u,
    kTimerFrames = 0x003989d8u,
    kRoundLength = 0x003989e8u,
    kPracticeBegun = 0x0039d2acu,
    kCameraEye = 0x001967b0u,
    kCameraFocus = 0x001968a0u,
    kProjectionX = 0x00197370u,
    kProjectionY = 0x00197384u,
    kShapes = 0x1a80u,
    kShapeCount = 0x1a7cu,
    kAttackLive = 0x00c1u,
    kBusy = 0x00a0u,
    kGuardState = 0x024cu,
    kGuarding = 6u,
    /* Frames of nothing before the frame meter starts afresh, and of nothing it shows after a move */
    kMeterIdle = 30u,
    kMeterTail = 12u,
    kItems = 0x003bb368u,
    kItemCount = 0x116u,
    kItemHad = 3u,
    kItemsPacked = 0x003bb1f0u,
    kItemsPackedBytes = 75u,
    /* How long health must stay put before a refill: the combo is over. */
    kRefillFrames = 90u,
    /* The longest recording: 20 seconds */
    kRecordFrames = 60u * 20u,
    /* How long a note stays up, and the combo readout after a combo */
    kNoteFrames = 60u * 3u,
};

float LoadFloat(uint32_t address)
{
    float value;
    std::memcpy(&value, recomp_memory(address, sizeof value), sizeof value);
    return value;
}

void StoreFloat(uint32_t address, float value)
{
    std::memcpy(recomp_memory(address, sizeof value), &value, sizeof value);
}

uint32_t Record(int player) { return kPlayer1 + static_cast<uint32_t>(player) * kPlayerStride; }

bool InFight() { return *recomp_memory_u32(kScene) == kFightScene && !sc2_net_live; }

/* Practice, where training's displays show (hit boxes, frame data, input display, combo readout) */
bool InPractice() { return InFight() && *recomp_memory(kMode, 1u) == kPractice; }

/* --- the input display --- */

/* A change of what a player held: the direction (numpad, from the player's side: 6 is towards the other) and the
   buttons (A horizontal, B vertical, K kick, G guard), and for how many frames */
struct Held {
    uint8_t direction = 5;
    uint8_t buttons = 0;
    uint32_t frames = 0;
};
std::vector<Held> history[2];
RecompInputGamepad seen[2];

uint8_t Direction(const RecompInputGamepad &pad, bool mirrored)
{
    constexpr int kDead = 16384;
    bool up = (pad.buttons & 0x1u) || pad.thumb_ly > kDead, down = (pad.buttons & 0x2u) || pad.thumb_ly < -kDead;
    bool left = (pad.buttons & 0x4u) || pad.thumb_lx < -kDead, right = (pad.buttons & 0x8u) || pad.thumb_lx > kDead;
    if (mirrored) std::swap(left, right);
    const int column = left && !right ? 0 : right && !left ? 2 : 1;
    const int row = down && !up ? 0 : up && !down ? 2 : 1;
    return static_cast<uint8_t>(1 + column + 3 * row);
}

/* (the Xbox's X, Y, B and A, as Soulcalibur II names them; the black and white buttons and the triggers are
   shown as the player's own layout makes them: their own letters) */
uint8_t Buttons(const RecompInputGamepad &pad)
{
    constexpr uint8_t kOn = 0x40u;
    uint8_t buttons = 0;
    if (pad.analog_buttons[2] > kOn) buttons |= 1u;   /* X: A */
    if (pad.analog_buttons[3] > kOn) buttons |= 2u;   /* Y: B */
    if (pad.analog_buttons[1] > kOn) buttons |= 4u;   /* B: K */
    if (pad.analog_buttons[0] > kOn) buttons |= 8u;   /* A: G */
    if (pad.analog_buttons[4] > kOn) buttons |= 16u;  /* black */
    if (pad.analog_buttons[5] > kOn) buttons |= 32u;  /* white */
    if (pad.analog_buttons[6] > kOn || pad.analog_buttons[7] > kOn) buttons |= 64u; /* a trigger */
    return buttons;
}

/* Whether player 1 stands on the left of the picture: the line from player 1 to player 2 against the camera's right */
bool player1_left = true;

void Sides()
{
    float eye[3], focus[3], one[3], two[3];
    std::memcpy(eye, recomp_memory(kCameraEye, 12u), 12u);
    std::memcpy(focus, recomp_memory(kCameraFocus, 12u), 12u);
    std::memcpy(one, recomp_memory(Record(0) + kPlace, 12u), 12u);
    std::memcpy(two, recomp_memory(Record(1) + kPlace, 12u), 12u);
    const float right_x = -(focus[2] - eye[2]), right_z = focus[0] - eye[0];
    const float side = (two[0] - one[0]) * right_x + (two[2] - one[2]) * right_z;
    if (std::isfinite(side) && std::fabs(side) > 1e-4f) player1_left = side > 0.0f;
}


/* --- the combo readout: by the player who hits --- */

struct Combo {
    unsigned hits = 0;
    float damage = 0.0f, last = 0.0f, best = 0.0f;
};
Combo combo[2];
float health_before[2];
bool reacting_before[2];

bool Reacting(int player)
{
    const uint32_t record = Record(player);
    return *reinterpret_cast<const int32_t *>(recomp_memory(record + kHitStun, 4u)) > 0 ||
        *recomp_memory_u32(record + kReaction) != 0u;
}

/* --- frame data: each fighter's last move, the advantage after it, and the meter --- */

struct Move {
    bool on = false;
    uint32_t start = 0, first_live = 0, last_live = 0, live = 0;
    bool contact = false, hit = false;
};
struct Shown {
    bool valid = false;
    uint32_t startup = 0, active = 0, recovery = 0;
    bool contact = false, hit = false;
    bool advantage_known = false;
    int advantage = 0;
};
struct Advantage {
    bool waiting = false;
    uint32_t attacker_free = 0, defender_free = 0;
};
Move move[2];
Shown shown[2];
Advantage advantage[2]; /* (by attacker) */
uint32_t frame_number, meter_idle = kMeterIdle;
int32_t stun_before[2];
std::vector<uint8_t> meter[2];

/* --- the endless battle --- */

uint32_t control_before[2];      /* (who played each fighter before: given back after; 0 none taken) */
uint32_t taken_until[2];         /* (the frame a touched controller keeps its fighter until) */
bool endless_hud;                /* (the HUD hidden by it) */

bool Touched(const RecompInputGamepad &pad)
{
    constexpr int kDead = 12000;
    if (pad.buttons & 0x0fu) return true;
    for (unsigned i = 0; i < 8u; ++i)
        if (pad.analog_buttons[i] > 0x40u) return true;
    return pad.thumb_lx > kDead || pad.thumb_lx < -kDead || pad.thumb_ly > kDead || pad.thumb_ly < -kDead;
}

void Endless(bool on, uint32_t frame)
{
    for (int p = 0; p < 2; ++p) {
        uint32_t *control = recomp_memory_u32(Record(p) + kControl);
        if (on) {
            if (control_before[p] == 0u) control_before[p] = *control;
            if (Touched(seen[p])) taken_until[p] = frame + kTakenFrames;
            *control = frame < taken_until[p] ? kController : kComputer;
            /* (no knockouts) */
            const float max = LoadFloat(Record(p) + kHealthMax);
            if (max == 240.0f && LoadFloat(Record(p) + kHealth) < max) StoreFloat(Record(p) + kHealth, max);
        } else if (control_before[p] != 0u) {
            *control = control_before[p];
            control_before[p] = 0u;
            taken_until[p] = 0u;
        }
    }
    if (on) {
        /* (no clock: held where it started) */
        const uint32_t length = *recomp_memory_u32(kRoundLength);
        uint32_t *seconds = recomp_memory_u32(kTimerSeconds);
        if (length > 0u && length <= 99u && *seconds < length) *seconds = length;
    }
    if (on != endless_hud) {
        recomp_scene_freeze_set_overlay_hidden(on);
        endless_hud = on;
    }
}

/* --- the round's start, for starting over --- */

SaveState round_start;
uint32_t round_key = 0xffffffffu; /* (the scene's switch count and the round's number: a round of its own) */
bool round_armed, practice_before;
bool start_over_asked;

/* --- the dummy --- */

std::vector<RecompInputGamepad> recording;
size_t replay_at;
RecompInputGamepad record_source;
Dummy dummy_before = Dummy::Off;
bool practice_dummy;

char note[96];
uint32_t note_frames;

/* The hit boxes the last frame showed */
Sc2PanelShape shapes[SC2_PANEL_SHAPES];
unsigned shape_count;

} // namespace

Training &TrainingState()
{
    static Training state;
    return state;
}

void TrainingNote(const char *text)
{
    std::snprintf(note, sizeof note, "%s", text);
    note_frames = kNoteFrames;
}

bool TrainingStartOver()
{
    if (sc2_net_live) return false;
    if (!InFight() || !round_start.valid) {
        TrainingNote("Start over: in a fight, once its round has begun.");
        return false;
    }
    start_over_asked = true;
    return true;
}

bool TrainingRestart()
{
    if (!start_over_asked) return false;
    start_over_asked = false;
    if (!InFight() || !round_start.valid) return false;
    recomp_kernel_drain_others();
    LoadFrame(round_start);
    for (int p = 0; p < 2; ++p) {
        history[p].clear();
        combo[p].hits = 0;
        health_before[p] = LoadFloat(Record(p) + kHealth);
        reacting_before[p] = false;
    }
    replay_at = 0;
    return true;
}

void TrainingFrame()
{
    Training &training = TrainingState();
    if (!InFight()) {
        practice_dummy = false;
        control_before[0] = control_before[1] = 0u;
        if (endless_hud) {
            recomp_scene_freeze_set_overlay_hidden(false);
            endless_hud = false;
        }
        round_start.valid = false;
        round_key = 0xffffffffu;
        for (int p = 0; p < 2; ++p) {
            history[p].clear();
            combo[p] = Combo{};
            move[p] = Move{};
            shown[p] = Shown{};
            advantage[p] = Advantage{};
            meter[p].clear();
        }
        return;
    }
    /* The round's start: the first frame its clock moves (or, in Practice, its fighting begins) */
    const uint32_t key = sc2_scene_switches << 8 | *recomp_memory(kRound, 1u);
    if (key != round_key) {
        round_key = key;
        round_armed = true;
        practice_before = LoadFloat(kPracticeBegun) != 0.0f;
        for (int p = 0; p < 2; ++p) combo[p] = Combo{};
    }
    if (round_armed) {
        const uint32_t length = *recomp_memory_u32(kRoundLength);
        bool begun;
        if (length == 0xffffffffu) {
            const bool now = LoadFloat(kPracticeBegun) != 0.0f;
            begun = now && !practice_before;
            practice_before = now;
        } else {
            begun = *recomp_memory_u32(kTimerSeconds) < length || *recomp_memory_u32(kTimerFrames) != 0u;
        }
        if (begun) {
            round_armed = false;
            recomp_kernel_drain_others();
            SaveFrame(round_start);
            std::fprintf(stderr, "recomp sc2: training: the round's start kept\n");
        }
    }
    /* The hits: a fall in health is a hit; it is one more of a combo while the one hit was still reacting */
    bool hit[2] = {false, false};
    for (int p = 0; p < 2; ++p) {
        const float health = LoadFloat(Record(p) + kHealth);
        Combo &by = combo[1 - p];
        if (health < health_before[p] - 0.01f && health_before[p] <= 240.0f) {
            hit[p] = true;
            const float damage = health_before[p] - health;
            if (!reacting_before[p] || by.hits == 0) {
                by.hits = 0;
                by.damage = 0.0f;
            }
            ++by.hits;
            by.damage += damage;
            by.last = damage;
            by.best = std::max(by.best, by.damage);
        }
        health_before[p] = health;
        reacting_before[p] = Reacting(p);
    }
    /* Frame data and the meter */
    ++frame_number;
    uint8_t cell[2];
    bool free_now[2], contact[2];
    for (int p = 0; p < 2; ++p) {
        const uint32_t record = Record(p);
        const bool busy = *recomp_memory(record + kBusy, 1u) != 0u;
        const bool live = *recomp_memory(record + kAttackLive, 1u) != 0u;
        const int32_t stun = *reinterpret_cast<const int32_t *>(recomp_memory(record + kHitStun, 4u));
        const bool reeling = stun > 0 || *recomp_memory_u32(record + kReaction) != 0u ||
            *recomp_memory(record + kGuardState, 1u) == kGuarding;
        contact[p] = stun > stun_before[p] || hit[p];   /* (this fighter was hit or guarded) */
        stun_before[p] = stun;
        free_now[p] = !busy && !reeling;
        const bool own = busy && !reeling;
        cell[p] = live ? SC2_METER_ACTIVE
            : own ? (move[p].on && move[p].first_live ? SC2_METER_RECOVERY : SC2_METER_STARTUP)
            : reeling ? SC2_METER_STUN : SC2_METER_FREE;
        Move &m = move[p];
        if ((own || live) && !m.on) {
            m = Move{};
            m.on = true;
            m.start = frame_number;
        }
        if (m.on && live) {
            if (!m.first_live) m.first_live = frame_number;
            m.last_live = frame_number;
            ++m.live;
        }
        if (m.on && !own && !live) {
            m.on = false;
            if (m.first_live) {
                Shown &out = shown[p];
                out = Shown{};
                out.valid = true;
                out.startup = m.first_live - m.start + 1u;
                out.active = m.live;
                out.recovery = frame_number - 1u - m.last_live;
                out.contact = m.contact;
                out.hit = m.hit;
            }
        }
    }
    for (int p = 0; p < 2; ++p) {
        /* (a contact during this fighter's move: the advantage counted from now until both are free) */
        if (move[p].on && contact[1 - p]) {
            move[p].contact = true;
            move[p].hit = move[p].hit || hit[1 - p];
            advantage[p] = Advantage{true, 0u, 0u};
        }
        Advantage &a = advantage[p];
        if (!a.waiting) continue;
        if (!a.attacker_free && free_now[p]) a.attacker_free = frame_number;
        if (!a.defender_free && free_now[1 - p]) a.defender_free = frame_number;
        if (a.attacker_free && a.defender_free) {
            a.waiting = false;
            shown[p].advantage_known = true;
            shown[p].advantage = static_cast<int>(a.defender_free) - static_cast<int>(a.attacker_free);
        }
    }
    const bool doing = cell[0] != SC2_METER_FREE || cell[1] != SC2_METER_FREE;
    if (doing && meter_idle >= kMeterIdle) {
        meter[0].clear();
        meter[1].clear();
    }
    meter_idle = doing ? 0u : meter_idle + 1u;
    if (doing || meter_idle <= kMeterTail) {
        for (int p = 0; p < 2; ++p) {
            meter[p].push_back(cell[p]);
            if (meter[p].size() > SC2_PANEL_METER) meter[p].erase(meter[p].begin());
        }
    }

    Endless(training.endless, frame_number);

    /* The cheats: health kept full, or emptied by any hit */
    for (int p = 0; p < 2; ++p) {
        const uint32_t record = Record(p);
        const float health = LoadFloat(record + kHealth), max = LoadFloat(record + kHealthMax);
        if (max != 240.0f || !(health > 0.0f)) continue;
        if (training.no_damage[p] && health < max) {
            StoreFloat(record + kHealth, max);
            health_before[p] = max;
        } else if (training.one_hit && hit[p]) {
            StoreFloat(record + kHealth, 0.0f);
            health_before[p] = 0.0f;
        }
    }
    /* The controls the game had this frame: the display, and the recording */
    Sides();
    for (int p = 0; p < 2; ++p) {
        const bool mirrored = (p == 0) != player1_left;
        Held now{Direction(seen[p], mirrored), Buttons(seen[p]), 1};
        std::vector<Held> &rows = history[p];
        if (!rows.empty() && rows.front().direction == now.direction && rows.front().buttons == now.buttons)
            ++rows.front().frames;
        else
            rows.insert(rows.begin(), now);
        if (rows.size() > SC2_PANEL_INPUT_ROWS) rows.pop_back();
    }
    if (training.dummy != dummy_before) {
        if (training.dummy == Dummy::Record) recording.clear();
        replay_at = 0;
        dummy_before = training.dummy;
    }
    /* (Practice's dummy is its own: played by controller 2 while recording or replaying, then its own again) */
    uint32_t *control = recomp_memory_u32(Record(1) + kControl);
    if (training.dummy != Dummy::Off && *control == kPracticeDummy) {
        *control = kController;
        practice_dummy = true;
    } else if (training.dummy == Dummy::Off && practice_dummy) {
        if (*control == kController) *control = kPracticeDummy;
        practice_dummy = false;
    }
    if (training.dummy == Dummy::Record && recording.size() < kRecordFrames) recording.push_back(seen[1]);
    if (training.dummy == Dummy::Replay && !recording.empty()) replay_at = (replay_at + 1u) % recording.size();
}

void TrainingHitBoxes()
{
    shape_count = 0;
    if (!TrainingState().hit_boxes || !InPractice() || LoadFloat(Record(0) + kHealthMax) != 240.0f) return;
    float eye[3], focus[3];
    std::memcpy(eye, recomp_memory(kCameraEye, 12u), 12u);
    std::memcpy(focus, recomp_memory(kCameraFocus, 12u), 12u);
    /* (pixels a unit at a unit's depth, across and down, from the projection the view is drawn with: across is the
       height's times the picture's shape, 1.1 in 4:3, less in the game's own 16:9) */
    const float focal_x = std::fabs(LoadFloat(kProjectionX)), focal = std::fabs(LoadFloat(kProjectionY));
    /* The camera's axes: forward, right (level), up */
    float z[3] = {focus[0] - eye[0], focus[1] - eye[1], focus[2] - eye[2]};
    const float zl = std::sqrt(z[0] * z[0] + z[1] * z[1] + z[2] * z[2]);
    const float xl = std::sqrt(z[0] * z[0] + z[2] * z[2]);
    if (!(zl > 1e-4f) || !(xl > 1e-4f) || !(focal > 0.0f) || !(focal_x > 0.0f)) return;
    for (float &v : z) v /= zl;
    const float x[3] = {z[2] / (xl / zl), 0.0f, -z[0] / (xl / zl)};
    const float y[3] = {z[1] * x[2] - z[2] * x[1], z[2] * x[0] - z[0] * x[2], z[0] * x[1] - z[1] * x[0]};
    /* A point of the world in the picture; false behind the camera */
    const auto project = [&](const float w[3], float &px, float &py, float &depth) {
        const float d[3] = {w[0] - eye[0], w[1] - eye[1], w[2] - eye[2]};
        depth = d[0] * z[0] + d[1] * z[1] + d[2] * z[2];
        if (depth < 0.05f) return false;
        px = 320.0f - focal_x * (d[0] * x[0] + d[1] * x[1] + d[2] * x[2]) / depth;
        py = 240.0f - focal * (d[0] * y[0] + d[1] * y[1] + d[2] * y[2]) / depth;
        return std::isfinite(px) && std::isfinite(py);
    };
    for (int p = 0; p < 2; ++p) {
        const uint32_t record = Record(p);
        const bool live = *recomp_memory(record + kAttackLive, 1u) != 0u;
        uint32_t shape = *recomp_memory_u32(record + kShapes);
        const uint32_t count = std::min<uint32_t>(*recomp_memory_u32(record + kShapeCount), 48u);
        for (uint32_t n = 0; n < count && shape != 0u && shape_count < SC2_PANEL_SHAPES; ++n) {
            const uint8_t kind = *recomp_memory(shape + 0x16u, 1u);
            Sc2PanelShape out{};
            float a[3], b[3], depth;
            if (kind == 0u) {
                std::memcpy(a, recomp_memory(shape + 0x30u, 12u), 12u);
                const float radius = LoadFloat(shape + 0x50u);
                if (radius < 1.0f && project(a, out.x0, out.y0, depth)) {
                    out.x1 = out.x0;
                    out.y1 = out.y0;
                    out.radius = focal * radius / depth;
                    out.kind = radius >= 0.75f ? SC2_SHAPE_PUSH : SC2_SHAPE_HURT;
                    shapes[shape_count++] = out;
                }
            } else if (kind == 1u) {
                std::memcpy(a, recomp_memory(shape + 0x50u, 12u), 12u);
                std::memcpy(b, recomp_memory(shape + 0x60u, 12u), 12u);
                float depth_b;
                if (project(a, out.x0, out.y0, depth) && project(b, out.x1, out.y1, depth_b)) {
                    out.kind = live ? SC2_SHAPE_ATTACK_LIVE : SC2_SHAPE_ATTACK;
                    shapes[shape_count++] = out;
                }
            }
            shape = *recomp_memory_u32(shape + 0x18u);
        }
    }
}

bool TrainingUnlockAll()
{
    if (sc2_net_live) return false;
    std::memset(recomp_memory(kItems, kItemCount), kItemHad, kItemCount);
    /* The save keeps the items packed, four to a byte (0x3BB1F0: 300 items in 75 bytes, the first in the low
       bits), and loading unpacks them over the table (0x565B0). The game's own unlock (0x57680) packs the table
       again after each change (0x564C0); this does the same, or the next save would write the old unlocks (#5). */
    uint8_t *packed = recomp_memory(kItemsPacked, kItemsPackedBytes);
    const uint8_t *items = recomp_memory(kItems, 4u * kItemsPackedBytes);
    for (uint32_t n = 0; n < kItemsPackedBytes; ++n)
        packed[n] = uint8_t((items[4u * n] & 3u) | (items[4u * n + 1u] & 3u) << 2 | (items[4u * n + 2u] & 3u) << 4 |
                            (items[4u * n + 3u] & 3u) << 6);
    TrainingNote("Everything unlocked (kept when the game saves)");
    return true;
}

bool TrainingPad(uint32_t port, RecompInputGamepad *pad, bool sampled)
{
    if (port > 1u) return sampled;
    const Training &training = TrainingState();
    if (training.dummy != Dummy::Off && InFight()) {
        if (training.dummy == Dummy::Record) {
            if (port == 0u) {
                record_source = sampled ? *pad : RecompInputGamepad{};
                *pad = RecompInputGamepad{};
            } else {
                *pad = record_source;
                sampled = true;
            }
        } else if (port == 1u) {
            *pad = recording.empty() ? RecompInputGamepad{} : recording[replay_at % recording.size()];
            sampled = true;
        }
    }
    seen[port] = sampled ? *pad : RecompInputGamepad{};
    return sampled;
}

/* An online match's win streak: VS Battle's counts (player 1's wins at 0x39de58, its draws at 0x39de5a, player 2's
   wins at 0x3aecb8: +0x4c38 in each fighter's record), followed from the match's start; a win by the one who won the
   last adds to their streak, anything else starts it again. */
unsigned OnlineStreak(int &player)
{
    static bool live;
    static uint16_t wins[2], draws;
    static unsigned streak;
    static int holder = -1;
    const uint16_t now[2] = {*reinterpret_cast<const uint16_t *>(recomp_memory(0x39de58u, 2u)),
                             *reinterpret_cast<const uint16_t *>(recomp_memory(0x3aecb8u, 2u))};
    const uint16_t drawn = *reinterpret_cast<const uint16_t *>(recomp_memory(0x39de5au, 2u));
    if (!live) {
        live = true;
        wins[0] = now[0];
        wins[1] = now[1];
        draws = drawn;
    }
    for (int p = 0; p < 2; ++p) {
        if (now[p] == wins[p] + 1u) {
            streak = holder == p ? streak + 1u : 1u;
            holder = p;
        }
    }
    if (drawn != draws || (now[0] < wins[0] || now[1] < wins[1])) streak = 0u, holder = -1;
    wins[0] = now[0];
    wins[1] = now[1];
    draws = drawn;
    player = holder;
    return streak;
}

/* The FPS counter's text: frames shown (a call a frame) over the last half second */
void FpsText(char *out, size_t size)
{
    using Clock = std::chrono::steady_clock;
    static Clock::time_point since = Clock::now();
    static unsigned frames;
    static double rate;
    ++frames;
    const double seconds = std::chrono::duration<double>(Clock::now() - since).count();
    if (seconds >= 0.5) {
        rate = frames / seconds;
        frames = 0u;
        since = Clock::now();
    }
    if (TrainingState().fps_counter) {
        const double rendered = recomp_d3d_visual_fps();
        if (rendered > 0) std::snprintf(out, size, "%.0f FPS/%.0f sim", rendered, rate);
        else std::snprintf(out, size, "%.0f FPS", rate);
    }
}

void ApplyTraining(uint32_t)
{
    const Training &training = TrainingState();
    char fps[sizeof Sc2Panel{}.fps] = "";
    FpsText(fps, sizeof fps);
    if (sc2_net_live) {
        /* (an online match has none of training: nothing written that the other game would not write; only the win
           streak, read from VS Battle's counts, which both games have alike) */
        Sc2Panel online{};
        std::memcpy(online.fps, fps, sizeof fps);
        int player = -1;
        const unsigned streak = OnlineStreak(player);
        if (streak >= 2u && player >= 0)
            std::snprintf(online.readout[player][online.readout_rows[player]++], SC2_PANEL_TEXT, "STREAK %u", streak);
        sc2_panel_publish(&online);
        return;
    }
    static float last[2];
    static uint32_t still[2];
    for (uint32_t player = 0; player < 2u; ++player) {
        const uint32_t health = Record(static_cast<int>(player)) + kHealth;
        const float now = LoadFloat(health), max = LoadFloat(Record(static_cast<int>(player)) + kHealthMax);
        /* (only while a fight's records look like one: a full bar of 240) */
        if (!training.health_refill || max != 240.0f || !(now >= 0.0f) || now >= max) {
            still[player] = 0u;
            last[player] = now;
            continue;
        }
        still[player] = now == last[player] ? still[player] + 1u : 0u;
        last[player] = now;
        if (still[player] >= kRefillFrames && now > 0.0f) {
            StoreFloat(health, max);
            still[player] = 0u;
        }
    }
    if (training.infinite_time) {
        const uint32_t length = *recomp_memory_u32(kRoundLength);
        uint32_t *seconds = recomp_memory_u32(kTimerSeconds);
        if (length > 0u && length <= 99u && *seconds > 0u && *seconds < length) *seconds = length;
    }

    /* The panels */
    Sc2Panel panel{};
    std::memcpy(panel.fps, fps, sizeof fps);
    if (note_frames != 0u) {
        --note_frames;
        std::snprintf(panel.note, sizeof panel.note, "%s", note);
    }
    /* (once the fighters' records are filled in: while the fight loads they are not) */
    if (InPractice() && LoadFloat(Record(0) + kHealthMax) == 240.0f && LoadFloat(Record(1) + kHealthMax) == 240.0f) {
        for (int p = 0; p < 2; ++p) {
            if (training.combo_readout) {
                const Combo &c = combo[p];
                unsigned &rows = panel.readout_rows[p];
                std::snprintf(panel.readout[p][rows++], SC2_PANEL_TEXT, "P%d  %u HIT%s  %.0f", p + 1, c.hits,
                    c.hits == 1u ? "" : "S", c.damage);
                std::snprintf(panel.readout[p][rows++], SC2_PANEL_TEXT, "Last hit %.1f", c.last);
                std::snprintf(panel.readout[p][rows++], SC2_PANEL_TEXT, "Best %.0f", c.best);
            }
            if (training.frame_data && shown[p].valid) {
                const Shown &f = shown[p];
                unsigned &rows = panel.readout_rows[p];
                std::snprintf(panel.readout[p][rows++], SC2_PANEL_TEXT, "Startup %u  Active %u%s  Recovery %u", f.startup,
                    f.active, f.contact ? "*" : "", f.recovery);
                if (!f.contact) std::snprintf(panel.readout[p][rows++], SC2_PANEL_TEXT, "Missed");
                else if (f.advantage_known)
                    std::snprintf(panel.readout[p][rows++], SC2_PANEL_TEXT, "On %s %+d", f.hit ? "hit" : "guard", f.advantage);
                else std::snprintf(panel.readout[p][rows++], SC2_PANEL_TEXT, "On %s ...", f.hit ? "hit" : "guard");
            }
            if (training.input_display) {
                for (const Held &held : history[p]) {
                    char buttons[16] = "";
                    static const char *const kNames[] = {"A", "B", "K", "G", "Bk", "Wh", "T"};
                    for (int b = 0; b < 7; ++b) {
                        if (!(held.buttons >> b & 1u)) continue;
                        if (*buttons) std::strncat(buttons, "+", sizeof buttons - std::strlen(buttons) - 1u);
                        std::strncat(buttons, kNames[b], sizeof buttons - std::strlen(buttons) - 1u);
                    }
                    std::snprintf(panel.input[p][panel.input_rows[p]++], SC2_PANEL_TEXT, "%3u  %u  %s",
                        std::min<uint32_t>(held.frames, 999u), held.direction, buttons);
                }
            }
        }
    }
    /* (the dummy, in any fight: Practice and VS Battle) */
    if (InFight() && training.dummy == Dummy::Record) {
        std::snprintf(panel.readout[1][panel.readout_rows[1]++], SC2_PANEL_TEXT, "RECORDING %.1f s", recording.size() / 60.0);
    } else if (InFight() && training.dummy == Dummy::Replay) {
        std::snprintf(panel.readout[1][panel.readout_rows[1]++], SC2_PANEL_TEXT,
            recording.empty() ? "REPLAY: nothing recorded" : "REPLAY %.1f s", recording.size() / 60.0);
    }
    if (training.frame_data && InPractice() && !meter[0].empty()) {
        panel.meter_length = static_cast<unsigned>(meter[0].size());
        for (int p = 0; p < 2; ++p) std::memcpy(panel.meter[p], meter[p].data(), meter[p].size());
    }
    if (InFight()) {
        panel.shape_count = shape_count;
        std::memcpy(panel.shapes, shapes, sizeof shapes[0] * shape_count);
    }
    sc2_panel_publish(&panel);
}

} // namespace sc2

extern "C" void sc2_training_hit_boxes(void) { sc2::TrainingHitBoxes(); }
