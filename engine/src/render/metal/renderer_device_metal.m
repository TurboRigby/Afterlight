/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "metal_internal.h"
#include "metal_shaders_lib.h"

static MTLVertexDescriptor *make_mesh_vertex_descriptor(void) {
    MTLVertexDescriptor *vdesc = [MTLVertexDescriptor vertexDescriptor];
    vdesc.attributes[0].format = MTLVertexFormatFloat3;
    vdesc.attributes[0].offset = offsetof(af_vertex, position);
    vdesc.attributes[0].bufferIndex = AF_MTL_VERTEX_BUFFER_INDEX;

    vdesc.attributes[1].format = MTLVertexFormatFloat4;
    vdesc.attributes[1].offset = offsetof(af_vertex, color);
    vdesc.attributes[1].bufferIndex = AF_MTL_VERTEX_BUFFER_INDEX;

    vdesc.attributes[2].format = MTLVertexFormatFloat3;
    vdesc.attributes[2].offset = offsetof(af_vertex, normal);
    vdesc.attributes[2].bufferIndex = AF_MTL_VERTEX_BUFFER_INDEX;

    vdesc.attributes[3].format = MTLVertexFormatFloat2;
    vdesc.attributes[3].offset = offsetof(af_vertex, uv);
    vdesc.attributes[3].bufferIndex = AF_MTL_VERTEX_BUFFER_INDEX;

    vdesc.attributes[4].format = MTLVertexFormatUShort4;
    vdesc.attributes[4].offset = offsetof(af_vertex, joints);
    vdesc.attributes[4].bufferIndex = AF_MTL_VERTEX_BUFFER_INDEX;

    vdesc.attributes[5].format = MTLVertexFormatFloat4;
    vdesc.attributes[5].offset = offsetof(af_vertex, weights);
    vdesc.attributes[5].bufferIndex = AF_MTL_VERTEX_BUFFER_INDEX;

    vdesc.layouts[AF_MTL_VERTEX_BUFFER_INDEX].stride = sizeof(af_vertex);
    vdesc.layouts[AF_MTL_VERTEX_BUFFER_INDEX].stepFunction = MTLVertexStepFunctionPerVertex;
    return vdesc;
}

static void configure_scene_attachments(MTLRenderPipelineDescriptor *desc, uint32_t config) {
    bool motion = (config & AF_RESOURCE_MOTION) != 0;
    bool gi = (config & AF_RESOURCE_RAY_GI) != 0;
    desc.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA16Float;
    desc.colorAttachments[1].pixelFormat = (motion || gi) ? MTLPixelFormatRGBA16Float : MTLPixelFormatInvalid;
    desc.colorAttachments[2].pixelFormat = gi ? MTLPixelFormatRGBA16Float : MTLPixelFormatInvalid;
    desc.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
}

bool af_mtl_build_mesh_pipelines(af_mtl_renderer *r, id<MTLFunction> frag_fn, id<MTLRenderPipelineState> *out_opaque,
                                 id<MTLRenderPipelineState> *out_blend) {
    if (!r || !frag_fn || !out_opaque || !out_blend)
        return false;
    *out_opaque = nil;
    *out_blend = nil;

    @autoreleasepool {
        NSError *err = nil;
        MTLRenderPipelineDescriptor *desc = [[MTLRenderPipelineDescriptor alloc] init];
        desc.label = @"Afterlight Mesh Opaque";
        desc.vertexFunction = r->mesh_vert_fn;
        desc.fragmentFunction = frag_fn;
        desc.vertexDescriptor = make_mesh_vertex_descriptor();
        configure_scene_attachments(desc, r->swapchain.resource_config);

        id<MTLRenderPipelineState> opaque = [r->device newRenderPipelineStateWithDescriptor:desc error:&err];
        if (!opaque) {
            [desc release];
            af_log("error", "Metal mesh opaque pipeline failed: %s", err ? err.localizedDescription.UTF8String : "unknown");
            return false;
        }

        desc.label = @"Afterlight Mesh Blend";
        desc.colorAttachments[0].blendingEnabled = YES;
        desc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
        desc.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        desc.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
        desc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
        desc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        desc.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;

        id<MTLRenderPipelineState> blend = [r->device newRenderPipelineStateWithDescriptor:desc error:&err];
        [desc release];
        if (!blend) {
            [opaque release];
            af_log("error", "Metal mesh blend pipeline failed: %s", err ? err.localizedDescription.UTF8String : "unknown");
            return false;
        }

        *out_opaque = opaque;
        *out_blend = blend;
        return true;
    }
}

