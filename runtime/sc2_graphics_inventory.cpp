// SPDX-License-Identifier: GPL-3.0-or-later
#include "sc2_graphics_inventory.h"
#include "d3d_warmup_catalog.h"
#include <array>
#include <cstring>
#include <filesystem>
#include <set>
#include <stdexcept>

namespace sc2 {
std::vector<RecompWarmShader> GraphicsInventory(const std::vector<uint8_t> &memory)
{
    const auto word = [&](size_t address) {
        if (address > memory.size() || memory.size() - address < 4) throw std::runtime_error("inventory address");
        uint32_t value; std::memcpy(&value, memory.data() + address, 4); return value;
    };
    using Bytes = std::array<uint8_t, sizeof(RecompWarmShader)>;
    std::set<Bytes> unique;
    const auto add = [&](uint32_t fvf, const RecompD3dPixelProgram &pixel, const std::vector<uint32_t> &program) {
        RecompWarmShader recipe;
        std::memset(&recipe, 0, sizeof recipe);
        recipe.fvf = fvf; recipe.pixel = pixel; recipe.count = uint32_t(program.size() / 4);
        if (!program.empty()) std::memcpy(recipe.program, program.data(), program.size() * 4);
        Bytes bytes{}; std::memcpy(bytes.data(), &recipe, sizeof recipe); unique.insert(bytes);
    };
    // Material selection in the supported executable: five pixel definitions.
    for (uint32_t address = 0x193f60; address < 0x194410; address += 0xf0) {
        std::array<uint32_t, 60> w{};
        for (size_t i = 0; i < w.size(); ++i) w[i] = word(address + i * 4);
        if ((w[53] != 0x11101 && w[53] != 0x11102) || (w[54] != 0 && w[54] != 1 && w[54] != 0x21))
            throw std::runtime_error("inventory material definition");
        std::vector<unsigned> active;
        for (unsigned i = 0; i < 4; ++i) if (((w[54] >> (5 * i)) & 31) == 1) active.push_back(i);
        for (unsigned bits = 0; bits < (1u << (2 * active.size())); ++bits) {
            RecompD3dPixelProgram pixel{}; pixel.kind = RECOMP_D3D_PIXEL_COMBINERS;
            auto &c = pixel.combiners; c.control = w[53];
            for (unsigned i = 0; i < 8; ++i) {
                c.color_input[i] = w[34+i]; c.color_output[i] = w[45+i];
                c.alpha_input[i] = w[i]; c.alpha_output[i] = w[26+i];
            }
            c.final0 = w[8]; c.final1 = w[9]; c.texture_modes = w[54];
            c.input_texture = w[56]; c.dot_mapping = w[55]; c.compare_mode = w[42];
            for (size_t j = 0; j < active.size(); ++j) {
                auto &stage = pixel.stages[active[j]]; stage.sampled = true;
                stage.alpha_kill = ((bits >> j) & 1) != 0;
                stage.transform_uv = ((bits >> (j + active.size())) & 1) != 0;
            }
            add(0x152, pixel, {});
        }
    }
    struct Layout { uint32_t fvf; std::vector<uint32_t> program; };
    std::vector<Layout> layouts;
    for (uint32_t fvf : {2u, 0x42u, 0x44u, 0x104u, 0x112u, 0x142u, 0x144u, 0x152u}) layouts.push_back({fvf, {}});
    // Effect vertex declarations and programs; unsupported paths remain explicit.
    for (unsigned index = 0; index < 4; ++index) {
        const auto code = word(0x19567c + index * 20), decl = word(0x195680 + index * 20);
        const uint32_t header = word(code), count = header >> 16;
        if ((header & 0xffff) != 0x2078 || count == 0 || count > 136) throw std::runtime_error("inventory vertex header");
        uint32_t mapping[16]; for (unsigned i = 0; i < 16; ++i) mapping[i] = i;
        const uint32_t slots[][3] = {{0,0x32,2},{2,0x32,16},{3,0x40,64},{9,0x22,256}};
        unsigned used = 0; Layout layout{}; bool ended = false;
        for (unsigned i = 0; i < 16; ++i) {
            const auto token = word(decl + i * 4);
            if (token == 0xffffffff) { ended = true; break; }
            if (token == 0x20000000) continue;
            if (token >> 29 != 2) throw std::runtime_error("inventory declaration token");
            bool found = false;
            for (unsigned slot = 0; slot < 4; ++slot) if (!(used & (1u << slot)) && slots[slot][1] == ((token >> 16) & 255)) {
                used |= 1u << slot; mapping[token & 15] = slots[slot][0]; layout.fvf |= slots[slot][2]; found = true; break;
            }
            if (!found) throw std::runtime_error("inventory declaration input");
        }
        if (!ended) throw std::runtime_error("inventory declaration terminator");
        for (unsigned i = 0; i < count * 4; ++i) layout.program.push_back(word(code + 4 + i * 4));
        for (unsigned i = 0; i < count; ++i) {
            auto &a = layout.program[i*4+1]; a = (a & ~(15u << 9)) | (mapping[(a >> 9) & 15] << 9);
        }
        if (!(layout.program.back() & 1)) throw std::runtime_error("inventory program terminator");
        // This legacy program reads an undeclared input and cannot be translated.
        if (layout.fvf == 2) continue;
        layouts.push_back(std::move(layout));
    }
    for (const auto &layout : layouts) for (unsigned spec = 0; spec < 2; ++spec) {
        RecompD3dPixelProgram pixel{}; pixel.kind = RECOMP_D3D_PIXEL_FIXED_FUNCTION; pixel.specular_add = spec != 0;
        add(layout.fvf, pixel, layout.program);
        if (!(layout.fvf & 0xf00)) continue;
        for (unsigned op : {4u,5u}) for (unsigned alpha = 0; alpha < 2; ++alpha)
            for (unsigned kill = 0; kill < 2; ++kill) for (unsigned transform = 0; transform < 2; ++transform) {
                if (transform && (!layout.program.empty() || (layout.fvf & 14) == 4)) continue;
                auto p = pixel; p.stage_count = 1; auto &stage = p.stages[0];
                const uint32_t fields[] = {op,0,2,0,op,0,2,alpha,1,0};
                static_assert(offsetof(RecompD3dPixelStage, sampled) == sizeof fields);
                std::memcpy(&stage, fields, sizeof fields);
                stage.sampled = true; stage.alpha_kill = kill != 0; stage.transform_uv = transform != 0;
                add(layout.fvf, p, layout.program);
            }
    }
    std::vector<RecompWarmShader> result;
    for (const auto &bytes : unique) { RecompWarmShader r; std::memcpy(&r, bytes.data(), sizeof r); result.push_back(r); }
    return result;
}

bool PrepareGraphicsInventory(const std::vector<uint8_t> &memory, const std::string &folder)
{
    try {
        const auto records = GraphicsInventory(memory);
        const auto path = (std::filesystem::u8path(folder) / "offline-shaders-v1.bin").u8string();
        const auto previous = recomp_warmup::read<RecompWarmShader>(path);
        if (previous.size() == records.size() && std::memcmp(previous.data(), records.data(), records.size() * sizeof(RecompWarmShader)) == 0)
            return true;
        if (!recomp_warmup::rewrite(path, records)) throw std::runtime_error("inventory write failed");
        std::fprintf(stderr, "Graphics inventory: prepared %zu shader recipes from the authenticated executable\n", records.size());
        return true;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "Graphics inventory unavailable: %s; ordinary compilation remains available\n", error.what());
        return false;
    }
}
}
