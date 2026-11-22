/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "metal_internal.h"
#include "AreaTex.h"
#include "SearchTex.h"

enum { AF_MTL_UPSCALED_EXTRACT_SET = 14, AF_MTL_UPSCALED_COMPOSITE_SET = 15 };

typedef struct mtl_post_frame {
    id<MTLTexture> hdr;
    id<MTLTexture> motion;
    id<MTLTexture> blur[2];
    id<MTLTexture> upscaled;
    id<MTLTexture> gi;
} mtl_post_frame;

struct af_mtl_post {
    id<MTLDevice> device;
    uint32_t out_w, out_h, half_w, half_h, render_w, render_h;
    uint32_t count;
    uint64_t allocation_bytes;
    unsigned allocation_attempts;
    id<MTLRenderPipelineState> blur_pipeline;
    id<MTLRenderPipelineState> output_pipeline;
    id<MTLRenderPipelineState> gi_pipeline;
    id<MTLDepthStencilState> depth_none;
    id<MTLSamplerState> sampler;
    mtl_post_frame *frames;
    id<MTLTexture> display;
    id<MTLTexture> history[2];
    id<MTLTexture> gi_history[2];
    bool initialized, history_valid;
    bool gi, gi_initialized, gi_history_valid, motion;
    unsigned history_index, gi_history_index;
    float adapted_exposure;
    af_mtl_texture *area_lut, *search_lut;
};

static id<MTLTexture> alloc_post_tex(af_mtl_post *p, uint32_t w, uint32_t h, MTLTextureUsage usage) {
    const char *fault = getenv("AFTERLIGHT_TEST_POST_FAIL_AT");
    if (fault && strtoul(fault, NULL, 10) == ++p->allocation_attempts) {
        af_log("info", "Injected post allocation failure for cleanup regression.");
        return nil;
    }
    MTLTextureDescriptor *desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float
                                                                                    width:w
                                                                                   height:h
                                                                                mipmapped:NO];
    desc.storageMode = MTLStorageModePrivate;
    desc.usage = usage;
    id<MTLTexture> tex = [p->device newTextureWithDescriptor:desc];
    if (!tex)
        return nil;
    uint64_t bytes = (uint64_t)[tex allocatedSize];
    p->allocation_bytes += bytes ? bytes : (uint64_t)w * h * 8u;
    return tex;
}

static id<MTLTexture> alloc_rt_tex(af_mtl_post *p, uint32_t w, uint32_t h) {
    return alloc_post_tex(p, w, h, MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead);
}

void af_mtl_post_destroy(af_mtl_post *p) {
    if (!p)
        return;
    for (uint32_t i = 0; p->frames && i < p->count; ++i) {
        AF_MTL_RELEASE(p->frames[i].hdr);
        AF_MTL_RELEASE(p->frames[i].motion);
        AF_MTL_RELEASE(p->frames[i].blur[0]);
        AF_MTL_RELEASE(p->frames[i].blur[1]);
        AF_MTL_RELEASE(p->frames[i].upscaled);
        AF_MTL_RELEASE(p->frames[i].gi);
    }
    AF_MTL_RELEASE(p->display);
    AF_MTL_RELEASE(p->history[0]);
    AF_MTL_RELEASE(p->history[1]);
    AF_MTL_RELEASE(p->gi_history[0]);
    AF_MTL_RELEASE(p->gi_history[1]);
    af_mtl_texture_destroy(p->area_lut);
    af_mtl_texture_destroy(p->search_lut);
    AF_MTL_RELEASE(p->blur_pipeline);
    AF_MTL_RELEASE(p->output_pipeline);
    AF_MTL_RELEASE(p->gi_pipeline);
    AF_MTL_RELEASE(p->depth_none);
    AF_MTL_RELEASE(p->sampler);
    free(p->frames);
    free(p);
}

uint64_t af_mtl_post_allocation_bytes(const af_mtl_post *p) {
    return p ? p->allocation_bytes : 0;
}

