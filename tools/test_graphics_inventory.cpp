// SPDX-License-Identifier: GPL-3.0-or-later
#include "sc2_graphics_inventory.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>

int main(int argc, char **argv)
{
    if (argc == 3) {
        // Optional private check against a loaded, authenticated executable image.
        std::ifstream file(argv[1], std::ios::binary);
        if (!file) return 1;
        std::vector<uint8_t> memory((std::istreambuf_iterator<char>(file)), {});
        return sc2::PrepareGraphicsInventory(memory, argv[2]) ? 0 : 1;
    }
    assert(argc == 1);
    std::vector<uint8_t> memory(0x200000);
    const auto put = [&](size_t at, uint32_t value) { std::memcpy(memory.data() + at, &value, 4); };
    for (size_t at = 0x193f60; at < 0x194410; at += 0xf0) put(at + 53*4, 0x11101);
    for (unsigned i = 0; i < 4; ++i) { put(0x19567c+i*20, 0x100); put(0x195680+i*20, 0x200); }
    put(0x100, 0x12078); put(0x110, 1); put(0x200, 0x40320000); put(0x204, 0xffffffff);
    const auto first = sc2::GraphicsInventory(memory), second = sc2::GraphicsInventory(memory);
    assert(!first.empty() && first.size() == second.size());
    assert(std::memcmp(first.data(), second.data(), first.size()*sizeof(first[0])) == 0);
    for (const auto &r : first) assert(r.visual <= 2 && r.count <= 136);
    const auto rejected = [&](const std::vector<uint8_t> &input) {
        try { sc2::GraphicsInventory(input); } catch (const std::exception &) { return true; }
        return false;
    };
    assert(rejected({}));
    put(0x100, 0xffff2078); assert(rejected(memory)); put(0x100, 0x12078);
    put(0x200, 0); assert(rejected(memory)); put(0x200, 0x40320000);
    put(0x110, 0); assert(rejected(memory));
    std::puts("PASS: native inventory determinism, unsupported inputs and bounds");
}
