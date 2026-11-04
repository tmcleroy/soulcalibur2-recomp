// SPDX-License-Identifier: GPL-3.0-or-later
// The start of each frame (0x32630, the frame routine's first call, on the game's main thread; sc2_hooks.c): where
// save states are taken and put back (sc2_state.h).
//
// The menu's save states (sc2_state.h), and training (sc2_training.h) once each frame that plays.
//
// The free camera's freeze: the first held frame takes a state, and every held frame after puts it back, so the
// game plays the same frame again and again, drawing it from wherever the camera is. A step lets one frame through
// and holds the next.
#include "sc2_state.h"
#include "sc2_menu.h"
#include "sc2_training.h"

#include <cstdio>

extern "C" {
#include "audio_output.h"
#include "d3d_vblank.h"
#include "kernel_abi.h"
#include "scene_freeze_adapter.h"
#include "sc2_hooks.h"
}

namespace {

sc2::SaveState held;
bool sound_paused;

sc2::SaveState slots[sc2::kStateSlots];
std::uint32_t slot_scene[sc2::kStateSlots]; // (sc2_scene_switches when saved: the scene it was saved in)
int asked_slot;                             // (1-9; 0: none)
bool asked_save;

// A save or load asked for: false when a load could not be
bool DoAsked()
{
    const int slot = asked_slot;
    asked_slot = 0;
    char note[96];
    sc2::SaveState &state = slots[slot - 1];
    if (asked_save) {
        recomp_kernel_drain_others();
        sc2::SaveFrame(state);
        slot_scene[slot - 1] = sc2_scene_switches;
        std::snprintf(note, sizeof note, "State %d saved", slot);
        sc2::TrainingNote(note);
        return true;
    }
    if (!state.valid) {
        std::snprintf(note, sizeof note, "State %d is empty", slot);
        sc2::TrainingNote(note);
        return false;
    }
    if (slot_scene[slot - 1] != sc2_scene_switches) {
        std::snprintf(note, sizeof note, "State %d loads only where it was saved (that fight, that menu)", slot);
        sc2::TrainingNote(note);
        return false;
    }
    recomp_kernel_drain_others();
    recomp_d3d_visual_reset();
    sc2::LoadFrame(state);
    std::snprintf(note, sizeof note, "State %d loaded", slot);
    sc2::TrainingNote(note);
    return true;
}

} // namespace

namespace sc2 {

void AskSaveState(int slot)
{
    if (sc2_net_live || slot < 1 || slot > kStateSlots) return;
    asked_slot = slot;
    asked_save = true;
}

void AskLoadState(int slot)
{
    if (sc2_net_live || slot < 1 || slot > kStateSlots) return;
    asked_slot = slot;
    asked_save = false;
}

bool StateSaved(int slot) { return slot >= 1 && slot <= kStateSlots && slots[slot - 1].valid; }

} // namespace sc2

extern "C" void (*sc2_net_frame)(void) = nullptr;

#ifdef __ANDROID__
extern "C" void sc2_android_frame(void); // (sc2_android.cpp: the log's speed line)
#endif

extern "C" void sc2_frame_begins(void)
{
    sc2_frame_thread = recomp_kernel_current_thread_id();
    static unsigned last_scene;
    if (last_scene != sc2_scene_switches || sc2_net_live || sc2::GameSpeed() != 1.0) {
        recomp_d3d_visual_reset();
        last_scene = sc2_scene_switches;
    }
#ifdef __ANDROID__
    sc2_android_frame();
#endif
    if (sc2_net_frame != nullptr) {
        sc2_net_frame();
        return;
    }
    const int action = sc2_freeze_frame_action();
    if (action != SC2_FREEZE_NONE && !sound_paused) {
        recomp_audio_output_set_paused(1);
        sound_paused = true;
    } else if (action == SC2_FREEZE_NONE && sound_paused) {
        recomp_audio_output_set_paused(0);
        sound_paused = false;
    }
    if (asked_slot != 0) {
        // (a load while paused: the pause holds the loaded frame from here)
        if (DoAsked() && !asked_save) held.valid = false;
    }
    if (sc2::TrainingRestart()) held.valid = false;
    if (action == SC2_FREEZE_NONE) {
        held.valid = false;
        /* (faster than full speed: some frames played without being shown or paced, as rollback's are) */
        static double shown;
        const double rate = sc2::GameSpeed();
        shown += rate > 1.0 ? 1.0 / rate : 1.0;
        sc2_fast_forward = shown < 1.0;
        if (shown >= 1.0) shown -= 1.0;
        sc2::TrainingFrame();
        return;
    }
    sc2_fast_forward = 0;
    recomp_kernel_drain_others();
    if (!held.valid) {
        recomp_d3d_visual_reset();
        sc2::SaveFrame(held);
        return;
    }
    // Replaying the same held frame is continuous presentation (the photo
    // camera can still move). Keep immutable meshes/history across restores.
    sc2::LoadFrame(held, action == SC2_FREEZE_STEP);
    if (action == SC2_FREEZE_STEP) {
        held.valid = false; // (this frame goes on; the next is held)
        sc2::TrainingFrame();
    }
}