id<MTLTexture> af_mtl_post_scene_texture(const af_mtl_post *p, uint32_t frame) {
    return (p && p->frames) ? p->frames[frame % p->count].hdr : nil;
}

id<MTLTexture> af_mtl_post_motion_texture(const af_mtl_post *p, uint32_t frame) {
    return (p && p->frames) ? p->frames[frame % p->count].motion : nil;
}

id<MTLTexture> af_mtl_post_gi_texture(const af_mtl_post *p, uint32_t frame) {
    return (p && p->frames) ? p->frames[frame % p->count].gi : nil;
}

id<MTLTexture> af_mtl_post_upscaled_texture(const af_mtl_post *p, uint32_t frame) {
    return (p && p->frames) ? p->frames[frame % p->count].upscaled : nil;
}

void af_mtl_post_reset_temporal(af_mtl_post *p) {
    if (p) {
        af_mtl_post_invalidate_lighting_history(p, true);
        p->history_index = 0;
        p->gi_history_index = 0;
        p->adapted_exposure = 1.0f;
    }
}

void af_mtl_post_invalidate_lighting_history(af_mtl_post *p, bool gi) {
    if (p) {
        p->history_valid = false;
        if (gi)
            p->gi_history_valid = false;
    }
}

af_mtl_post *af_mtl_post_create(id<MTLDevice> device, id<MTLFunction> vert_fn, id<MTLFunction> post_fn,
                                id<MTLFunction> gi_fn, uint32_t out_w, uint32_t out_h, uint32_t render_w,
                                uint32_t render_h, uint32_t count, af_render_settings settings, bool motion, bool gi,
                                bool upscale) {
    if (!device || !vert_fn || !post_fn || !out_w || !out_h || !render_w || !render_h || !count)
        return NULL;
    motion = motion || gi;

    af_mtl_post *p = calloc(1, sizeof(*p));
    if (!p)
        return NULL;
    p->device = device;
    p->out_w = out_w;
    p->out_h = out_h;
    p->half_w = (out_w + 1u) / 2u;
    p->half_h = (out_h + 1u) / 2u;
    p->render_w = render_w;
    p->render_h = render_h;
    p->count = count;
    p->adapted_exposure = 1.0f;
    p->gi = gi;
    p->motion = motion;
    p->frames = calloc(count, sizeof(*p->frames));
    if (!p->frames)
        goto fail;

    @autoreleasepool {
        NSError *err = nil;

        MTLRenderPipelineDescriptor *pdesc = [[MTLRenderPipelineDescriptor alloc] init];
        pdesc.label = @"Afterlight Post HDR";
        pdesc.vertexFunction = vert_fn;
        pdesc.fragmentFunction = post_fn;
        pdesc.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA16Float;
        p->blur_pipeline = [device newRenderPipelineStateWithDescriptor:pdesc error:&err];
        if (!p->blur_pipeline) {
            [pdesc release];
            af_log("error", "Metal post blur pipeline failed: %s", err ? err.localizedDescription.UTF8String : "unknown");
            goto fail;
        }

        pdesc.label = @"Afterlight Post Output";
        pdesc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm_sRGB;
        p->output_pipeline = [device newRenderPipelineStateWithDescriptor:pdesc error:&err];
        [pdesc release];
        if (!p->output_pipeline) {
            af_log("error", "Metal post output pipeline failed: %s", err ? err.localizedDescription.UTF8String : "unknown");
            goto fail;
        }

        if (gi) {
            if (!gi_fn)
                goto fail;
            MTLRenderPipelineDescriptor *gdesc = [[MTLRenderPipelineDescriptor alloc] init];
            gdesc.label = @"Afterlight GI Denoise";
            gdesc.vertexFunction = vert_fn;
            gdesc.fragmentFunction = gi_fn;
            gdesc.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA16Float;
            gdesc.colorAttachments[0].blendingEnabled = NO;
            gdesc.colorAttachments[0].writeMask = MTLColorWriteMaskAll;
            gdesc.colorAttachments[1].pixelFormat = MTLPixelFormatRGBA16Float;
            gdesc.colorAttachments[1].blendingEnabled = YES;
            gdesc.colorAttachments[1].sourceRGBBlendFactor = MTLBlendFactorOne;
            gdesc.colorAttachments[1].destinationRGBBlendFactor = MTLBlendFactorOne;
            gdesc.colorAttachments[1].rgbBlendOperation = MTLBlendOperationAdd;
            gdesc.colorAttachments[1].sourceAlphaBlendFactor = MTLBlendFactorOne;
            gdesc.colorAttachments[1].destinationAlphaBlendFactor = MTLBlendFactorZero;
            gdesc.colorAttachments[1].alphaBlendOperation = MTLBlendOperationAdd;
            gdesc.colorAttachments[1].writeMask = MTLColorWriteMaskRed | MTLColorWriteMaskGreen | MTLColorWriteMaskBlue;
            p->gi_pipeline = [device newRenderPipelineStateWithDescriptor:gdesc error:&err];
            [gdesc release];
            if (!p->gi_pipeline) {
                af_log("error", "Metal GI denoise pipeline failed: %s",
                       err ? err.localizedDescription.UTF8String : "unknown");
                goto fail;
            }
        }

        MTLDepthStencilDescriptor *dsdesc = [[MTLDepthStencilDescriptor alloc] init];
        dsdesc.depthCompareFunction = MTLCompareFunctionAlways;
        dsdesc.depthWriteEnabled = NO;
        p->depth_none = [device newDepthStencilStateWithDescriptor:dsdesc];
        [dsdesc release];
        if (!p->depth_none)
            goto fail;

        MTLSamplerDescriptor *sdesc = [[MTLSamplerDescriptor alloc] init];
        sdesc.minFilter = MTLSamplerMinMagFilterLinear;
        sdesc.magFilter = MTLSamplerMinMagFilterLinear;
        sdesc.mipFilter = MTLSamplerMipFilterNotMipmapped;
        sdesc.sAddressMode = MTLSamplerAddressModeClampToEdge;
        sdesc.tAddressMode = MTLSamplerAddressModeClampToEdge;
        sdesc.rAddressMode = MTLSamplerAddressModeClampToEdge;
        p->sampler = [device newSamplerStateWithDescriptor:sdesc];
        [sdesc release];
        if (!p->sampler)
            goto fail;

        unsigned shared_count =
            settings.anti_aliasing >= AF_AA_SMAA ? 3u : settings.anti_aliasing != AF_AA_OFF ? 1u : 0u;
        if (shared_count >= 1u) {
            p->display = alloc_rt_tex(p, out_w, out_h);
            if (!p->display)
                goto fail;
        }
        if (shared_count >= 3u) {
            p->history[0] = alloc_rt_tex(p, out_w, out_h);
            p->history[1] = alloc_rt_tex(p, out_w, out_h);
            if (!p->history[0] || !p->history[1])
                goto fail;
        }
        if (gi) {
            p->gi_history[0] = alloc_rt_tex(p, render_w, render_h);
            p->gi_history[1] = alloc_rt_tex(p, render_w, render_h);
            if (!p->gi_history[0] || !p->gi_history[1])
                goto fail;
        }

        if (settings.anti_aliasing == AF_AA_SMAA) {
            unsigned char *lut = malloc(AREATEX_WIDTH * AREATEX_HEIGHT * 4u);
            if (!lut)
                goto fail;
            for (unsigned i = 0; i < AREATEX_WIDTH * AREATEX_HEIGHT; ++i) {
                lut[4 * i] = areaTexBytes[2 * i];
                lut[4 * i + 1] = areaTexBytes[2 * i + 1];
                lut[4 * i + 2] = 0;
                lut[4 * i + 3] = 255;
            }
            p->area_lut = af_mtl_texture_create(device, lut, AREATEX_WIDTH, AREATEX_HEIGHT, false, false, 1.0f);
            free(lut);
            if (!p->area_lut)
                goto fail;

            lut = malloc(SEARCHTEX_WIDTH * SEARCHTEX_HEIGHT * 4u);
            if (!lut)
                goto fail;
            for (unsigned i = 0; i < SEARCHTEX_WIDTH * SEARCHTEX_HEIGHT; ++i) {
                lut[4 * i] = searchTexBytes[i];
                lut[4 * i + 1] = lut[4 * i + 2] = 0;
                lut[4 * i + 3] = 255;
            }
            p->search_lut = af_mtl_texture_create(device, lut, SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT, false, false, 1.0f);
            free(lut);
            if (!p->search_lut)
                goto fail;

            p->allocation_bytes +=
                af_mtl_texture_info(p->area_lut).allocation_bytes + af_mtl_texture_info(p->search_lut).allocation_bytes;
        }

        for (uint32_t i = 0; i < count; ++i) {
            mtl_post_frame *f = &p->frames[i];
            f->hdr = alloc_rt_tex(p, render_w, render_h);
            if (!f->hdr)
                goto fail;
            if (motion) {
                f->motion = alloc_rt_tex(p, render_w, render_h);
                if (!f->motion)
                    goto fail;
            }
            if (gi) {
                f->gi = alloc_rt_tex(p, render_w, render_h);
                if (!f->gi)
                    goto fail;
            }
            if (upscale) {
                f->upscaled = alloc_post_tex(
                    p, out_w, out_h, MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite | MTLTextureUsageRenderTarget);
                if (!f->upscaled)
                    goto fail;
            }
            if (settings.bloom) {
                f->blur[0] = alloc_rt_tex(p, p->half_w, p->half_h);
                f->blur[1] = alloc_rt_tex(p, p->half_w, p->half_h);
                if (!f->blur[0] || !f->blur[1])
                    goto fail;
            }
        }
    }

    return p;

fail:
    af_mtl_post_destroy(p);
    return NULL;
}

