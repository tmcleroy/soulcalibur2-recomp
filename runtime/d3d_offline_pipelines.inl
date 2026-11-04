// SPDX-License-Identifier: GPL-3.0-or-later
/* Static SC2 render-state families. See tools/build_graphics_inventory.py and
   docs/graphics-preparation.md. No guest draws are executed during this pass. */
bool prepareOfflinePipelines(RecompD3dPresenter *p, const WarmShader &r,
    const ShaderPair &pair, const std::function<bool()> &progress)
{
    struct Blend { bool enabled; RecompD3dBlendFactor src,dst; RecompD3dBlendOp op; };
    const Blend blends[] = {
        {false,RECOMP_D3D_BLEND_ONE,RECOMP_D3D_BLEND_ZERO,RECOMP_D3D_BLEND_OP_ADD},
        {true,RECOMP_D3D_BLEND_SRC_ALPHA,RECOMP_D3D_BLEND_INV_SRC_ALPHA,RECOMP_D3D_BLEND_OP_ADD},
        {true,RECOMP_D3D_BLEND_SRC_ALPHA,RECOMP_D3D_BLEND_ONE,RECOMP_D3D_BLEND_OP_ADD},
        {true,RECOMP_D3D_BLEND_ONE,RECOMP_D3D_BLEND_ONE,RECOMP_D3D_BLEND_OP_REVERSE_SUBTRACT},
        {true,RECOMP_D3D_BLEND_ZERO,RECOMP_D3D_BLEND_SRC_COLOR,RECOMP_D3D_BLEND_OP_ADD},
        {true,RECOMP_D3D_BLEND_ONE,RECOMP_D3D_BLEND_SRC_COLOR,RECOMP_D3D_BLEND_OP_ADD},
        {true,RECOMP_D3D_BLEND_DST_COLOR,RECOMP_D3D_BLEND_SRC_COLOR,RECOMP_D3D_BLEND_OP_ADD},
        {true,RECOMP_D3D_BLEND_SRC_ALPHA,RECOMP_D3D_BLEND_ONE,RECOMP_D3D_BLEND_OP_REVERSE_SUBTRACT},
        {true,RECOMP_D3D_BLEND_DST_ALPHA,RECOMP_D3D_BLEND_INV_DST_ALPHA,RECOMP_D3D_BLEND_OP_ADD},
    };
    const bool model=r.pixel.kind==RECOMP_D3D_PIXEL_COMBINERS;
    RecompD3dPresenterDrawCommand draw{};
    draw.depth.depth_func=RECOMP_D3D_COMPARE_GREATER_EQUAL;
    draw.blend.color_write_mask=15;
    PipelineState state{&draw,pair,r.fvf,pair.layout.stride,SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        RECOMP_D3D_CULL_NONE,SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM,p->depth_format,p->msaa,0,false,false};
    for (unsigned padded=0;padded<(model?2u:1u);++padded) {
        state.stride=pair.layout.stride+(padded?4:0);
        for (auto primitive:{SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP}) {
            state.primitive=primitive;
            for (unsigned cull=0;cull<(model?2u:1u);++cull) {
                state.cull=static_cast<RecompD3dCullMode>(cull);
                for (unsigned depth=0;depth<(model?2u:3u);++depth) {
                    draw.depth.depth_test_enable=depth!=2;
                    draw.depth.depth_write_enable=depth==0;
                    for (const auto &b:blends) {
                        draw.blend.blend_enable=b.enabled;draw.blend.src_factor=b.src;
                        draw.blend.dst_factor=b.dst;draw.blend.op=b.op;
                        if (!lookupPipeline(p,state)) return false;
                    }
                }
            }
        }
    }
    if (!model) {
        // Full-screen copies can target a color texture without a depth surface.
        state.stride=pair.layout.stride;state.cull=RECOMP_D3D_CULL_NONE;
        state.primitive=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        state.depth_format=SDL_GPU_TEXTUREFORMAT_INVALID;state.samples=SDL_GPU_SAMPLECOUNT_1;
        draw.depth.depth_test_enable=draw.depth.depth_write_enable=false;
        draw.blend.blend_enable=false;
        if (!lookupPipeline(p,state)) return false;
        // Shadow receiver overlay: the volume pass left a non-zero stencil.
        state.depth_format=p->depth_format;state.samples=p->msaa;
        draw.depth.depth_test_enable=true;
        draw.depth.stencil_enable=true;draw.depth.stencil_func=RECOMP_D3D_COMPARE_NOT_EQUAL;
        draw.depth.stencil_read_mask=draw.depth.stencil_write_mask=255;
        draw.blend.blend_enable=true;draw.blend.src_factor=RECOMP_D3D_BLEND_SRC_ALPHA;
        draw.blend.dst_factor=RECOMP_D3D_BLEND_INV_SRC_ALPHA;draw.blend.op=RECOMP_D3D_BLEND_OP_ADD;
        if (!lookupPipeline(p,state)) return false;
        if (r.fvf==2) {
            // Stencil shadow volume front/back passes (0x2ea90 family).
            draw.blend.color_write_mask=0;draw.depth.depth_func=RECOMP_D3D_COMPARE_LESS_EQUAL;
            state.primitive=SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP;
            for (auto dst:{RECOMP_D3D_BLEND_ONE,RECOMP_D3D_BLEND_INV_SRC_ALPHA}) {
                draw.blend.dst_factor=dst;
                state.cull=RECOMP_D3D_CULL_CLOCKWISE;
                draw.depth.stencil_func=RECOMP_D3D_COMPARE_GREATER;
                draw.depth.stencil_pass=RECOMP_D3D_STENCIL_REPLACE;
                if (!lookupPipeline(p,state)) return false;
                state.cull=RECOMP_D3D_CULL_COUNTER_CLOCKWISE;
                draw.depth.stencil_func=RECOMP_D3D_COMPARE_EQUAL;
                draw.depth.stencil_pass=RECOMP_D3D_STENCIL_ZERO;
                if (!lookupPipeline(p,state)) return false;
            }
        }
    }
    return progress();
}