static bool create_scene_pipelines(af_mtl_renderer *r) {
    af_mtl_swapchain *sc = &r->swapchain;
    if ((sc->resource_config & AF_RESOURCE_RAY_QUERY) && getenv("AFTERLIGHT_TEST_RAY_PIPELINE_FAIL"))
        return false;

    id<MTLFunction> frag_fn = (sc->resource_config & AF_RESOURCE_RAY_QUERY) ? r->mesh_ray_frag_fn : r->mesh_frag_fn;
    if (!frag_fn)
        return false;

    if (!af_mtl_build_mesh_pipelines(r, frag_fn, &sc->pipeline, &sc->blend_pipeline))
        return false;

    @autoreleasepool {
        NSError *err = nil;
        MTLRenderPipelineDescriptor *desc = [[MTLRenderPipelineDescriptor alloc] init];
        desc.label = @"Afterlight Water Attenuation";
        desc.vertexFunction = r->mesh_vert_fn;
        desc.fragmentFunction = frag_fn;
        desc.vertexDescriptor = make_mesh_vertex_descriptor();
        configure_scene_attachments(desc, sc->resource_config);
        desc.colorAttachments[0].blendingEnabled = YES;
        desc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorZero;
        desc.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorSourceColor;
        desc.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
        desc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
        desc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        desc.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;

        sc->water_attenuation = [r->device newRenderPipelineStateWithDescriptor:desc error:&err];
        if (!sc->water_attenuation) {
            [desc release];
            af_log("error", "Metal water attenuation pipeline failed: %s",
                   err ? err.localizedDescription.UTF8String : "unknown");
            return false;
        }

        desc.label = @"Afterlight Water Scattering";
        desc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorOne;
        desc.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOne;
        sc->water_scattering = [r->device newRenderPipelineStateWithDescriptor:desc error:&err];
        [desc release];
        if (!sc->water_scattering) {
            af_log("error", "Metal water scattering pipeline failed: %s",
                   err ? err.localizedDescription.UTF8String : "unknown");
            return false;
        }

        MTLRenderPipelineDescriptor *sdesc = [[MTLRenderPipelineDescriptor alloc] init];
        sdesc.label = @"Afterlight Sky";
        sdesc.vertexFunction = r->sky_vert_fn;
        sdesc.fragmentFunction = r->sky_frag_fn;
        configure_scene_attachments(sdesc, sc->resource_config);
        sc->sky_pipeline = [r->device newRenderPipelineStateWithDescriptor:sdesc error:&err];
        [sdesc release];
        if (!sc->sky_pipeline) {
            af_log("error", "Metal sky pipeline failed: %s", err ? err.localizedDescription.UTF8String : "unknown");
            return false;
        }
    }

    for (size_t i = 0; i < r->custom_shader_count; ++i) {
        AF_MTL_RELEASE(r->custom_shaders[i].opaque_pipeline);
        AF_MTL_RELEASE(r->custom_shaders[i].blend_pipeline);
        if (!af_mtl_build_mesh_pipelines(r, r->custom_shaders[i].fragment_fn, &r->custom_shaders[i].opaque_pipeline,
                                         &r->custom_shaders[i].blend_pipeline))
            return false;
    }
    return true;
}

void af_mtl_destroy_swapchain(af_mtl_renderer *r) {
    af_mtl_swapchain *sc = &r->swapchain;
    sc->upscaler_created = false;
    af_mtl_post_destroy(sc->post);
    sc->post = NULL;
    af_mtl_refraction_destroy(sc->refraction);
    sc->refraction = NULL;
    for (unsigned i = 0; i < AF_FRAMES_IN_FLIGHT; ++i)
        AF_MTL_RELEASE(sc->depths[i]);
    sc->depth_bytes = 0;
    for (size_t i = 0; i < r->custom_shader_count; ++i) {
        AF_MTL_RELEASE(r->custom_shaders[i].opaque_pipeline);
        AF_MTL_RELEASE(r->custom_shaders[i].blend_pipeline);
    }
    AF_MTL_RELEASE(sc->pipeline);
    AF_MTL_RELEASE(sc->blend_pipeline);
    AF_MTL_RELEASE(sc->sky_pipeline);
    AF_MTL_RELEASE(sc->water_attenuation);
    AF_MTL_RELEASE(sc->water_scattering);
    sc->width = sc->height = sc->render_width = sc->render_height = 0;
    sc->resource_config = 0;
}

bool af_mtl_create_swapchain(af_mtl_renderer *r, af_render_settings settings, uint32_t config) {
    int pw = 0, ph = 0;
    SDL_GetWindowSizeInPixels(r->window, &pw, &ph);
    if (pw <= 0 || ph <= 0) {
        r->resize_pending = true;
        return true;
    }
    uint32_t width = (uint32_t)pw;
    uint32_t height = (uint32_t)ph;

    if (!r->swapchain_invalid && r->swapchain.post && r->swapchain.width == width && r->swapchain.height == height &&
        r->swapchain.resource_config == config) {
        r->resize_pending = false;
        return true;
    }

    if (!af_mtl_r_wait_idle(r))
        return false;
    af_mtl_destroy_swapchain(r);

    r->layer.drawableSize = CGSizeMake((CGFloat)width, (CGFloat)height);

    bool upscale = (config & AF_RESOURCE_UPSCALER) != 0 && r->upscaler != NULL;
    uint32_t render_w = width, render_h = height;
    if (upscale) {
        float scale = af_mtl_upscaler_quality_scale(r->upscaler, r->upscaler_quality);
        render_w = (uint32_t)lroundf((float)width * scale);
        render_h = (uint32_t)lroundf((float)height * scale);
        if (render_w < 1u)
            render_w = 1u;
        if (render_h < 1u)
            render_h = 1u;
        if (render_w > width)
            render_w = width;
        if (render_h > height)
            render_h = height;
    }

    r->swapchain.width = width;
    r->swapchain.height = height;
    r->swapchain.render_width = render_w;
    r->swapchain.render_height = render_h;
    r->swapchain.resource_config = config;

    @autoreleasepool {
        MTLTextureDescriptor *ddesc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                                                                         width:render_w
                                                                                        height:render_h
                                                                                     mipmapped:NO];
        ddesc.storageMode = MTLStorageModePrivate;
        ddesc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        for (unsigned i = 0; i < AF_FRAMES_IN_FLIGHT; ++i) {
            r->swapchain.depths[i] = [r->device newTextureWithDescriptor:ddesc];
            if (!r->swapchain.depths[i])
                goto fail;
            uint64_t bytes = (uint64_t)[r->swapchain.depths[i] allocatedSize];
            r->swapchain.depth_bytes += bytes ? bytes : (uint64_t)render_w * render_h * 4u;
        }
    }

    bool motion = (config & AF_RESOURCE_MOTION) != 0;
    bool gi = (config & AF_RESOURCE_RAY_GI) != 0;
    r->swapchain.post = af_mtl_post_create(r->device, r->sky_vert_fn, r->post_frag_fn, r->gi_denoise_frag_fn, width,
                                           height, render_w, render_h, AF_FRAMES_IN_FLIGHT, settings, motion, gi,
                                           upscale);
    if (!r->swapchain.post)
        goto fail;

    uint32_t refr_w = (config & AF_RESOURCE_REFRACTION) ? render_w : 1u;
    uint32_t refr_h = (config & AF_RESOURCE_REFRACTION) ? render_h : 1u;
    r->swapchain.refraction = af_mtl_refraction_create(r->device, refr_w, refr_h, AF_FRAMES_IN_FLIGHT);
    if (!r->swapchain.refraction)
        goto fail;

    if (upscale) {
        if (!af_mtl_upscaler_configure(r->upscaler, render_w, render_h, width, height, true))
            goto fail;
        r->swapchain.upscaler_created = true;
    }

    if (!create_scene_pipelines(r))
        goto fail;

    r->resize_pending = false;
    r->swapchain_invalid = false;
    r->temporal_valid = false;
    ++r->swapchain_rebuilds;
    return true;

