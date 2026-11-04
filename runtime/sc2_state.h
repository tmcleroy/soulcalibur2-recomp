// SPDX-License-Identifier: GPL-3.0-or-later
// Save states: the game as it is at the start of a frame (0x32630, the frame routine's first call, on the game's
// main thread), to be put back there later. The free camera's freeze puts the same frame back every frame; online
// play's rollback goes back to the frame a late control changed (sc2_rollback.h).
//
// A state is all of guest RAM, the main thread's registers and FPU, the host's side of what the game's run depends
// on (sc2_hooks.h, Sc2HostState), and the host's records that the guest's refer to: the kernel's allocations and the
// DirectSound device and buffers. Putting one back leaves the other guest threads' own memory (their
// objects, stacks, TLS) and the kernel's page alone: they are run until each waits before a state is taken or put
// back (recomp_kernel_drain_others), so they wait where they waited, and only the main thread's frame goes back.
#pragma once

#include <cstdint>
#include <vector>

extern "C" {
#include "runtime.h"
#include "sc2_hooks.h"
}

namespace sc2 {

struct SaveState {
    std::vector<std::uint8_t> ram;
    RecompRegisters registers{};
    RecompFpuContext fpu{};
    Sc2HostState host{};
    // (the host's records the guest's own refer to: its allocations, the sound device and buffers)
    std::vector<std::uint8_t> kernel_memory, sound_device, sound_buffers;
    bool valid = false;
};

void SaveFrame(SaveState &state);
void LoadFrame(const SaveState &state, bool reset_visual = true);

// The in-game menu's save states (sc2_frame.cpp): nine slots, kept while the game runs. Asked for between frames,
// done at the start of the next. A state loads only in the scene it was saved in (the same fight, the same menu):
// the game's other threads (the loader, the sound) are not put back with it.
constexpr int kStateSlots = 9;
void AskSaveState(int slot); // 1-9
void AskLoadState(int slot);
bool StateSaved(int slot);

} // namespace sc2
