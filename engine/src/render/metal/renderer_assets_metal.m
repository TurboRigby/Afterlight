/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "metal_internal.h"

void af_mtl_active_environment_textures(const af_mtl_renderer *r, af_mtl_texture **out_spec, af_mtl_texture **out_diff,
                                        af_mtl_texture **out_sky) {
    af_mtl_texture *spec = r->environment, *diff = r->irradiance, *sky = r->environment;
    if (r->active_environment[0]) {
        for (size_t i = 0; i < r->environment_count; ++i) {
            if (!strcmp(r->environments[i].name, r->active_environment)) {
                spec = r->environments[i].specular;
                diff = r->environments[i].diffuse;
                sky = r->environments[i].sky;
                break;
            }
        }
    }
    if (out_spec)
        *out_spec = spec;
    if (out_diff)
        *out_diff = diff;
    if (out_sky)
        *out_sky = sky;
}

bool af_mtl_create_environment(af_mtl_renderer *r, uint32_t profile) {
    af_ibl_images images;
    if (!af_ibl_bake(profile, &images))
        return false;
    af_mtl_texture *spec =
        af_mtl_texture_create_hdr(r->device, images.specular, AF_IBL_WIDTH, AF_IBL_HEIGHT, AF_IBL_LEVELS, true);
    af_mtl_texture *diff = af_mtl_texture_create_hdr(r->device, images.diffuse, AF_IBL_DIFFUSE_WIDTH,
                                                     AF_IBL_DIFFUSE_HEIGHT, 1, true);
    af_mtl_texture *brdf =
        r->brdf ? NULL : af_mtl_texture_create_hdr(r->device, images.brdf, AF_IBL_LUT_SIZE, AF_IBL_LUT_SIZE, 1, false);
    af_ibl_free(&images);
    if (!spec || !diff || (!r->brdf && !brdf)) {
        af_mtl_texture_destroy(spec);
        af_mtl_texture_destroy(diff);
        af_mtl_texture_destroy(brdf);
        return false;
    }
    if (r->environment && !af_mtl_r_wait_idle(r)) {
        af_mtl_texture_destroy(spec);
        af_mtl_texture_destroy(diff);
        af_mtl_texture_destroy(brdf);
        return false;
    }
    af_mtl_texture_destroy(r->environment);
    af_mtl_texture_destroy(r->irradiance);
    r->environment = spec;
    r->irradiance = diff;
    if (brdf)
        r->brdf = brdf;
    r->environment_profile = profile;
    return true;
}

af_temporal_test_stats af_mtl_r_test_temporal(const af_mtl_renderer *r) {
    return r ? (af_temporal_test_stats){r->temporal_frame,    r->temporal_resets,  r->swapchain.width,
                                        r->swapchain.height,   r->temporal_objects, r->temporal_skins,
                                        r->temporal_supported, r->temporal_valid,   r->temporal_jitter}
             : (af_temporal_test_stats){0};
}

void af_mtl_r_reset_temporal(af_mtl_renderer *r) {
    r->temporal_valid = false;
    af_mtl_post_reset_temporal(r->swapchain.post);
}

bool af_mtl_r_set_debug_view(af_mtl_renderer *r, af_debug_view view) {
    if (!r || view >= AF_DEBUG_COUNT || (view != AF_DEBUG_SHADED && !r->temporal_supported))
        return false;
    if (r->debug_view != view) {
        r->debug_view = view;
        r->temporal_valid = false;
    }
    return true;
}

void af_mtl_r_set_visibility_culling(af_mtl_renderer *r, bool enabled) {
    r->culling_disabled = !enabled;
}

void af_mtl_r_set_prop_instancing(af_mtl_renderer *r, bool enabled) {
    r->prop_instancing_disabled = !enabled;
}

bool af_mtl_r_set_render_budget(af_mtl_renderer *r, double ms, uint64_t bytes) {
    if (!r || !isfinite(ms) || ms <= 0 || ms > 1000 || !bytes)
        return false;
    r->gpu_budget_ms = ms;
    r->gpu_budget_bytes = bytes;
    return true;
}