fail:
    af_mtl_destroy_swapchain(r);
    return false;
}

bool af_mtl_r_wait_idle(af_mtl_renderer *r) {
    if (!r || !r->queue)
        return true;
    if (r->frame_semaphore) {
        unsigned slots = r->frame_semaphore_held ? (AF_FRAMES_IN_FLIGHT - 1u) : AF_FRAMES_IN_FLIGHT;
        for (unsigned i = 0; i < slots; ++i)
            dispatch_semaphore_wait(r->frame_semaphore, DISPATCH_TIME_FOREVER);
        for (unsigned i = 0; i < slots; ++i)
            dispatch_semaphore_signal(r->frame_semaphore);
    }
    @autoreleasepool {
        for (unsigned i = 0; i < AF_FRAMES_IN_FLIGHT; ++i) {
            if (r->frames[i].last_command) {
                [r->frames[i].last_command waitUntilCompleted];
                AF_MTL_RELEASE(r->frames[i].last_command);
            }
        }
    }
    return true;
}

void af_mtl_r_resize(af_mtl_renderer *r) {
    if (r)
        r->resize_pending = true;
}

/* Native MetalFX upscaler controls exposed through renderer upscaler API. */
void af_mtl_r_set_upscaler(af_mtl_renderer *r, const struct af_vulkan_upscaler_api *api, void *state) {
    (void)r;
    (void)api;
    (void)state;
}

bool af_mtl_r_set_upscaler_enabled(af_mtl_renderer *r, bool enabled) {
    if (!r || !r->upscaler)
        return false;
    if (r->upscaler_enabled != enabled) {
        r->upscaler_enabled = enabled;
        r->upscaler_failed = false;
        r->swapchain_invalid = r->resize_pending = true;
    }
    return true;
}

bool af_mtl_r_upscaler_active(const af_mtl_renderer *r) {
    return r && r->upscaler && r->upscaler_enabled && !r->upscaler_failed && r->swapchain.upscaler_created;
}

uint32_t af_mtl_r_upscaler_quality_count(const af_mtl_renderer *r) {
    return r ? af_mtl_upscaler_quality_count(r->upscaler) : 0u;
}

const char *af_mtl_r_upscaler_quality_name(const af_mtl_renderer *r, uint32_t index) {
    return r ? af_mtl_upscaler_quality_name(r->upscaler, index) : NULL;
}

uint32_t af_mtl_r_upscaler_quality(const af_mtl_renderer *r) {
    return r ? r->upscaler_quality : 0u;
}

bool af_mtl_r_set_upscaler_quality(af_mtl_renderer *r, uint32_t index) {
    if (!r || !r->upscaler || index >= af_mtl_upscaler_quality_count(r->upscaler))
        return false;
    if (r->upscaler_quality != index) {
        r->upscaler_quality = index;
        r->swapchain_invalid = r->resize_pending = true;
        r->temporal_valid = false;
    }
    return true;
}

void af_mtl_r_release_upscaler(af_mtl_renderer *r) {
    if (!r)
        return;
    (void)af_mtl_r_wait_idle(r);
    r->upscaler_enabled = false;
    r->swapchain.upscaler_created = false;
}

