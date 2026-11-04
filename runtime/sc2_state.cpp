// SPDX-License-Identifier: GPL-3.0-or-later
#include "sc2_state.h"

extern "C" {
#include "kernel_abi.h"
#include "d3d_vblank.h"
#include "xbox_memory_layout.h"
}

extern "C" {
size_t sc2_dsound_device_state_size(void);
void sc2_dsound_device_state_save(void *out);
void sc2_dsound_device_state_load(const void *in);
size_t sc2_dsound_buffers_state_size(void);
void sc2_dsound_buffers_state_save(void *out);
void sc2_dsound_buffers_state_load(const void *in);
}

#include <algorithm>
#include <cstring>
#include <utility>

namespace sc2 {

void SaveFrame(SaveState &state)
{
    state.ram.resize(RECOMP_XBOX_RAM_SIZE);
    std::memcpy(state.ram.data(), recomp_memory(0u, RECOMP_XBOX_RAM_SIZE), RECOMP_XBOX_RAM_SIZE);
    state.registers = recomp_runtime.registers;
    recomp_fpu_context_save(&state.fpu);
    sc2_host_state_get(&state.host);
    state.kernel_memory.resize(recomp_kernel_memory_state_size());
    recomp_kernel_memory_state_save(state.kernel_memory.data());
    state.sound_device.resize(sc2_dsound_device_state_size());
    sc2_dsound_device_state_save(state.sound_device.data());
    state.sound_buffers.resize(sc2_dsound_buffers_state_size());
    sc2_dsound_buffers_state_save(state.sound_buffers.data());
    state.valid = true;
}

void LoadFrame(const SaveState &state, bool reset_visual)
{
    if (!state.valid) return;
    if (reset_visual) recomp_d3d_visual_reset();
    // (left alone: the other threads' memory, and the kernel's page and above)
    std::vector<std::pair<std::uint32_t, std::uint32_t>> kept;
    std::uint32_t base = 0u, size = 0u;
    bool current = false;
    for (unsigned i = 0u; recomp_kernel_thread_region(i, &base, &size, &current); ++i)
        if (size != 0u && !current) kept.emplace_back(base, base + size);
    kept.emplace_back(XBOX_KERNEL_DATA_BASE, RECOMP_XBOX_RAM_SIZE);
    std::sort(kept.begin(), kept.end());
    std::uint8_t *ram = recomp_memory(0u, RECOMP_XBOX_RAM_SIZE);
    std::uint32_t at = 0u;
    for (const auto &range : kept) {
        if (range.first > at) std::memcpy(ram + at, state.ram.data() + at, range.first - at);
        at = std::max(at, range.second);
    }
    if (at < RECOMP_XBOX_RAM_SIZE) std::memcpy(ram + at, state.ram.data() + at, RECOMP_XBOX_RAM_SIZE - at);
    recomp_runtime.registers = state.registers;
    recomp_fpu_context_restore(&state.fpu);
    sc2_host_state_set(&state.host);
    recomp_kernel_memory_state_load(state.kernel_memory.data());
    sc2_dsound_device_state_load(state.sound_device.data());
    sc2_dsound_buffers_state_load(state.sound_buffers.data());
}

} // namespace sc2