af_engine_stats af_mtl_r_stats(const af_mtl_renderer *r) {
    size_t active_meshes = 0;
    for (size_t i = 0; i < r->mesh_count; ++i)
        active_meshes += r->meshes[i].name[0] && !r->meshes[i].retired;
    uint64_t texture_bytes = 0;
    uint32_t mip_levels = 0;
    for (size_t i = 0; i < r->texture_count; ++i) {
        af_texture_info info = af_mtl_texture_info(r->textures[i].texture);
        texture_bytes += info.allocation_bytes;
        mip_levels += info.mip_levels;
    }
    uint64_t mesh_bytes = 0, other = 0;
    for (size_t i = 0; i < r->mesh_count; ++i) {
        const af_mtl_mesh_asset *m = &r->meshes[i];
        mesh_bytes += m->gpu.vertices.allocation_bytes + m->gpu.indices.allocation_bytes;
        if (m->gpu.residency)
            for (unsigned page = 0; page < AF_CLUSTER_RESIDENT_PAGES; ++page) {
                const af_mtl_gpu_mesh *gpu = &m->gpu.residency->pages[page].gpu;
                mesh_bytes += gpu->vertices.allocation_bytes + gpu->indices.allocation_bytes;
            }
        for (unsigned f = 0; f < AF_FRAMES_IN_FLIGHT; ++f)
            mesh_bytes += m->streams[f].gpu.vertices.allocation_bytes + m->streams[f].gpu.indices.allocation_bytes;
    }
    for (unsigned f = 0; f < AF_FRAMES_IN_FLIGHT; ++f) {
        other += r->frames[f].uniform.allocation_bytes + r->frames[f].skin_buffer.allocation_bytes +
                 r->frames[f].motion_buffer.allocation_bytes + r->frames[f].morph_buffer.allocation_bytes +
                 r->frames[f].light_buffer.allocation_bytes + r->frames[f].light_tile_buffer.allocation_bytes +
                 r->frames[f].cluster_input.allocation_bytes + r->frames[f].cluster_transforms.allocation_bytes +
                 r->frames[f].cluster_stats_buf.allocation_bytes + r->frames[f].cluster_indirect.allocation_bytes;
    }
    other += r->swapchain.depth_bytes;
    af_mtl_texture *fixed[] = {r->white, r->flat_normal, r->environment, r->irradiance, r->brdf};
    for (unsigned i = 0; i < 5; ++i)
        if (fixed[i])
            other += af_mtl_texture_info(fixed[i]).allocation_bytes;
    for (size_t i = 0; i < r->environment_count; ++i) {
        other += af_mtl_texture_info(r->environments[i].sky).allocation_bytes +
                 af_mtl_texture_info(r->environments[i].specular).allocation_bytes +
                 af_mtl_texture_info(r->environments[i].diffuse).allocation_bytes;
    }
    af_ray_stats ray = af_mtl_ray_query_stats(r->rays);
    other += ray.bytes;
    uint64_t post = af_mtl_post_allocation_bytes(r->swapchain.post);
    uint64_t shadows = af_mtl_shadows_allocation_bytes(r->shadows) + af_mtl_shadows_allocation_bytes(r->local_shadows);
    uint64_t refraction = af_mtl_refraction_allocation_bytes(r->swapchain.refraction);
    uint64_t tracked = post + shadows + refraction + mesh_bytes + texture_bytes + other;

    return (af_engine_stats){
        .active_backend = AF_RENDER_BACKEND_METAL,
        .ray_query_supported = r->ray_supported,
        .ray_traced_shadows_active = (r->swapchain.resource_config & AF_RESOURCE_RAY_SHADOWS) != 0 && !r->ray_failed,
        .ray_traced_water_reflections_active =
            (r->swapchain.resource_config & AF_RESOURCE_RAY_REFLECTIONS) != 0 && !r->ray_failed,
        .ray_traced_material_effects_active =
            (r->swapchain.resource_config & AF_RESOURCE_RAY_MATERIALS) != 0 && !r->ray_failed,
        .ray_traced_gi_active = (r->swapchain.resource_config & AF_RESOURCE_RAY_GI) != 0 && !r->ray_failed,
        .ray_tracing_failed = r->ray_failed,
        .ray_tracing_bytes = ray.bytes,
        .ray_blas_builds = ray.blas_builds,
        .ray_tlas_builds = ray.tlas_builds,
        .ray_instances = ray.instances,
        .ray_meshes = ray.meshes,
        .ray_raster_casters = r->ray_raster_casters,
        .gpu_ray_build_ms = ray.gpu_build_ms,
        .post_bytes = post,
        .shadow_bytes = shadows,
        .refraction_bytes = refraction,
        .mesh_bytes = mesh_bytes,
        .tracked_gpu_bytes = tracked,
        .gpu_budget_ms = r->gpu_budget_ms,
        .gpu_budget_bytes = r->gpu_budget_bytes,
        .gpu_time_over_budget = r->timing_valid && r->gpu_ms[0] > r->gpu_budget_ms,
        .gpu_memory_over_budget = tracked > r->gpu_budget_bytes,
        .debug_view = r->debug_view,
        .effective_aa = (af_anti_aliasing)(r->swapchain.resource_config & AF_RESOURCE_AA_MASK),
        .visible_objects = r->visible_objects,
        .culled_objects = r->culled_objects,
        .gpu_timing_supported = true,
        .gpu_timing_valid = r->timing_valid,
        .gpu_frame_ms = r->gpu_ms[0],
        .gpu_shadow_ms = r->gpu_ms[1],
        .gpu_scene_ms = r->gpu_ms[2],
        .gpu_post_ms = r->gpu_ms[3],
        .gpu_overlay_ms = r->gpu_ms[4],
        .cpu_fence_ms = r->cpu_fence_ms,
        .cpu_acquire_ms = r->cpu_acquire_ms,
        .cpu_record_ms = r->cpu_record_ms,
        .hdr_enabled = r->swapchain.post != NULL,
        .local_shadow_lights = r->local_shadow_lights,
        .local_shadow_maps = r->local_shadow_lights ? r->local_shadow_layers : 0,
        .local_shadow_resolution = r->local_shadow_resolution,
        .local_shadow_overflow = r->local_shadow_overflow,
        .texture_bytes = texture_bytes,
        .texture_mip_levels = mip_levels,
        .texture_anisotropy = r->anisotropy,
        .rendered_frames = r->rendered_frames,
        .swapchain_rebuilds = r->swapchain_rebuilds,
        .loaded_meshes = active_meshes,
        .water_upload_bytes = r->water_upload_bytes,
        .water_stream_uploads = r->water_stream_uploads,
        .mesh_idle_waits = r->mesh_idle_waits,
        .water_upload_ms = r->water_upload_ms,
        .loaded_textures = r->texture_count,
        .missing_textures = r->missing_textures,
        .draw_calls = r->draw_calls,
        .shadow_draw_calls = r->shadow_draw_calls,
        .shadow_passes = r->shadow_passes,
        .shadow_cached_passes = r->shadow_cached_passes,
        .shadow_cascades = r->shadow_layers,
        .shadow_resolution = r->shadow_resolution,
        .missing_meshes = r->missing_meshes,
        .validation_errors = atomic_load(&r->validation_errors),
        .validation_enabled = r->validation_enabled};
}