static void clear_texture_zero(id<MTLCommandBuffer> cmd, id<MTLTexture> tex) {
    if (!tex)
        return;
    MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
    rp.colorAttachments[0].texture = tex;
    rp.colorAttachments[0].loadAction = MTLLoadActionClear;
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    rp.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 0.0);
    id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:rp];
    [enc endEncoding];
}

void af_mtl_post_denoise_gi(af_mtl_post *p, id<MTLCommandBuffer> cmd, uint32_t frame, af_render_settings settings,
                            af_rect area, af_vec2 previous_minus_current_jitter) {
    if (!p || !p->gi || !cmd)
        return;
    mtl_post_frame *f = &p->frames[frame % p->count];
    if (!p->gi_initialized) {
        clear_texture_zero(cmd, p->gi_history[0]);
        clear_texture_zero(cmd, p->gi_history[1]);
        p->gi_initialized = true;
        p->gi_history_valid = false;
    }

    unsigned parity = p->gi_history_index % 2u;
    float path_complexity =
        fmaxf(1.0f, (float)settings.rtgi_samples) * fmaxf(1.0f, (float)settings.rtgi_bounces);
    float temporal_blend = fminf(0.045f, fmaxf(0.025f, 0.08f / sqrtf(path_complexity)));
    float spatial_sigma = settings.rtgi_samples <= 1 ? 3.5f : 2.5f;
    struct {
        af_vec4 viewport, texel, params;
    } push = {.viewport = {area.x, area.y, area.x + area.width, area.y + area.height},
              .texel = {1.0f / (float)p->render_w, 1.0f / (float)p->render_h, temporal_blend,
                        p->gi_history_valid ? 1.0f : 0.0f},
              .params = {4.0f, spatial_sigma, previous_minus_current_jitter.x, previous_minus_current_jitter.y}};

    MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
    rp.colorAttachments[0].texture = p->gi_history[parity];
    rp.colorAttachments[0].loadAction = MTLLoadActionDontCare;
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    rp.colorAttachments[1].texture = f->hdr;
    rp.colorAttachments[1].loadAction = MTLLoadActionLoad;
    rp.colorAttachments[1].storeAction = MTLStoreActionStore;

    id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:rp];
    if (!enc)
        return;
    MTLViewport vp = {0.0, 0.0, (double)p->render_w, (double)p->render_h, 0.0, 1.0};
    MTLScissorRect sc = {0, 0, p->render_w, p->render_h};
    [enc setViewport:vp];
    [enc setScissorRect:sc];
    [enc setRenderPipelineState:p->gi_pipeline];
    [enc setDepthStencilState:p->depth_none];
    [enc setCullMode:MTLCullModeNone];
    [enc setFragmentBytes:&push length:sizeof(push) atIndex:AF_MTL_BUF_PUSH];
    [enc setFragmentTexture:f->gi atIndex:AF_MTL_POST_TEX_0];
    [enc setFragmentSamplerState:p->sampler atIndex:AF_MTL_POST_TEX_0];
    [enc setFragmentTexture:f->motion atIndex:AF_MTL_POST_TEX_1];
    [enc setFragmentSamplerState:p->sampler atIndex:AF_MTL_POST_TEX_1];
    [enc setFragmentTexture:p->gi_history[1u - parity] atIndex:AF_MTL_POST_TEX_2];
    [enc setFragmentSamplerState:p->sampler atIndex:AF_MTL_POST_TEX_2];
    [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    [enc endEncoding];

    p->gi_history_valid = true;
    ++p->gi_history_index;
}

