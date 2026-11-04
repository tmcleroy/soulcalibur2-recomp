// SPDX-License-Identifier: GPL-3.0-or-later
// Read-only capture at SC2's native skinning boundary. Opt-in, bounded to eight
// poses. No guest memory/register writes and no gameplay work is skipped.
#include "sc2_scene_probe.h"
#include "d3d_scene_snapshot.h"
#include "d3d_presenter.h"
#include "d3d_visual_interpolation.h"
#include "d3d_vblank.h"
extern "C" {
#include "kernel_abi.h"
}
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
#include <map>
#include <unordered_map>

namespace {
struct RegisteredPose {
    std::shared_ptr<const Sc2SkinPose> pose;
    std::vector<float> output;
};
std::map<uint32_t, RegisteredPose> poses;
std::unordered_map<uint32_t, std::shared_ptr<const Sc2SkinMesh>> meshes;
unsigned pose_generation;
bool nativeEnabled() {
    static const bool enabled = [] {
        const char *s = std::getenv("RECOMP_SCENE_NATIVE");
        if (s) return std::strcmp(s,"1") == 0;
        const char *fps = std::getenv("RECOMP_RENDER_FPS");
        return fps && *fps && std::strcmp(fps,"off") != 0 && std::strcmp(fps,"0") != 0;
    }();
    return enabled;
}
}

void sc2_scene_attach(RecompD3dPresenterDrawCommand *draw, uint32_t position_address) {
    if (!nativeEnabled() || pose_generation != recomp_d3d_visual_generation() || draw->program_count || !draw->vertex_count) return;
    const uint32_t offset = position_address & 0x3ffffffu;
    auto found = poses.upper_bound(offset);
    if (found == poses.begin()) return;
    --found;
    const auto &pose = found->second.pose;
    if ((offset-found->first)%32) return;
    const uint32_t first = (offset-found->first)/32;
    if (uint64_t(first)+draw->vertex_count > pose->mesh->starts.size()) return;
    // A guest address is only a candidate: the game can modify or reuse the
    // skinning output before drawing. Native replay must match the actual draw.
    if (!sc2_visual::matchesSkinOutput(*draw, found->second.output, first)) {
        static unsigned reports = 0;
        static const bool trace = std::getenv("RECOMP_SCENE_CAPTURE") != nullptr;
        if (trace && reports++ < 8)
            std::fprintf(stderr, "SC2 scene: changed skin output model=%08x first=%u; using drawn geometry\n", pose->mesh->model, first);
        return;
    }
    draw->scene_pose = pose.get();
    draw->scene_first_vertex = first;
}