static bool upload_texture(af_mtl_renderer *r, const char *name, const unsigned char *rgba, uint32_t width,
                           uint32_t height, bool normal) {
    if (r)
        ++r->texture_revision;
    if (!name || !name[0] || strlen(name) >= AF_MESH_NAME_CAPACITY || !rgba || !width || !height || width > 8192 ||
        height > 8192)
        return false;
    size_t index = 0;
    while (index < r->texture_count && strcmp(r->textures[index].name, name))
        ++index;
    if (index == r->texture_count && r->texture_count >= AF_SCENE_MAX_ASSETS)
        return false;
    if (!af_mtl_r_wait_idle(r))
        return false;
    af_mtl_texture *texture = af_mtl_texture_create(r->device, rgba, width, height, !normal, true, r->anisotropy);
    if (!texture)
        return false;
    if (index == r->texture_count) {
        if (r->texture_count == r->texture_capacity) {
            size_t n = r->texture_capacity ? r->texture_capacity * 2u : 8u;
            af_mtl_texture_asset *assets = realloc(r->textures, n * sizeof(*assets));
            if (!assets) {
                af_mtl_texture_destroy(texture);
                return false;
            }
            r->textures = assets;
            r->texture_capacity = n;
        }
        r->textures[index] = (af_mtl_texture_asset){0};
        snprintf(r->textures[index].name, sizeof(r->textures[index].name), "%s", name);
        ++r->texture_count;
    } else {
        af_mtl_texture_destroy(r->textures[index].texture);
    }
    r->textures[index].texture = texture;
    r->textures[index].normal = normal;
    return true;
}