static void resolve_post_set(const af_mtl_post *p, const mtl_post_frame *f, unsigned j, id<MTLTexture> *t0,
                             id<MTLTexture> *t1, id<MTLTexture> *t2, id<MTLSamplerState> *s0, id<MTLSamplerState> *s1,
                             id<MTLSamplerState> *s2) {
    id<MTLTexture> sources[16] = {f->hdr,        f->hdr,        f->blur[0],    f->blur[1],    f->hdr,       p->display,
                                  p->history[0], p->display,    f->hdr,        f->hdr,        p->history[0], p->history[1],
                                  p->history[0], p->history[1], f->upscaled,   f->upscaled};
    id<MTLTexture> src = (j < 16u && sources[j]) ? sources[j] : f->hdr;
    id<MTLTexture> img1 = nil;
    id<MTLSamplerState> smp1 = p->sampler;

    if (j == 6 && p->area_lut) {
        img1 = af_mtl_texture_handle(p->area_lut);
        smp1 = af_mtl_texture_sampler(p->area_lut);
    } else if (j == 0 || j == 10 || j == 11 || j == AF_MTL_UPSCALED_COMPOSITE_SET) {
        img1 = f->blur[0];
    } else if (j == 7 || j == 8) {
        img1 = p->history[1];
    } else if (j == 9) {
        img1 = p->history[0];
    } else {
        img1 = src;
    }
    if (!img1)
        img1 = f->hdr;

    id<MTLTexture> img2 = nil;
    id<MTLSamplerState> smp2 = p->sampler;
    if (p->motion && (j == 0 || j == 4 || j == 5 || j == 8 || j == 9 || j == 10 || j == 11 || j == 12 || j == 13 ||
                      j == AF_MTL_UPSCALED_COMPOSITE_SET)) {
        img2 = f->motion;
    } else if (p->search_lut) {
        img2 = af_mtl_texture_handle(p->search_lut);
        smp2 = af_mtl_texture_sampler(p->search_lut);
    } else {
        img2 = f->hdr;
    }
    if (!img2)
        img2 = f->hdr;

    *t0 = src;
    *t1 = img1;
    *t2 = img2;
    *s0 = p->sampler;
    *s1 = smp1;
    *s2 = smp2;
}