static bool load_shader_library(af_mtl_renderer *r) {
    @autoreleasepool {
        dispatch_data_t data = dispatch_data_create(af_metal_shaders_metallib, af_metal_shaders_metallib_size, NULL,
                                                    DISPATCH_DATA_DESTRUCTOR_DEFAULT);
        NSError *err = nil;
        r->shader_lib = [r->device newLibraryWithData:data error:&err];
        dispatch_release(data);
        if (!r->shader_lib) {
            af_log("error", "Metal shader library load failed: %s",
                   err ? err.localizedDescription.UTF8String : "unknown");
            return false;
        }
        r->mesh_vert_fn = [r->shader_lib newFunctionWithName:@"af_mesh_vert_main"];
        r->mesh_frag_fn = [r->shader_lib newFunctionWithName:@"af_mesh_frag_main"];
        r->mesh_ray_frag_fn = [r->shader_lib newFunctionWithName:@"af_mesh_ray_frag_main"];
        r->sky_vert_fn = [r->shader_lib newFunctionWithName:@"af_fullscreen_vert_main"];
        r->sky_frag_fn = [r->shader_lib newFunctionWithName:@"af_sky_frag_main"];
        r->shadow_vert_fn = [r->shader_lib newFunctionWithName:@"af_shadow_vert_main"];
        r->shadow_frag_fn = [r->shader_lib newFunctionWithName:@"af_shadow_frag_main"];
        r->post_frag_fn = [r->shader_lib newFunctionWithName:@"af_post_frag_main"];
        r->gi_denoise_frag_fn = [r->shader_lib newFunctionWithName:@"af_gi_denoise_frag_main"];
        r->ui_vert_fn = [r->shader_lib newFunctionWithName:@"af_ui_vert_main"];
        r->ui_frag_fn = [r->shader_lib newFunctionWithName:@"af_ui_frag_main"];
        r->cluster_icb_fn = [r->shader_lib newFunctionWithName:@"af_cluster_icb_main"];

        if (!r->mesh_vert_fn || !r->mesh_frag_fn || !r->sky_vert_fn || !r->sky_frag_fn || !r->shadow_vert_fn ||
            !r->shadow_frag_fn || !r->post_frag_fn || !r->ui_vert_fn || !r->ui_frag_fn) {
            af_log("error", "Metal shader library is missing required entry points.");
            return false;
        }
    }
    return true;
}

static bool create_depth_states(af_mtl_renderer *r) {
    @autoreleasepool {
        MTLDepthStencilDescriptor *d = [[MTLDepthStencilDescriptor alloc] init];
        d.depthCompareFunction = MTLCompareFunctionLess;
        d.depthWriteEnabled = YES;
        r->depth_rw = [r->device newDepthStencilStateWithDescriptor:d];

        d.depthWriteEnabled = NO;
        r->depth_ro = [r->device newDepthStencilStateWithDescriptor:d];

        d.depthCompareFunction = MTLCompareFunctionAlways;
        d.depthWriteEnabled = NO;
        r->depth_none = [r->device newDepthStencilStateWithDescriptor:d];
        [d release];
    }
    return r->depth_rw && r->depth_ro && r->depth_none;
}

static bool create_frame_buffers(af_mtl_renderer *r) {
    uint64_t skin_bytes = (uint64_t)(2u * AF_SKIN_MATRICES) * sizeof(af_mat4);
    uint64_t light_bytes = (uint64_t)AF_SCENE_MAX_LOCAL_LIGHTS * sizeof(af_gpu_light);
    uint64_t init_motion_bytes = 16u * sizeof(af_motion_record);
    uint64_t init_tile_bytes = 1024u * sizeof(af_light_tile_mask);

    for (unsigned i = 0; i < AF_FRAMES_IN_FLIGHT; ++i) {
        af_mtl_frame *f = &r->frames[i];
        if (!af_mtl_create_buffer(r, NULL, sizeof(af_frame_uniform), &f->uniform) ||
            !af_mtl_create_buffer(r, NULL, skin_bytes, &f->skin_buffer) ||
            !af_mtl_create_buffer(r, NULL, init_motion_bytes, &f->motion_buffer) ||
            !af_mtl_create_buffer(r, NULL, light_bytes, &f->light_buffer) ||
            !af_mtl_create_buffer(r, NULL, init_tile_bytes, &f->light_tile_buffer))
            return false;
        memset([f->motion_buffer.handle contents], 0, (size_t)init_motion_bytes);
        memset([f->light_buffer.handle contents], 0, (size_t)light_bytes);
        memset([f->light_tile_buffer.handle contents], 0, (size_t)init_tile_bytes);
        af_mat4 *skins = (af_mat4 *)[f->skin_buffer.handle contents];
        for (size_t s = 0; s < 2u * AF_SKIN_MATRICES; ++s)
            skins[s] = af_mat4_identity();
    }
    return true;
}