bool af_mtl_r_remove_texture(af_mtl_renderer *r, const char *name) {
    if (!r || !name || !name[0])
        return false;
    ++r->texture_revision;
    for (size_t i = 0; i < r->environment_count; ++i) {
        if (!strcmp(r->environments[i].name, name)) {
            if (!af_mtl_r_wait_idle(r))
                return false;
            af_mtl_environment_asset *asset = &r->environments[i];
            af_mtl_texture_destroy(asset->specular);
            af_mtl_texture_destroy(asset->diffuse);
            af_mtl_texture_destroy(asset->sky);
            *asset = r->environments[--r->environment_count];
            return true;
        }
    }
    size_t index = 0;
    while (index < r->texture_count && strcmp(r->textures[index].name, name))
        ++index;
    if (index == r->texture_count || !af_mtl_r_wait_idle(r))
        return false;
    af_mtl_texture_destroy(r->textures[index].texture);
    memmove(&r->textures[index], &r->textures[index + 1], (r->texture_count - index - 1) * sizeof(*r->textures));
    --r->texture_count;
    return true;
}

bool af_mtl_r_upload_overlay_atlas(af_mtl_renderer *r, const unsigned char *rgba, uint32_t width, uint32_t height) {
    if (!r || !rgba || !width || !height || width > 8192 || height > 8192)
        return false;
    if (!af_mtl_r_wait_idle(r))
        return false;
    af_mtl_ui *ui = af_mtl_ui_create(r->device, r->ui_vert_fn, r->ui_frag_fn, rgba, width, height);
    if (!ui)
        return false;
    af_mtl_ui_destroy(r->ui);
    r->ui = ui;
    return true;
}

bool af_mtl_r_test_capture_region(af_mtl_renderer *r, af_vec2 position, uint32_t width, uint32_t height) {
    if (!r || !r->capture_supported || !width || !height || width > 4096 || height > 4096)
        return false;
    uint64_t bytes = (uint64_t)width * height * 8u; /* up to RGBA16F (8 bytes/pixel) */
    if (r->capture_buffer.capacity < bytes) {
        if (!af_mtl_r_wait_idle(r))
            return false;
        af_mtl_destroy_buffer(&r->capture_buffer);
        if (!af_mtl_create_buffer(r, NULL, bytes, &r->capture_buffer))
            return false;
    }
    r->capture_position = position;
    r->capture_width = width;
    r->capture_height = height;
    r->capture_pending = true;
    r->capture_ready = false;
    r->capture_motion = false;
    return true;
}

bool af_mtl_r_test_capture_motion(af_mtl_renderer *r, af_vec2 p) {
    if (!af_mtl_r_test_capture_region(r, p, 2, 1))
        return false;
    r->capture_width = 1;
    r->capture_height = 1;
    r->capture_motion = true;
    return true;
}

bool af_mtl_r_test_read_motion(af_mtl_renderer *r, af_vec4 *out) {
    if (!r || !r->capture_ready || !r->capture_motion || !out || !af_mtl_r_wait_idle(r))
        return false;
    const uint16_t *h = (const uint16_t *)[r->capture_buffer.handle contents];
    *out = (af_vec4){af_rc_f16_to_f32(h[0]), af_rc_f16_to_f32(h[1]), af_rc_f16_to_f32(h[2]), af_rc_f16_to_f32(h[3])};
    r->capture_ready = r->capture_motion = false;
    return true;
}

bool af_mtl_r_test_capture_pixel(af_mtl_renderer *r, af_vec2 position) {
    return af_mtl_r_test_capture_region(r, position, 1, 1);
}