static id<MTLRenderCommandEncoder> draw_post_pass(af_mtl_post *p, id<MTLCommandBuffer> cmd, id<MTLTexture> target,
                                                  bool output, uint32_t w, uint32_t h, const mtl_post_frame *f,
                                                  unsigned set_index, const void *push) {
    MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
    rp.colorAttachments[0].texture = target;
    rp.colorAttachments[0].loadAction = MTLLoadActionClear;
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    rp.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 0.0);

    id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:rp];
    if (!enc)
        return nil;

    id<MTLTexture> t0, t1, t2;
    id<MTLSamplerState> s0, s1, s2;
    resolve_post_set(p, f, set_index, &t0, &t1, &t2, &s0, &s1, &s2);

    MTLViewport vp = {0.0, 0.0, (double)w, (double)h, 0.0, 1.0};
    MTLScissorRect sc = {0, 0, w, h};
    [enc setViewport:vp];
    [enc setScissorRect:sc];
    [enc setRenderPipelineState:output ? p->output_pipeline : p->blur_pipeline];
    [enc setDepthStencilState:p->depth_none];
    [enc setCullMode:MTLCullModeNone];
    [enc setFragmentBytes:push length:128 atIndex:AF_MTL_BUF_PUSH];
    [enc setFragmentTexture:t0 atIndex:AF_MTL_POST_TEX_0];
    [enc setFragmentSamplerState:s0 atIndex:AF_MTL_POST_TEX_0];
    [enc setFragmentTexture:t1 atIndex:AF_MTL_POST_TEX_1];
    [enc setFragmentSamplerState:s1 atIndex:AF_MTL_POST_TEX_1];
    [enc setFragmentTexture:t2 atIndex:AF_MTL_POST_TEX_2];
    [enc setFragmentSamplerState:s2 atIndex:AF_MTL_POST_TEX_2];
    [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];

    if (!output) {
        [enc endEncoding];
        return nil;
    }
    return enc;
}