void af_mtl_r_destroy(af_mtl_renderer *r) {
    if (!r)
        return;
    @autoreleasepool {
        if (r->queue)
            (void)af_mtl_r_wait_idle(r);
        af_mtl_destroy_swapchain(r);
        af_mtl_destroy_cluster_compute(r);
        af_mtl_ray_query_destroy(r->rays);
        af_mtl_upscaler_destroy(r->upscaler);
        af_mtl_destroy_buffer(&r->capture_buffer);
        af_mtl_ui_destroy(r->ui);
        af_mtl_shadows_destroy(r->local_shadows);
        af_mtl_shadows_destroy(r->shadows);
        free(r->shadow_draws);
        free(r->shadow_bounds);
        for (size_t i = 0; i < r->texture_count; ++i)
            af_mtl_texture_destroy(r->textures[i].texture);
        free(r->textures);
        for (size_t i = 0; i < r->custom_shader_count; ++i) {
            AF_MTL_RELEASE(r->custom_shaders[i].opaque_pipeline);
            AF_MTL_RELEASE(r->custom_shaders[i].blend_pipeline);
            AF_MTL_RELEASE(r->custom_shaders[i].fragment_fn);
            free(r->custom_shaders[i].spirv);
        }
        free(r->custom_shaders);
        af_mtl_texture_destroy(r->white);
        af_mtl_texture_destroy(r->flat_normal);
        for (size_t i = 0; i < r->environment_count; ++i) {
            af_mtl_texture_destroy(r->environments[i].specular);
            af_mtl_texture_destroy(r->environments[i].diffuse);
            af_mtl_texture_destroy(r->environments[i].sky);
        }
        af_mtl_texture_destroy(r->environment);
        af_mtl_texture_destroy(r->irradiance);
        af_mtl_texture_destroy(r->brdf);
        for (size_t i = 0; i < r->mesh_count; ++i)
            af_mtl_stream_destroy(&r->meshes[i]);
        free(r->meshes);
        for (unsigned i = 0; i < AF_FRAMES_IN_FLIGHT; ++i) {
            af_mtl_destroy_buffer(&r->frames[i].uniform);
            af_mtl_destroy_buffer(&r->frames[i].skin_buffer);
            af_mtl_destroy_buffer(&r->frames[i].motion_buffer);
            af_mtl_destroy_buffer(&r->frames[i].morph_buffer);
            af_mtl_destroy_buffer(&r->frames[i].light_buffer);
            af_mtl_destroy_buffer(&r->frames[i].light_tile_buffer);
            AF_MTL_RELEASE(r->frames[i].last_command);
        }
        AF_MTL_RELEASE(r->depth_rw);
        AF_MTL_RELEASE(r->depth_ro);
        AF_MTL_RELEASE(r->depth_none);
        AF_MTL_RELEASE(r->mesh_vert_fn);
        AF_MTL_RELEASE(r->mesh_frag_fn);
        AF_MTL_RELEASE(r->mesh_ray_frag_fn);
        AF_MTL_RELEASE(r->sky_vert_fn);
        AF_MTL_RELEASE(r->sky_frag_fn);
        AF_MTL_RELEASE(r->shadow_vert_fn);
        AF_MTL_RELEASE(r->shadow_frag_fn);
        AF_MTL_RELEASE(r->post_frag_fn);
        AF_MTL_RELEASE(r->gi_denoise_frag_fn);
        AF_MTL_RELEASE(r->ui_vert_fn);
        AF_MTL_RELEASE(r->ui_frag_fn);
        AF_MTL_RELEASE(r->cluster_icb_fn);
        AF_MTL_RELEASE(r->shader_lib);
        AF_MTL_RELEASE(r->queue);
        AF_MTL_RELEASE(r->device);
        if (r->frame_semaphore)
            dispatch_release(r->frame_semaphore);
        if (r->metal_view)
            SDL_Metal_DestroyView(r->metal_view);
        free(r->ray_inputs);
        free(r->light_tile_staging);
        free(r->motion_previous);
        free(r->motion_next);
        free(r->motion_records);
        free(r->morph_draws);
        free(r->morph_vertices);
        free(r->morph_deltas);
    }
    free(r);
}

static const af_renderer_ops mtl_renderer_ops;

af_renderer *af_mtl_renderer_create(SDL_Window *window, bool enable_validation, af_extensions *extensions) {
    (void)extensions;
    af_mtl_renderer *r = calloc(1, sizeof(*r));
    if (!r)
        return NULL;
    r->base.ops = &mtl_renderer_ops;
    atomic_init(&r->validation_errors, 0);
    r->window = window;
    r->validation_enabled = enable_validation;
    r->gpu_budget_ms = 1000.0 / 60.0;
    r->gpu_budget_bytes = UINT64_C(512) * 1024 * 1024;
    r->page_budget = UINT64_C(8) * 1024 * 1024;
    r->anisotropy = 8.0f;
    r->temporal_supported = true;
    r->capture_supported = true;
    r->resize_pending = true;
    r->frame_semaphore = dispatch_semaphore_create(AF_FRAMES_IN_FLIGHT);
    if (!r->frame_semaphore)
        goto fail;

    @autoreleasepool {
        r->device = MTLCreateSystemDefaultDevice();
        if (!r->device) {
            af_log("error", "No Metal GPU device available.");
            goto fail;
        }
        r->queue = [r->device newCommandQueue];
        if (!r->queue) {
            af_log("error", "Metal command queue creation failed.");
            goto fail;
        }
        r->metal_view = SDL_Metal_CreateView(window);
        if (!r->metal_view) {
            af_log("error", "SDL_Metal_CreateView failed: %s", SDL_GetError());
            goto fail;
        }
        r->layer = (CAMetalLayer *)SDL_Metal_GetLayer(r->metal_view);
        if (!r->layer) {
            af_log("error", "SDL_Metal_GetLayer returned NULL.");
            goto fail;
        }
        r->layer.device = r->device;
        r->layer.pixelFormat = MTLPixelFormatBGRA8Unorm_sRGB;
        r->layer.displaySyncEnabled = YES;
        r->layer.framebufferOnly = NO;
        r->layer.maximumDrawableCount = 3;

        if (!load_shader_library(r) || !create_depth_states(r) || !create_frame_buffers(r))
            goto fail;

        const char *disable_ray = getenv("AFTERLIGHT_DISABLE_RAY_QUERY");
        bool disable_ray_env = disable_ray && disable_ray[0] && strcmp(disable_ray, "0") != 0;
        r->ray_supported = !disable_ray_env && [r->device supportsRaytracing] && (r->mesh_ray_frag_fn != nil);

        /* Apple Silicon / Metal 3 supports GPU-driven Indirect Command Buffers. */
        r->cluster_gpu_supported = (r->cluster_icb_fn != nil) &&
                                   ([r->device supportsFamily:MTLGPUFamilyApple7] ||
                                    [r->device supportsFamily:MTLGPUFamilyMetal3]);

        r->upscaler = af_mtl_upscaler_create(r->device);
        const char *mfx = getenv("AFTERLIGHT_METALFX");
        if (r->upscaler && mfx && mfx[0] && strcmp(mfx, "0") != 0)
            r->upscaler_enabled = true;

        af_log("info", "GPU: %s (Metal native)", [r->device name].UTF8String);
        af_log("info", "Hardware ray queries (Metal): %s.", r->ray_supported ? "available" : "unavailable (raster fallback)");
        if (r->upscaler)
            af_log("info", "MetalFX upscaling: available (%s).", r->upscaler_enabled ? "enabled" : "standby");
    }

    const unsigned char white[] = {255, 255, 255, 255};
    r->white = af_mtl_texture_create(r->device, white, 1, 1, true, true, r->anisotropy);
    const unsigned char flat[] = {128, 128, 255, 255};
    r->flat_normal = af_mtl_texture_create(r->device, flat, 1, 1, false, true, r->anisotropy);

    af_shadow_settings shadows = af_shadow_settings_default();
    r->shadow_resolution = shadows.resolution;
    r->shadow_layers = shadows.mode == AF_SHADOW_CASCADED ? 4u : 1u;
    r->shadows = af_mtl_shadows_create(r->device, r->shadow_vert_fn, r->shadow_frag_fn, shadows.resolution,
                                       r->shadow_layers);
    r->local_shadow_resolution = 512;
    r->local_shadow_layers = 1;
    r->local_shadows = af_mtl_shadows_create(r->device, r->shadow_vert_fn, r->shadow_frag_fn, 512, 1);

    if (!r->white || !r->flat_normal || !r->shadows || !r->local_shadows || !af_mtl_create_environment(r, 0) ||
        !af_mtl_create_swapchain(r, af_render_settings_default(), 0))
        goto fail;

    return &r->base;

fail:
    af_mtl_r_destroy(r);
    return NULL;
}