bool af_mtl_r_test_read_pixels(af_mtl_renderer *r, unsigned char *rgba, size_t capacity, uint32_t *width,
                               uint32_t *height) {
    if (!r || !r->capture_ready || r->capture_motion || !rgba || !af_mtl_r_wait_idle(r))
        return false;
    size_t pixels = (size_t)r->capture_width * r->capture_height;
    if (capacity < pixels * 4u)
        return false;
    const unsigned char *src = (const unsigned char *)[r->capture_buffer.handle contents];
    /* Swapchain drawable is MTLPixelFormatBGRA8Unorm_sRGB -> swizzle BGRA to RGBA. */
    for (size_t i = 0; i < pixels; ++i) {
        const unsigned char *p = src + i * 4u;
        rgba[i * 4 + 0] = p[2];
        rgba[i * 4 + 1] = p[1];
        rgba[i * 4 + 2] = p[0];
        rgba[i * 4 + 3] = p[3];
    }
    if (width)
        *width = r->capture_width;
    if (height)
        *height = r->capture_height;
    r->capture_ready = false;
    return true;
}

bool af_mtl_r_test_read_pixel(af_mtl_renderer *r, unsigned char rgba[4]) {
    return af_mtl_r_test_read_pixels(r, rgba, 4, NULL, NULL);
}

bool af_mtl_r_texture_info(const af_mtl_renderer *r, const char *name, af_texture_info *out) {
    if (!r || !name || !name[0] || !out)
        return false;
    for (size_t i = 0; i < r->texture_count; ++i)
        if (!strcmp(r->textures[i].name, name)) {
            *out = af_mtl_texture_info(r->textures[i].texture);
            return true;
        }
    return false;
}

bool af_mtl_r_upload_texture(af_mtl_renderer *r, const char *name, const unsigned char *rgba, uint32_t w, uint32_t h) {
    return upload_texture(r, name, rgba, w, h, false);
}

bool af_mtl_r_upload_normal_map(af_mtl_renderer *r, const char *name, const unsigned char *rgba, uint32_t w,
                                uint32_t h) {
    return upload_texture(r, name, rgba, w, h, true);
}

void af_mtl_r_set_interpolation(af_mtl_renderer *r, const af_scene *previous, float alpha) {
    r->previous_scene = previous;
    r->interpolation_alpha = alpha;
}

bool af_mtl_r_commit_asset(af_mtl_renderer *r, const char *staged, const char *target, bool mesh) {
    if (!r || !staged || !staged[0] || !target || !target[0] || strlen(target) >= AF_MESH_NAME_CAPACITY ||
        !strcmp(staged, target) || !af_mtl_r_wait_idle(r))
        return false;
    if (!mesh) {
        for (size_t from = 0; from < r->environment_count; ++from) {
            if (!strcmp(r->environments[from].name, staged)) {
                size_t to = 0;
                while (to < r->environment_count && strcmp(r->environments[to].name, target))
                    ++to;
                if (to < r->environment_count) {
                    af_mtl_environment_asset swap = r->environments[to];
                    r->environments[to] = r->environments[from];
                    r->environments[from] = swap;
                    snprintf(r->environments[from].name, AF_MESH_NAME_CAPACITY, "%s", staged);
                } else {
                    to = from;
                }
                snprintf(r->environments[to].name, AF_MESH_NAME_CAPACITY, "%s", target);
                ++r->texture_revision;
                return true;
            }
        }
    }
    if (mesh) {
        size_t from = af_mtl_find_mesh_index(r, staged);
        size_t to = af_mtl_find_mesh_index(r, target);
        if (from == r->mesh_count)
            return false;
        if (to < r->mesh_count) {
            af_mtl_gpu_mesh old = r->meshes[to].gpu;
            r->meshes[to].gpu = r->meshes[from].gpu;
            r->meshes[from].gpu = old;
        } else {
            snprintf(r->meshes[from].name, sizeof(r->meshes[from].name), "%s", target);
        }
        ++r->mesh_revision;
    } else {
        size_t from = 0, to = 0;
        while (from < r->texture_count && strcmp(r->textures[from].name, staged))
            ++from;
        while (to < r->texture_count && strcmp(r->textures[to].name, target))
            ++to;
        if (from == r->texture_count)
            return false;
        if (to < r->texture_count) {
            af_mtl_texture *old = r->textures[to].texture;
            bool normal = r->textures[to].normal;
            r->textures[to].texture = r->textures[from].texture;
            r->textures[to].normal = r->textures[from].normal;
            r->textures[from].texture = old;
            r->textures[from].normal = normal;
        } else {
            snprintf(r->textures[from].name, sizeof(r->textures[from].name), "%s", target);
        }
        ++r->texture_revision;
    }
    return true;
}

