// SPDX-License-Identifier: GPL-3.0-or-later
#include "d3d_visual_interpolation.h"
#include "d3d_presenter_capture.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
#include <limits>

int main() {
    // Production timestamps move with snapshots, and reset on slot reuse.
    D3dCapturePacket timed;
    timed.produced_ns = 1234;
    auto timed_moved = std::move(timed);
    assert(timed_moved.produced_ns == 1234);
    timed_moved.clear(); assert(timed_moved.produced_ns == 0);
    struct Vertex { float xyz[3], normal[3], uv[2]; };
    Vertex a{{0, 0, 0}, {0, 1, 0}, {0, 0}}, b{{10, 4, 2}, {0, 1, 0}, {0, 0}};
    RecompD3dPresenterDrawCommand before{}, after{}, out{};
    before.fvf = after.fvf = 0x112;
    before.vertex_count = after.vertex_count = 1;
    before.vertex_stride = after.vertex_stride = sizeof(Vertex);
    before.has_transform = after.has_transform = true;
    before.vertex_bytes = &a; after.vertex_bytes = &b;
    // Guest output uses padded position/normal float4s, unlike gathered FVF.
    std::vector<float> skin{99,99,99,0, 0,0,0,0, 0,0,0,1, 0,1,0,0};
    assert(sc2_visual::matchesSkinOutput(before, skin, 1));
    assert(!sc2_visual::matchesSkinOutput(before, skin, 0));
    assert(!sc2_visual::matchesSkinOutput(before, skin, 2));
    assert(!sc2_visual::matchesSkinOutput(before, skin, UINT32_MAX));
    // Reused output buffers and post-skinning edits must reject stale poses.
    a.xyz[0] = 2; assert(!sc2_visual::matchesSkinOutput(before, skin, 1)); a.xyz[0] = 0;
    a.normal[1] = -1; assert(!sc2_visual::matchesSkinOutput(before, skin, 1)); a.normal[1] = 1;
    a.uv[0] = 7; assert(sc2_visual::matchesSkinOutput(before, skin, 1)); a.uv[0] = 0;
    assert(sc2_visual::sameAttributes(before, after, true));
    assert(!sc2_visual::sameAttributes(before, after, false));
    b.uv[0] = 3;
    assert(!sc2_visual::sameAttributes(before, after, true));
    b.uv[0] = 0;
    before.transform[12] = 10; after.transform[12] = 20;
    std::vector<unsigned char> vertices;
    assert(sc2_visual::interpolate(before, after, 0.5f, out, vertices));
    Vertex result;
    std::memcpy(&result, out.vertex_bytes, sizeof result);
    assert(result.xyz[0] == 5 && result.xyz[1] == 2 && result.xyz[2] == 1);
    assert(out.transform[12] == 15);
    assert(a.xyz[0] == 0 && b.xyz[0] == 10 && after.transform[12] == 20);
    assert(sc2_visual::interpolate(before, after, 0, out, vertices));
    std::memcpy(&result, out.vertex_bytes, sizeof result); assert(result.xyz[0] == 0);
    assert(sc2_visual::interpolate(before, after, 1, out, vertices));
    std::memcpy(&result, out.vertex_bytes, sizeof result); assert(result.xyz[0] == 10);
    b.uv[0] = 1; assert(!sc2_visual::interpolate(before, after, .5f, out, vertices)); b.uv[0] = 0;
    b.xyz[0] = 1000; assert(!sc2_visual::interpolate(before, after, .5f, out, vertices)); b.xyz[0] = 10;
    after.texture.data = 42; assert(!sc2_visual::interpolate(before, after, .5f, out, vertices)); after.texture.data = 0;
    assert(!sc2_visual::interpolate(before, after, std::numeric_limits<float>::quiet_NaN(), out, vertices));
    after.fvf = before.fvf = 0x104; // screen-space UI must never be interpolated
    assert(!sc2_visual::interpolate(before, after, .5f, out, vertices));
    before.fvf = after.fvf = 0x112;

    // Transparent seam pass: same vertices and directed triangles, new alpha
    // sort order. It must share the body's interpolation time without changing
    // the current frame's compositing order.
    {
        Vertex old[4]{a,a,a,a}, next[4]{b,b,b,b};
        const uint16_t indices0[]{0,1,2,1,3,2};
        const uint16_t indices1[]{1,3,2,0,1,2};
        const uint16_t reversed[]{1,2,3,0,1,2};
        const uint16_t duplicated[]{0,1,2,0,1,2};
        auto p=before,c=after;
        p.vertex_bytes=old;c.vertex_bytes=next;p.vertex_count=c.vertex_count=4;
        p.index_count=c.index_count=6;p.primitive_type=c.primitive_type=5;
        p.index_bytes=indices0;c.index_bytes=indices1;
        assert(sc2_visual::key(p)==sc2_visual::key(c));
        assert(sc2_visual::interpolate(p,c,.5f,out,vertices));
        assert(out.index_bytes==indices1);
        std::memcpy(&result,out.vertex_bytes,sizeof result);assert(result.xyz[0]==5);
        D3dCapturePacket previous,current;
        RecompD3dPresenterCommand pc{},cc{};pc.type=cc.type=RECOMP_D3D_PRESENTER_COMMAND_DRAW;
        pc.data.draw=p;cc.data.draw=c;assert(previous.add(pc)==RECOMP_D3D_PRESENTER_OK);
        assert(current.add(cc)==RECOMP_D3D_PRESENTER_OK);previous.seal();current.seal();
        std::vector<size_t> matches;std::unordered_map<uint64_t,size_t> prior;
        std::unordered_map<uint64_t,uint32_t> counts;
        sc2_visual::matchSnapshots(previous,current,matches,prior,counts);
        assert(matches[0]==0);
        c.index_bytes=reversed;assert(!sc2_visual::prepare(p,c));
        c.index_bytes=duplicated;assert(!sc2_visual::prepare(p,c));
        c.index_bytes=indices1;p.primitive_type=c.primitive_type=6;
        assert(!sc2_visual::prepare(p,c)); // Reordering strips changes connectivity.
    }

    // Optional native acceleration can appear/disappear without replacing the
    // mesh. Both handoff directions must match and use owned vertex endpoints.
    {
        auto mesh=std::make_shared<Sc2SkinMesh>();mesh->model=123;mesh->source=456;
        auto pose=std::make_shared<Sc2SkinPose>();pose->mesh=mesh;
        for (bool entering : {false,true}) {
            RecompD3dPresenterCommand p{},c{};
            p.type=c.type=RECOMP_D3D_PRESENTER_COMMAND_DRAW;
            p.data.draw=before;c.data.draw=after;
            (entering?c:p).data.draw.scene_pose=pose.get();
            (entering?c:p).data.draw.scene_first_vertex=37;
            assert(sc2_visual::key(p.data.draw)==sc2_visual::key(c.data.draw));
            D3dCapturePacket old,now;assert(old.add(p)==RECOMP_D3D_PRESENTER_OK);
            assert(now.add(c)==RECOMP_D3D_PRESENTER_OK);old.seal();now.seal();
            std::vector<size_t> matches;std::unordered_map<uint64_t,size_t> prior;
            std::unordered_map<uint64_t,uint32_t> counts;
            sc2_visual::matchSnapshots(old,now,matches,prior,counts);assert(matches[0]==0);
            assert(sc2_visual::interpolate(old.command(0).data.draw,now.command(0).data.draw,.5f,out,vertices));
            std::memcpy(&result,out.vertex_bytes,sizeof result);assert(result.xyz[0]==5);
            // Optional pose metadata must not manufacture unique identities
            // for otherwise ambiguous moving copies of the same geometry.
            D3dCapturePacket copies;assert(copies.add(c)==RECOMP_D3D_PRESENTER_OK);
            c.data.draw.scene_pose=entering?nullptr:pose.get();
            assert(copies.add(c)==RECOMP_D3D_PRESENTER_OK);copies.seal();
            sc2_visual::matchSnapshots(old,copies,matches,prior,counts);
            assert(matches[0]==SIZE_MAX && matches[1]==SIZE_MAX);
        }
    }

    // Snapshot general shader textures and palettes, not just the old legacy texture slots.
    unsigned char texture[4]{1,2,3,4}, palette[4]{5,6,7,8};
    RecompD3dPresenterCommand command{};
    command.type = RECOMP_D3D_PRESENTER_COMMAND_DRAW;
    command.data.draw = before;
    command.data.draw.pixel_program.kind = RECOMP_D3D_PIXEL_COMBINERS;
    command.data.draw.stage_textures[3].bytes = texture;
    command.data.draw.stage_textures[3].byte_count = 4;
    command.data.draw.stage_textures[3].palette = palette;
    command.data.draw.stage_textures[3].palette_byte_count = 4;
    D3dCapturePacket packet;
    assert(packet.add(command) == RECOMP_D3D_PRESENTER_OK);
    command.type = RECOMP_D3D_PRESENTER_COMMAND_COPY_RECT;
    assert(packet.add(command) == RECOMP_D3D_PRESENTER_OK);
    packet.seal();
    texture[0] = palette[0] = 99; a.xyz[0] = 99;
    D3dCapturePacket moved(std::move(packet));
    const auto &draw = moved.command(0).data.draw;
    assert(static_cast<const unsigned char *>(draw.stage_textures[3].bytes)[0] == 1);
    assert(static_cast<const unsigned char *>(draw.stage_textures[3].palette)[0] == 5);
    std::memcpy(&result, draw.vertex_bytes, sizeof result); assert(result.xyz[0] == 0);
    assert(moved.command(1).type == RECOMP_D3D_PRESENTER_COMMAND_COPY_RECT);
    // Scene poses and their immutable meshes must survive game-side replacement
    // and two capture stages, then release when both packets are recycled.
    auto mesh = std::make_shared<Sc2SkinMesh>();
    mesh->model = 123; mesh->source = 456;
    auto pose = std::make_shared<Sc2SkinPose>(); pose->mesh = mesh;
    pose->matrices.resize(16, 1);
    std::weak_ptr<const Sc2SkinPose> weak = pose;
    command = {};
    command.type = RECOMP_D3D_PRESENTER_COMMAND_DRAW;
    command.data.draw = before;
    command.data.draw.scene_pose = pose.get();
    D3dCapturePacket stage1, stage2;
    assert(stage1.add(command) == RECOMP_D3D_PRESENTER_OK);
    stage1.seal();
    assert(stage2.add(stage1.command(0)) == RECOMP_D3D_PRESENTER_OK);
    stage2.seal();
    pose.reset(); mesh.reset(); stage1.clear();
    assert(!weak.expired());
    const auto *held_pose = static_cast<const Sc2SkinPose *>(stage2.command(0).data.draw.scene_pose);
    assert(held_pose->mesh->model == 123 && held_pose->matrices[0] == 1);
    stage2.clear(); assert(weak.expired());
    {
        Vertex left{{1,2,3},{0,1,0},{0,0}}, right{{11,2,3},{0,1,0},{0,0}};
        uint16_t index=0;
        const auto add = [&](D3dCapturePacket &packet, const Vertex &vertex, float camera, uint32_t source=0) {
            RecompD3dPresenterCommand c{}; c.type=RECOMP_D3D_PRESENTER_COMMAND_DRAW;
            auto &d=c.data.draw; d.fvf=0x112; d.has_transform=true;
            d.vertex_bytes=&vertex; d.vertex_stride=sizeof(Vertex); d.vertex_count=1;
            d.index_bytes=&index; d.index_count=1; d.primitive_type=5;
            d.transform[0]=d.transform[5]=d.transform[10]=d.transform[15]=1;
            d.transform[12]=camera;
            d.visual_vertex_source=source;
            assert(packet.add(c)==RECOMP_D3D_PRESENTER_OK);
        };
        D3dCapturePacket old,next,one,twins,changed;
        add(old,left,0); add(old,right,0); old.seal();
        add(next,right,4); add(next,left,4); next.seal(); // reordered by visibility
        std::vector<size_t> matched;
        std::unordered_map<uint64_t,size_t> previous_draws;
        std::unordered_map<uint64_t,uint32_t> current_counts;
        const auto match = [&](const auto &p,const auto &c) {
            sc2_visual::matchSnapshots(p,c,matched,previous_draws,current_counts);
            const auto expected = matched;
            D3dCapturePacket empty;
            std::vector<uint64_t> old_keys, new_keys;
            sc2_visual::matchSnapshots(empty,p,matched,previous_draws,current_counts,&old_keys);
            sc2_visual::matchSnapshots(p,c,matched,previous_draws,current_counts,&new_keys,&old_keys);
            assert(matched == expected);
            for (size_t i=0; i<c.count(); ++i)
                assert(new_keys[i] == sc2_visual::key(c.command(i).data.draw));
        };
        match(old,next); assert(matched[0]==1 && matched[1]==0);
        std::vector<unsigned char> interpolated;
        RecompD3dPresenterDrawCommand middle{};
        assert(sc2_visual::interpolate(old.command(matched[0]).data.draw,next.command(0).data.draw,.5f,middle,interpolated));
        assert(middle.transform[12]==2 && static_cast<const Vertex *>(middle.vertex_bytes)->xyz[0]==11);
        add(one,left,4); one.seal(); // one instance was culled
        match(old,one); assert(matched[0]==0);
        match(one,next); assert(matched[0]==SIZE_MAX && matched[1]==0);
        add(twins,left,0); add(twins,left,5); twins.seal();
        match(twins,twins); assert(matched[0]==SIZE_MAX && matched[1]==SIZE_MAX);
        D3dCapturePacket instances, reordered_instances;
        add(instances,left,0,100); add(instances,left,5,200); instances.seal();
        add(reordered_instances,left,6,200); add(reordered_instances,left,1,100); reordered_instances.seal();
        match(instances,reordered_instances); assert(matched[0]==1 && matched[1]==0);
        right.xyz[0]+=1;
        add(changed,left,4); add(changed,right,4); changed.seal();
        match(old,changed); assert(matched[0]==0 && matched[1]==SIZE_MAX);
    }
    before={}; after={};
    before.fvf=after.fvf=0x112;
    before.vertex_bytes=after.vertex_bytes=&a;
    before.vertex_count=after.vertex_count=1;
    before.vertex_stride=after.vertex_stride=sizeof(Vertex);
    before.has_transform=after.has_transform=true;
    before.pixel_program.kind=after.pixel_program.kind=RECOMP_D3D_PIXEL_FIXED_FUNCTION;
    after.pixel_program.constants[4][0]=1;
    before.pixel_program.constants[RECOMP_D3D_PIXEL_CONSTANT_UV][2]=.75f;
    after.pixel_program.constants[RECOMP_D3D_PIXEL_CONSTANT_UV][2]=0.0f;
    after.pixel_program.constants[RECOMP_D3D_PIXEL_CONSTANT_UV+1][2]=.25f;
    assert(sc2_visual::interpolate(before,after,.5f,out,vertices));
    assert(out.pixel_program.constants[4][0]==.5f);
    assert(out.pixel_program.constants[RECOMP_D3D_PIXEL_CONSTANT_UV][2]==0.0f);
    assert(out.pixel_program.constants[RECOMP_D3D_PIXEL_CONSTANT_UV+1][2]==.25f);
    after.pixel_program.stage_count=2;
    assert(!sc2_visual::prepare(before,after));
    after.pixel_program.stage_count=0;
    before.directional.enabled=after.directional.enabled=true;
    after.directional.view[0]=10;
    before.general_fog[0][0]=after.general_fog[0][0]=1;
    after.general_fog[1][2]=4;
    assert(sc2_visual::interpolate(before,after,.5f,out,vertices));
    assert(out.directional.view[0]==5 && out.general_fog[1][2]==2);
    std::puts("PASS: interpolation, snapshot ownership, skin-output validation, repeated-mesh matching/reorder/culling/ambiguity");
}