void sc2_scene_probe_skin(uint32_t model) {
    static const char *path = std::getenv("RECOMP_SCENE_CAPTURE");
    if (!path && !nativeEnabled()) return;
    static unsigned calls = 0;
    static const unsigned at = [] { const char *s = std::getenv("RECOMP_SCENE_CAPTURE_AT"); return s ? std::strtoul(s, nullptr, 10) : 1000ul; }();
    ++calls;
    const bool capture = path && calls >= at && calls < at+8;
    if (!capture && !nativeEnabled()) return;
    if (pose_generation != recomp_d3d_visual_generation() || poses.size() > 128 || meshes.size() > 128) {
        poses.clear(); meshes.clear(); pose_generation = recomp_d3d_visual_generation();
    }
    constexpr uint32_t ram = 64u*1024*1024;
    const auto valid = [](uint32_t a, uint64_t n) {
        const uint32_t window = a & ~(ram-1), offset = a & (ram-1);
        return (window == 0 || window == 0x80000000u || window == 0xf0000000u) &&
            offset >= 0x10000 && uint64_t(offset) + n <= ram;
    };
    if (!valid(model, 28)) return;
    uint32_t h[7]; recomp_guest_load(h, model, sizeof h);
    const uint32_t slot = *recomp_memory_u32(0x38c4b4);
    const uint32_t palette = *recomp_memory_u32(0x38c668);
    if (slot < 2) poses.erase(h[5+slot]&0x3ffffffu);
    if (capture) std::fprintf(stderr, "SC2 scene probe: call=%u model=%08x counts=%u,%u,%u,%u source=%08x buffers=%08x,%08x slot=%u palette=%08x\n",
        calls, model, h[0], h[1], h[2], h[3], h[4], h[5], h[6], slot, palette);
    if (capture) std::fflush(stderr);
    const uint64_t count = uint64_t(h[0]) + h[1] + h[2] + h[3];
    if (!count || count > 65536 || slot > 1 || !valid(h[5+slot], count*32)) return;
    const auto ready = meshes.find(model);
    if (!capture && ready != meshes.end()) {
        const auto &mesh = ready->second;
        const size_t bytes = mesh->influences.size()*sizeof(Sc2SkinInfluence);
        if (mesh->source == h[4] && std::memcmp(mesh->classes,h,16) == 0 && valid(h[4],bytes) &&
            std::memcmp(mesh->influences.data(),recomp_memory(h[4],bytes),bytes) == 0 &&
            valid(palette, uint64_t(mesh->bones)*64)) {
            auto pose = std::make_shared<Sc2SkinPose>(); pose->mesh = mesh;
            pose->matrices.resize(mesh->bones*16);
            recomp_guest_load(pose->matrices.data(),palette,pose->matrices.size()*4);
            for (float f : pose->matrices) if (!std::isfinite(f)) return;
            RegisteredPose entry; entry.pose = pose; entry.output.resize(count*8);
            recomp_guest_load(entry.output.data(), h[5+slot], entry.output.size()*4);
            poses[h[5+slot]&0x3ffffffu] = std::move(entry);
            return;
        }
    }
    using Influence = Sc2SkinInfluence;
    std::vector<Influence> source;
    std::vector<uint32_t> starts, lengths;
    uint32_t cursor = h[4], extra = 0, max_bone = 0;
    for (uint32_t v = 0; v < count; ++v) {
        const uint32_t n = v < h[0] ? 1 : v < uint64_t(h[0])+h[1] ? 2 :
            v < uint64_t(h[0])+h[1]+h[2] ? 3 : 4+extra;
        if (n > 32 || !valid(cursor, uint64_t(n)*32)) return;
        starts.push_back(static_cast<uint32_t>(source.size())); lengths.push_back(n);
        for (unsigned j = 0; j < n; ++j) {
            Influence inf; recomp_guest_load(&inf, cursor, sizeof inf); cursor += sizeof inf;
            for (float f : inf.position) if (!std::isfinite(f)) return;
            for (float f : inf.normal) if (!std::isfinite(f)) return;
            max_bone = std::max(max_bone, inf.bone & 255u);
            source.push_back(inf);
        }
        if (n >= 4) extra += source.back().bone >> 8;
    }
    if (!valid(palette, uint64_t(max_bone+1)*64)) return;
    std::vector<float> matrices((max_bone+1)*16), output(count*8);
    recomp_guest_load(matrices.data(), palette, matrices.size()*4);
    for (float f : matrices) if (!std::isfinite(f)) return;
    recomp_guest_load(output.data(), h[5+slot], output.size()*4);
    double position_error = 0, normal_error = 0;
    const auto cached = meshes.find(model);
    const bool known_mesh = cached != meshes.end() && cached->second->source == h[4] &&
        cached->second->starts == starts && cached->second->lengths == lengths &&
        cached->second->influences.size() == source.size() &&
        std::memcmp(cached->second->influences.data(), source.data(), source.size()*sizeof(Influence)) == 0;
    for (uint32_t v = 0; (!known_mesh || capture) && v < count; ++v) {
        float p[3]{}, normal[3]{};
        for (uint32_t j = 0; j < lengths[v]; ++j) {
            const auto &inf = source[starts[v]+j];
            const float *m = matrices.data() + (inf.bone & 255u)*16;
            for (unsigned k = 0; k < 3; ++k) {
                p[k] += ((inf.position[0]*m[k] + inf.position[1]*m[4+k]) + inf.position[2]*m[8+k]) + inf.position[3]*m[12+k];
                normal[k] += (inf.normal[0]*m[k] + inf.normal[1]*m[4+k]) + inf.normal[2]*m[8+k];
            }
        }
        for (unsigned k = 0; k < 3; ++k) {
            if (!std::isfinite(p[k]) || !std::isfinite(normal[k]) ||
                !std::isfinite(output[v*8+k]) || !std::isfinite(output[v*8+4+k])) return;
            position_error = std::max(position_error, double(std::abs(p[k]-output[v*8+k])));
            normal_error = std::max(normal_error, double(std::abs(normal[k]-output[v*8+4+k])));
        }
    }
    if (nativeEnabled() && position_error <= 0.001 && normal_error <= 0.001) {
        auto &mesh = meshes[model];
        if (!mesh || mesh->source != h[4] || mesh->starts != starts || mesh->lengths != lengths ||
            mesh->influences.size() != source.size() ||
            std::memcmp(mesh->influences.data(), source.data(), source.size()*sizeof(Influence))) {
            auto next = std::make_shared<Sc2SkinMesh>();
            next->model = model; next->source = h[4];
            std::copy(h,h+4,next->classes); next->bones = max_bone+1;
            next->influences = source; next->starts = starts; next->lengths = lengths;
            mesh = next;
        }
        auto pose = std::make_shared<Sc2SkinPose>();
        pose->mesh = mesh; pose->matrices = matrices;
        poses[h[5+slot]&0x3ffffffu] = {pose, output};
    }
    if (!capture) return;
    const auto name = std::string(path) + "." + std::to_string(calls) + ".bin";
    FILE *file = std::fopen(name.c_str(), "wb");
    if (!file) return;
    const uint32_t header[]{0x53433253, 1, model, static_cast<uint32_t>(count),
        static_cast<uint32_t>(source.size()), max_bone+1, h[4], palette, h[5+slot]};
    bool ok = std::fwrite(header, sizeof header, 1, file) == 1;
    ok &= std::fwrite(starts.data(), starts.size()*4, 1, file) == 1;
    ok &= std::fwrite(lengths.data(), lengths.size()*4, 1, file) == 1;
    ok &= std::fwrite(source.data(), source.size()*sizeof(Influence), 1, file) == 1;
    ok &= std::fwrite(matrices.data(), matrices.size()*4, 1, file) == 1;
    ok &= std::fwrite(output.data(), output.size()*4, 1, file) == 1;
    ok &= std::fclose(file) == 0;
    std::fprintf(stderr, "SC2 scene capture: saved=%d model=%08x vertices=%llu bones=%u position_error=%.9g normal_error=%.9g\n",
        ok, model, (unsigned long long)count, max_bone+1, position_error, normal_error);
    std::fflush(stderr);
}