bool af_mtl_r_upload_overlay_texture(af_mtl_renderer *r, uint32_t id, const unsigned char *rgba, uint32_t width,
                                     uint32_t height) {
    return r && id >= 2 && rgba && width && height && width <= 8192 && height <= 8192 &&
           af_mtl_ui_upload_texture(r->ui, id, rgba, width, height);
}

bool af_mtl_r_remove_overlay_texture(af_mtl_renderer *r, uint32_t id) {
    return r && af_mtl_ui_remove_texture(r->ui, id);
}

bool af_mtl_r_upload_environment(af_mtl_renderer *r, const char *name, const float *rgba, uint32_t width,
                                 uint32_t height) {
    if (!r || !name || !name[0] || strlen(name) >= AF_MESH_NAME_CAPACITY)
        return false;
    size_t index = 0;
    while (index < r->environment_count && strcmp(r->environments[index].name, name))
        ++index;
    if (index == 16)
        return false;
    af_ibl_images images;
    if (!af_ibl_bake_hdr(rgba, width, height, &images))
        return false;
    bool result =
        af_mtl_r_upload_environment_baked(r, name, &(af_hdr_file){(float *)rgba, width, height}, &images);
    af_ibl_free(&images);
    return result;
}

bool af_mtl_r_upload_environment_baked(af_mtl_renderer *r, const char *name, const af_hdr_file *source,
                                       const af_ibl_images *baked) {
    size_t index = 0;
    while (index < r->environment_count && strcmp(r->environments[index].name, name))
        ++index;
    if (index == 16)
        return false;
    af_mtl_environment_asset asset = {0};
    snprintf(asset.name, sizeof(asset.name), "%s", name);
    asset.specular =
        af_mtl_texture_create_hdr(r->device, baked->specular, AF_IBL_WIDTH, AF_IBL_HEIGHT, AF_IBL_LEVELS, true);
    asset.diffuse = af_mtl_texture_create_hdr(r->device, baked->diffuse, AF_IBL_DIFFUSE_WIDTH, AF_IBL_DIFFUSE_HEIGHT, 1,
                                              true);
    asset.sky = af_mtl_texture_create_hdr(r->device, source->rgba, source->width, source->height, 1, true);
    if (!asset.specular || !asset.diffuse || !asset.sky || !af_mtl_r_wait_idle(r)) {
        af_mtl_texture_destroy(asset.specular);
        af_mtl_texture_destroy(asset.diffuse);
        af_mtl_texture_destroy(asset.sky);
        return false;
    }
    if (index < r->environment_count) {
        af_mtl_texture_destroy(r->environments[index].specular);
        af_mtl_texture_destroy(r->environments[index].diffuse);
        af_mtl_texture_destroy(r->environments[index].sky);
    } else {
        ++r->environment_count;
    }
    r->environments[index] = asset;
    return true;
}

id<MTLRenderPipelineState> af_mtl_material_pipeline(const af_mtl_renderer *r, const char *shader_name, bool blend) {
    if (r && shader_name && shader_name[0]) {
        for (size_t i = 0; i < r->custom_shader_count; ++i) {
            if (!strcmp(r->custom_shaders[i].name, shader_name)) {
                id<MTLRenderPipelineState> custom =
                    blend ? r->custom_shaders[i].blend_pipeline : r->custom_shaders[i].opaque_pipeline;
                if (custom)
                    return custom;
                break;
            }
        }
    }
    return blend ? r->swapchain.blend_pipeline : r->swapchain.pipeline;
}