id<MTLRenderCommandEncoder> af_mtl_post_draw(af_mtl_post *p, id<MTLCommandBuffer> cmd, id<MTLTexture> drawable_tex,
                                             uint32_t frame, af_render_settings settings, af_rect area, af_vec2 jitter,
                                             af_debug_view debug_view, bool upscaled, float time_seconds,
                                             float delta_time) {
    mtl_post_frame *f = &p->frames[frame % p->count];
    float target_exposure = exp2f(
        settings.auto_exposure ? fminf(fmaxf(settings.exposure, settings.auto_exposure_min), settings.auto_exposure_max)
                               : settings.exposure);
    if (p->adapted_exposure <= 0.0f || !isfinite(p->adapted_exposure))
        p->adapted_exposure = target_exposure;
    if (delta_time > 0.0f && settings.auto_exposure && settings.auto_exposure_speed > 0.0f) {
        float blend = 1.0f - expf(-settings.auto_exposure_speed * delta_time);
        p->adapted_exposure += (target_exposure - p->adapted_exposure) * blend;
    } else {
        p->adapted_exposure = target_exposure;
    }
    float sat = settings.saturation <= 0.0f && settings.contrast <= 0.0f && settings.color_filter.x <= 0.0f &&
                        settings.color_filter.y <= 0.0f && settings.color_filter.z <= 0.0f
                    ? 1.0f
                    : settings.saturation;
    float con = settings.contrast <= 0.0f ? 1.0f : settings.contrast;
    af_vec3 filter =
        (settings.color_filter.x <= 0.0f && settings.color_filter.y <= 0.0f && settings.color_filter.z <= 0.0f)
            ? (af_vec3){1.0f, 1.0f, 1.0f}
            : settings.color_filter;
    struct {
        af_vec4 settings, filter, viewport, aa, temporal, grading, lens, dof_grain;
    } push = {
        .settings = {p->adapted_exposure,
                     !settings.tone_mapping ? 0.0f
                                            : (settings.tone_mapper == AF_TONE_MAP_OFF ? (float)AF_TONE_MAP_REINHARD
                                                                                       : (float)settings.tone_mapper),
                     settings.bloom ? settings.bloom_strength : 0.0f, settings.bloom_threshold},
        .filter = {0, 0, 0, settings.bloom ? settings.bloom_radius : 0.0f},
        .viewport = {area.x, area.y, area.x + area.width, area.y + area.height},
        .aa = {(float)settings.anti_aliasing, settings.aa_sharpness, settings.taa_history_weight,
               p->history_valid ? 1.0f : 0.0f},
        .temporal = {jitter.x, jitter.y, time_seconds, settings.motion_blur ? settings.motion_blur_strength : 0.0f},
        .grading = {sat, con, filter.x, filter.y},
        .lens = {filter.z, settings.vignette ? settings.vignette_intensity : 0.0f, settings.vignette_smoothness,
                 settings.chromatic_aberration ? settings.chromatic_aberration_strength : 0.0f},
        .dof_grain = {settings.dof_focus_distance, settings.dof_focus_range,
                      settings.depth_of_field ? settings.dof_blur_radius : 0.0f,
                      settings.film_grain ? settings.film_grain_intensity : 0.0f}};

    bool bloom = settings.bloom && settings.bloom_strength > 0.0f;
    bool aa = settings.anti_aliasing != AF_AA_OFF;
    bool taa = settings.anti_aliasing == AF_AA_TAA;
    unsigned parity = p->history_index % 2u;

    if (taa) {
        if (!p->initialized) {
            clear_texture_zero(cmd, p->history[0]);
            clear_texture_zero(cmd, p->history[1]);
            p->initialized = true;
        }
        push.filter.z = 10.0f;
        draw_post_pass(p, cmd, p->history[parity], false, p->out_w, p->out_h, f, 8u + parity, &push);
        p->history_valid = true;
        ++p->history_index;
    } else {
        p->history_valid = false;
    }

    if (debug_view != AF_DEBUG_SHADED) {
        push.filter.z = (float)(10u + (unsigned)debug_view);
        return draw_post_pass(p, cmd, drawable_tex, true, p->out_w, p->out_h, f, 8u + parity, &push);
    }

    unsigned last = settings.anti_aliasing == AF_AA_SMAA ? 7u : aa ? 5u : 4u;
    id<MTLRenderCommandEncoder> out_enc = nil;
    for (unsigned stage = bloom ? 1u : 4u; stage <= last; ++stage) {
        bool output = (stage == last);
        uint32_t w = stage >= 4u ? p->out_w : p->half_w;
        uint32_t h = stage >= 4u ? p->out_h : p->half_h;
        unsigned mode = stage == 4u ? (aa ? 5u : 0u) : stage >= 5u ? (last == 7u ? stage + 2u : 6u) : stage;
        id<MTLTexture> target = output       ? drawable_tex
                                : stage == 4 ? p->display
                                : stage == 5 ? p->history[0]
                                : stage == 6 ? p->history[1]
                                             : f->blur[stage == 2 ? 1 : 0];
        push.filter.x = 1.0f / (float)(stage == 1u ? p->out_w : p->half_w);
        push.filter.y = 1.0f / (float)(stage == 1u ? p->out_h : p->half_h);
        push.filter.z = (float)mode;
        unsigned set = stage >= 5u ? stage : stage == 4u ? (bloom ? 0u : 4u) : stage;
        if (taa && stage == 1u)
            set = 12u + parity;
        if (taa && stage == 4u)
            set = bloom ? 10u + parity : 12u + parity;
        if (upscaled && stage == 1u)
            set = AF_MTL_UPSCALED_EXTRACT_SET;
        if (upscaled && stage == 4u)
            set = AF_MTL_UPSCALED_COMPOSITE_SET;
        id<MTLRenderCommandEncoder> enc = draw_post_pass(p, cmd, target, output, w, h, f, set, &push);
        if (output)
            out_enc = enc;
    }
    return out_enc;
}