/* Type-safe af_renderer_ops vtable thunks. */
static void mtl_op_destroy(af_renderer *r) { af_mtl_r_destroy((af_mtl_renderer *)r); }
static void mtl_op_set_upscaler(af_renderer *r, const struct af_vulkan_upscaler_api *api, void *state) {
    af_mtl_r_set_upscaler((af_mtl_renderer *)r, api, state);
}
static bool mtl_op_set_upscaler_enabled(af_renderer *r, bool enabled) {
    return af_mtl_r_set_upscaler_enabled((af_mtl_renderer *)r, enabled);
}
static bool mtl_op_upscaler_active(const af_renderer *r) {
    return af_mtl_r_upscaler_active((const af_mtl_renderer *)r);
}
static uint32_t mtl_op_upscaler_quality_count(const af_renderer *r) {
    return af_mtl_r_upscaler_quality_count((const af_mtl_renderer *)r);
}
static const char *mtl_op_upscaler_quality_name(const af_renderer *r, uint32_t index) {
    return af_mtl_r_upscaler_quality_name((const af_mtl_renderer *)r, index);
}
static uint32_t mtl_op_upscaler_quality(const af_renderer *r) {
    return af_mtl_r_upscaler_quality((const af_mtl_renderer *)r);
}
static bool mtl_op_set_upscaler_quality(af_renderer *r, uint32_t index) {
    return af_mtl_r_set_upscaler_quality((af_mtl_renderer *)r, index);
}
static void mtl_op_release_upscaler(af_renderer *r) { af_mtl_r_release_upscaler((af_mtl_renderer *)r); }
static bool mtl_op_wait_idle(af_renderer *r) { return af_mtl_r_wait_idle((af_mtl_renderer *)r); }
static af_temporal_test_stats mtl_op_test_temporal(const af_renderer *r) {
    return af_mtl_r_test_temporal((const af_mtl_renderer *)r);
}
static void mtl_op_reset_temporal(af_renderer *r) { af_mtl_r_reset_temporal((af_mtl_renderer *)r); }
static void mtl_op_resize(af_renderer *r) { af_mtl_r_resize((af_mtl_renderer *)r); }
static bool mtl_op_upload_mesh(af_renderer *r, const char *name, const af_mesh_data *mesh) {
    return af_mtl_r_upload_mesh((af_mtl_renderer *)r, name, mesh);
}
static bool mtl_op_upload_mesh_file(af_renderer *r, const char *name, af_mesh_file *mesh, af_mesh_sidedness sidedness) {
    return af_mtl_r_upload_mesh_file((af_mtl_renderer *)r, name, mesh, sidedness);
}
static bool mtl_op_set_skin_palette(af_renderer *r, af_object_id object, const af_mat4 *matrices, size_t count) {
    return af_mtl_r_set_skin_palette((af_mtl_renderer *)r, object, matrices, count);
}
static void mtl_op_clear_skin_palettes(af_renderer *r) { af_mtl_r_clear_skin_palettes((af_mtl_renderer *)r); }
static void mtl_op_set_object_worlds(af_renderer *r, const af_scene *scene, const af_mat4 *world,
                                     const af_animation_pose *const *morph_poses, size_t count) {
    af_mtl_r_set_object_worlds((af_mtl_renderer *)r, scene, world, morph_poses, count);
}
static bool mtl_op_remove_texture(af_renderer *r, const char *name) {
    return af_mtl_r_remove_texture((af_mtl_renderer *)r, name);
}
static bool mtl_op_upload_texture(af_renderer *r, const char *name, const unsigned char *rgba, uint32_t width,
                                  uint32_t height) {
    return af_mtl_r_upload_texture((af_mtl_renderer *)r, name, rgba, width, height);
}
static bool mtl_op_upload_material_shader(af_renderer *r, const char *name, const uint32_t *spirv, size_t word_count) {
    return af_mtl_r_upload_material_shader((af_mtl_renderer *)r, name, spirv, word_count);
}
static bool mtl_op_remove_material_shader(af_renderer *r, const char *name) {
    return af_mtl_r_remove_material_shader((af_mtl_renderer *)r, name);
}
static bool mtl_op_remove_mesh(af_renderer *r, const char *name) {
    return af_mtl_r_remove_mesh((af_mtl_renderer *)r, name);
}
static bool mtl_op_upload_overlay_atlas(af_renderer *r, const unsigned char *rgba, uint32_t width, uint32_t height) {
    return af_mtl_r_upload_overlay_atlas((af_mtl_renderer *)r, rgba, width, height);
}
static af_object_id mtl_op_pick_object(af_renderer *r, const af_scene *scene, const af_camera *camera, af_rect viewport,
                                       af_vec2 point) {
    return af_mtl_r_pick_object((af_mtl_renderer *)r, scene, camera, viewport, point);
}
static bool mtl_op_update_water(af_renderer *r, const af_physics *physics, const af_scene *scene, float alpha) {
    return af_mtl_r_update_water((af_mtl_renderer *)r, physics, scene, alpha);
}
static bool mtl_op_update_landscape(af_renderer *r, const af_scene *scene) {
    return af_mtl_r_update_landscape((af_mtl_renderer *)r, scene);
}
static bool mtl_op_draw(af_renderer *r, af_color color, const af_scene *scene, const af_camera *camera,
                        af_rect viewport, af_object_id highlighted, const af_overlay_frame *overlay,
                        float frame_time_ms, bool *rendered) {
    return af_mtl_r_draw((af_mtl_renderer *)r, color, scene, camera, viewport, highlighted, overlay, frame_time_ms,
                         rendered);
}
static bool mtl_op_upload_overlay_texture(af_renderer *r, uint32_t id, const unsigned char *rgba, uint32_t width,
                                          uint32_t height) {
    return af_mtl_r_upload_overlay_texture((af_mtl_renderer *)r, id, rgba, width, height);
}
static bool mtl_op_remove_overlay_texture(af_renderer *r, uint32_t id) {
    return af_mtl_r_remove_overlay_texture((af_mtl_renderer *)r, id);
}
static bool mtl_op_test_capture_motion(af_renderer *r, af_vec2 position) {
    return af_mtl_r_test_capture_motion((af_mtl_renderer *)r, position);
}
static bool mtl_op_test_read_motion(af_renderer *r, af_vec4 *out) {
    return af_mtl_r_test_read_motion((af_mtl_renderer *)r, out);
}
static bool mtl_op_test_capture_region(af_renderer *r, af_vec2 position, uint32_t width, uint32_t height) {
    return af_mtl_r_test_capture_region((af_mtl_renderer *)r, position, width, height);
}
static bool mtl_op_test_read_pixels(af_renderer *r, unsigned char *rgba, size_t capacity, uint32_t *width,
                                    uint32_t *height) {
    return af_mtl_r_test_read_pixels((af_mtl_renderer *)r, rgba, capacity, width, height);
}
static bool mtl_op_test_capture_pixel(af_renderer *r, af_vec2 position) {
    return af_mtl_r_test_capture_pixel((af_mtl_renderer *)r, position);
}
static bool mtl_op_test_read_pixel(af_renderer *r, unsigned char rgba[4]) {
    return af_mtl_r_test_read_pixel((af_mtl_renderer *)r, rgba);
}
static bool mtl_op_set_debug_view(af_renderer *r, af_debug_view view) {
    return af_mtl_r_set_debug_view((af_mtl_renderer *)r, view);
}
static void mtl_op_set_visibility_culling(af_renderer *r, bool enabled) {
    af_mtl_r_set_visibility_culling((af_mtl_renderer *)r, enabled);
}
static void mtl_op_set_prop_instancing(af_renderer *r, bool enabled) {
    af_mtl_r_set_prop_instancing((af_mtl_renderer *)r, enabled);
}
static bool mtl_op_upload_cluster_stream(af_renderer *r, const char *name, af_cluster_mesh *mesh,
                                         af_cluster_stream *stream) {
    return af_mtl_r_upload_cluster_stream((af_mtl_renderer *)r, name, mesh, stream);
}
static bool mtl_op_upload_clusters(af_renderer *r, const char *name, af_cluster_mesh *mesh) {
    return af_mtl_r_upload_clusters((af_mtl_renderer *)r, name, mesh);
}
static void mtl_op_set_cluster_lod(af_renderer *r, bool enabled) {
    af_mtl_r_set_cluster_lod((af_mtl_renderer *)r, enabled);
}
static void mtl_op_set_cluster_gpu_culling(af_renderer *r, bool enabled) {
    af_mtl_r_set_cluster_gpu_culling((af_mtl_renderer *)r, enabled);
}
static void mtl_op_set_cluster_rendering(af_renderer *r, bool enabled, bool debug_colors) {
    af_mtl_r_set_cluster_rendering((af_mtl_renderer *)r, enabled, debug_colors);
}
static bool mtl_op_set_cluster_cache_budget(af_renderer *r, uint64_t bytes) {
    return af_mtl_r_set_cluster_cache_budget((af_mtl_renderer *)r, bytes);
}
static af_cluster_stats mtl_op_cluster_stats(const af_renderer *r) {
    return af_mtl_r_cluster_stats((const af_mtl_renderer *)r);
}
static bool mtl_op_set_render_budget(af_renderer *r, double gpu_ms, uint64_t bytes) {
    return af_mtl_r_set_render_budget((af_mtl_renderer *)r, gpu_ms, bytes);
}
static af_engine_stats mtl_op_stats(const af_renderer *r) { return af_mtl_r_stats((const af_mtl_renderer *)r); }
static bool mtl_op_texture_info(const af_renderer *r, const char *name, af_texture_info *out) {
    return af_mtl_r_texture_info((const af_mtl_renderer *)r, name, out);
}
static bool mtl_op_upload_environment(af_renderer *r, const char *name, const float *rgba, uint32_t width,
                                      uint32_t height) {
    return af_mtl_r_upload_environment((af_mtl_renderer *)r, name, rgba, width, height);
}
static bool mtl_op_upload_normal_map(af_renderer *r, const char *name, const unsigned char *rgba, uint32_t width,
                                     uint32_t height) {
    return af_mtl_r_upload_normal_map((af_mtl_renderer *)r, name, rgba, width, height);
}
static bool mtl_op_commit_asset(af_renderer *r, const char *staged, const char *target, bool mesh) {
    return af_mtl_r_commit_asset((af_mtl_renderer *)r, staged, target, mesh);
}
static bool mtl_op_upload_environment_baked(af_renderer *r, const char *name, const af_hdr_file *source,
                                            const af_ibl_images *images) {
    return af_mtl_r_upload_environment_baked((af_mtl_renderer *)r, name, source, images);
}
static void mtl_op_set_interpolation(af_renderer *r, const af_scene *previous, float alpha) {
    af_mtl_r_set_interpolation((af_mtl_renderer *)r, previous, alpha);
}