bool af_mtl_r_upload_material_shader(af_mtl_renderer *r, const char *name, const uint32_t *spirv, size_t word_count) {
    if (!r || !name || !name[0] || strlen(name) >= AF_MESH_NAME_CAPACITY || !spirv || word_count < 5 ||
        word_count > (4u * 1024u * 1024u) || spirv[0] != 0x07230203u)
        return false;
    size_t index = 0;
    while (index < r->custom_shader_count && strcmp(r->custom_shaders[index].name, name))
        ++index;
    if (index == r->custom_shader_count && r->custom_shader_count >= AF_SCENE_MAX_ASSETS)
        return false;

    char *msl_src = NULL;
    char spv_err[512] = {0};
    if (!af_spv_to_msl(spirv, word_count, AF_MSL_STAGE_FRAGMENT, AF_MSL_PROFILE_SCENE, "af_custom_frag_main", false,
                       &msl_src, spv_err, sizeof(spv_err))) {
        af_log("error", "Metal custom material shader '%s' SPIR-V translation failed: %s", name, spv_err);
        return false;
    }

    id<MTLFunction> frag_fn = nil;
    @autoreleasepool {
        NSString *source = [NSString stringWithUTF8String:msl_src];
        MTLCompileOptions *opts = [[MTLCompileOptions alloc] init];
        if (@available(macOS 13.0, *))
            opts.languageVersion = MTLLanguageVersion3_0;
        NSError *err = nil;
        id<MTLLibrary> lib = [r->device newLibraryWithSource:source options:opts error:&err];
        [opts release];
        if (!lib) {
            af_log("error", "Metal custom material shader '%s' compilation failed: %s", name,
                   err ? err.localizedDescription.UTF8String : "unknown");
            free(msl_src);
            return false;
        }
        frag_fn = [lib newFunctionWithName:@"af_custom_frag_main"];
        [lib release];
    }
    free(msl_src);
    if (!frag_fn)
        return false;

    uint32_t *copy = malloc(word_count * sizeof(uint32_t));
    if (!copy) {
        AF_MTL_RELEASE(frag_fn);
        return false;
    }
    memcpy(copy, spirv, word_count * sizeof(uint32_t));

    id<MTLRenderPipelineState> opaque = nil, blend = nil;
    if (r->swapchain.post && !af_mtl_build_mesh_pipelines(r, frag_fn, &opaque, &blend)) {
        AF_MTL_RELEASE(frag_fn);
        free(copy);
        return false;
    }

    if (!af_mtl_r_wait_idle(r)) {
        AF_MTL_RELEASE(opaque);
        AF_MTL_RELEASE(blend);
        AF_MTL_RELEASE(frag_fn);
        free(copy);
        return false;
    }

    if (index == r->custom_shader_count) {
        if (r->custom_shader_count == r->custom_shader_capacity) {
            size_t cap = r->custom_shader_capacity ? r->custom_shader_capacity * 2u : 4u;
            af_mtl_material_shader_asset *grown = realloc(r->custom_shaders, cap * sizeof(*grown));
            if (!grown) {
                AF_MTL_RELEASE(opaque);
                AF_MTL_RELEASE(blend);
                AF_MTL_RELEASE(frag_fn);
                free(copy);
                return false;
            }
            r->custom_shaders = grown;
            r->custom_shader_capacity = cap;
        }
        r->custom_shaders[index] = (af_mtl_material_shader_asset){0};
        snprintf(r->custom_shaders[index].name, sizeof(r->custom_shaders[index].name), "%s", name);
        ++r->custom_shader_count;
    } else {
        AF_MTL_RELEASE(r->custom_shaders[index].opaque_pipeline);
        AF_MTL_RELEASE(r->custom_shaders[index].blend_pipeline);
        AF_MTL_RELEASE(r->custom_shaders[index].fragment_fn);
        free(r->custom_shaders[index].spirv);
    }

    r->custom_shaders[index].spirv = copy;
    r->custom_shaders[index].word_count = word_count;
    r->custom_shaders[index].fragment_fn = frag_fn;
    r->custom_shaders[index].opaque_pipeline = opaque;
    r->custom_shaders[index].blend_pipeline = blend;
    return true;
}

bool af_mtl_r_remove_material_shader(af_mtl_renderer *r, const char *name) {
    if (!r || !name || !name[0])
        return false;
    size_t index = 0;
    while (index < r->custom_shader_count && strcmp(r->custom_shaders[index].name, name))
        ++index;
    if (index == r->custom_shader_count || !af_mtl_r_wait_idle(r))
        return false;
    AF_MTL_RELEASE(r->custom_shaders[index].opaque_pipeline);
    AF_MTL_RELEASE(r->custom_shaders[index].blend_pipeline);
    AF_MTL_RELEASE(r->custom_shaders[index].fragment_fn);
    free(r->custom_shaders[index].spirv);
    memmove(&r->custom_shaders[index], &r->custom_shaders[index + 1],
            (r->custom_shader_count - index - 1) * sizeof(*r->custom_shaders));
    --r->custom_shader_count;
    return true;
}