static const af_renderer_ops mtl_renderer_ops = {
    .backend = AF_RENDER_BACKEND_METAL,
    .destroy = mtl_op_destroy,
    .set_upscaler = mtl_op_set_upscaler,
    .set_upscaler_enabled = mtl_op_set_upscaler_enabled,
    .upscaler_active = mtl_op_upscaler_active,
    .upscaler_quality_count = mtl_op_upscaler_quality_count,
    .upscaler_quality_name = mtl_op_upscaler_quality_name,
    .upscaler_quality = mtl_op_upscaler_quality,
    .set_upscaler_quality = mtl_op_set_upscaler_quality,
    .release_upscaler = mtl_op_release_upscaler,
    .wait_idle = mtl_op_wait_idle,
    .test_temporal = mtl_op_test_temporal,
    .reset_temporal = mtl_op_reset_temporal,
    .resize = mtl_op_resize,
    .upload_mesh = mtl_op_upload_mesh,
    .upload_mesh_file = mtl_op_upload_mesh_file,
    .set_skin_palette = mtl_op_set_skin_palette,
    .clear_skin_palettes = mtl_op_clear_skin_palettes,
    .set_object_worlds = mtl_op_set_object_worlds,
    .remove_texture = mtl_op_remove_texture,
    .upload_texture = mtl_op_upload_texture,
    .upload_material_shader = mtl_op_upload_material_shader,
    .remove_material_shader = mtl_op_remove_material_shader,
    .remove_mesh = mtl_op_remove_mesh,
    .upload_overlay_atlas = mtl_op_upload_overlay_atlas,
    .pick_object = mtl_op_pick_object,
    .update_water = mtl_op_update_water,
    .update_landscape = mtl_op_update_landscape,
    .draw = mtl_op_draw,
    .upload_overlay_texture = mtl_op_upload_overlay_texture,
    .remove_overlay_texture = mtl_op_remove_overlay_texture,
    .test_capture_motion = mtl_op_test_capture_motion,
    .test_read_motion = mtl_op_test_read_motion,
    .test_capture_region = mtl_op_test_capture_region,
    .test_read_pixels = mtl_op_test_read_pixels,
    .test_capture_pixel = mtl_op_test_capture_pixel,
    .test_read_pixel = mtl_op_test_read_pixel,
    .set_debug_view = mtl_op_set_debug_view,
    .set_visibility_culling = mtl_op_set_visibility_culling,
    .set_prop_instancing = mtl_op_set_prop_instancing,
    .upload_cluster_stream = mtl_op_upload_cluster_stream,
    .upload_clusters = mtl_op_upload_clusters,
    .set_cluster_lod = mtl_op_set_cluster_lod,
    .set_cluster_gpu_culling = mtl_op_set_cluster_gpu_culling,
    .set_cluster_rendering = mtl_op_set_cluster_rendering,
    .set_cluster_cache_budget = mtl_op_set_cluster_cache_budget,
    .cluster_stats = mtl_op_cluster_stats,
    .set_render_budget = mtl_op_set_render_budget,
    .stats = mtl_op_stats,
    .texture_info = mtl_op_texture_info,
    .upload_environment = mtl_op_upload_environment,
    .upload_normal_map = mtl_op_upload_normal_map,
    .commit_asset = mtl_op_commit_asset,
    .upload_environment_baked = mtl_op_upload_environment_baked,
    .set_interpolation = mtl_op_set_interpolation,
};
