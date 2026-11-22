/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "renderer_internal.h"

void af_r_update_environment_descriptors(af_vk_renderer *renderer) {
    af_vk_texture *specular = renderer->environment, *diffuse = renderer->irradiance, *sky = renderer->environment;
    for (size_t i = 0; i < renderer->environment_count; ++i)
        if (!strcmp(renderer->active_environment, renderer->environments[i].name)) {
            specular = renderer->environments[i].specular;
            diffuse = renderer->environments[i].diffuse;
            sky = renderer->environments[i].sky;
            break;
        }
    VkDescriptorImageInfo images[] = {af_vk_texture_image(specular), af_vk_texture_image(diffuse),
                                      af_vk_texture_image(renderer->brdf), af_vk_texture_image(sky)};
    for (unsigned f = 0; f < AF_FRAMES_IN_FLIGHT; ++f) {
        VkWriteDescriptorSet writes[5];
        VkDescriptorImageInfo local = af_vk_shadows_image(renderer->local_shadows, f);
        writes[4] = (VkWriteDescriptorSet){.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                           .dstSet = renderer->frames[f].descriptor,
                                           .dstBinding = 1,
                                           .descriptorCount = 1,
                                           .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                           .pImageInfo = &local};
        for (uint32_t i = 0; i < 4; ++i)
            writes[i] = (VkWriteDescriptorSet){.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                               .dstSet = renderer->frames[f].descriptor,
                                               .dstBinding = i + 2,
                                               .descriptorCount = 1,
                                               .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                               .pImageInfo = &images[i]};
        vkUpdateDescriptorSets(renderer->device, 5, writes, 0, NULL);
    }
}
bool af_r_create_environment(af_vk_renderer *renderer, uint32_t profile) {
    af_ibl_images images;
    if (!af_ibl_bake(profile, &images))
        return false;
    af_vk_texture *environment = af_vk_texture_create_hdr(
        renderer->physical_device, renderer->device, renderer->allocator,
        images.specular, AF_IBL_WIDTH, AF_IBL_HEIGHT, AF_IBL_LEVELS, true);
    af_vk_texture *irradiance = af_vk_texture_create_hdr(
        renderer->physical_device, renderer->device, renderer->allocator,
        images.diffuse, AF_IBL_DIFFUSE_WIDTH, AF_IBL_DIFFUSE_HEIGHT, 1, true);
    af_vk_texture *brdf =
        af_vk_texture_create_hdr(renderer->physical_device, renderer->device, renderer->allocator,
                                 images.brdf, AF_IBL_LUT_SIZE, AF_IBL_LUT_SIZE, 1, false);
    af_ibl_free(&images);
    if (!environment || !irradiance || !brdf) {
        af_vk_texture_destroy(environment);
        af_vk_texture_destroy(irradiance);
        af_vk_texture_destroy(brdf);
        return false;
    }
    if (!af_vk_r_wait_idle(renderer)) {
        af_vk_texture_destroy(environment);
        af_vk_texture_destroy(irradiance);
        af_vk_texture_destroy(brdf);
        return false;
    }
    af_vk_texture_destroy(renderer->environment);
    af_vk_texture_destroy(renderer->irradiance);
    af_vk_texture_destroy(renderer->brdf);
    renderer->environment = environment;
    renderer->irradiance = irradiance;
    renderer->brdf = brdf;
    renderer->environment_profile = profile;
    if (renderer->frame_pool)
        af_r_update_environment_descriptors(renderer);
    return true;
}

bool af_vk_r_upload_overlay_atlas(af_vk_renderer *renderer, const unsigned char *rgba, uint32_t width,
                                      uint32_t height) {
    if (!rgba || !width || !height || width > 8192 || height > 8192 || (uint64_t)width * height * 4 > SIZE_MAX ||
        !af_vk_r_wait_idle(renderer))
        return false;
    af_vk_ui *ui = af_vk_ui_create(renderer->physical_device, renderer->device, renderer->allocator, rgba, width, height);
    if (!ui)
        return false;
    if (!af_vk_ui_create_pipeline(ui, af_vk_post_output_pass(renderer->swapchain.post))) {
        af_vk_ui_destroy(ui);
        return false;
    }
    af_vk_ui_destroy(renderer->ui);
    renderer->ui = ui;
    return true;
}


af_temporal_test_stats af_vk_r_test_temporal(const af_vk_renderer *r) {
    return r ? (af_temporal_test_stats){r->temporal_frame,          r->temporal_resets,  r->swapchain.extent.width,
                                        r->swapchain.extent.height, r->temporal_objects, r->temporal_skins,
                                        r->temporal_supported,      r->temporal_valid,   r->temporal_jitter}
             : (af_temporal_test_stats){0};
}
void af_vk_r_reset_temporal(af_vk_renderer *r) {
    if (r)
        r->temporal_valid = false;
}

bool af_vk_r_set_debug_view(af_vk_renderer *r, af_debug_view view) {
    if (!r || view < AF_DEBUG_SHADED || view >= AF_DEBUG_COUNT || (view != AF_DEBUG_SHADED && !r->temporal_supported))
        return false;
    if (r->debug_view == view)
        return true;
    r->debug_view = view;
    r->temporal_valid = false;
    return true;
}
void af_vk_r_set_visibility_culling(af_vk_renderer *r, bool enabled) {
    if (r)
        r->culling_disabled = !enabled;
}
void af_vk_r_set_prop_instancing(af_vk_renderer *r, bool enabled) {
    if (r)
        r->prop_instancing_disabled = !enabled;
}
bool af_vk_r_set_render_budget(af_vk_renderer *r, double ms, uint64_t bytes) {
    if (!r || !isfinite(ms) || ms <= 0 || ms > 1000 || bytes < 1024u * 1024u)
        return false;
    r->gpu_budget_ms = ms;
    r->gpu_budget_bytes = bytes;
    return true;
}
af_engine_stats af_vk_r_stats(const af_vk_renderer *renderer) {
    size_t active_meshes = 0;
    for (size_t i = 0; i < renderer->mesh_count; ++i)
        active_meshes += renderer->meshes[i].name[0] && !renderer->meshes[i].retired;
    uint64_t texture_bytes = 0;
    uint32_t mip_levels = 0;
    for (size_t i = 0; i < renderer->texture_count; ++i) {
        af_texture_info info = af_vk_texture_info(renderer->textures[i].texture);
        texture_bytes += info.allocation_bytes;
        mip_levels += info.mip_levels;
    }
    uint64_t mesh_bytes = 0, other = 0;
    for (size_t i = 0; i < renderer->mesh_count; ++i) {
        const af_mesh_asset *m = &renderer->meshes[i];
        mesh_bytes += m->gpu.vertices.allocation_bytes + m->gpu.indices.allocation_bytes;
        if (m->gpu.residency)
            for (unsigned page = 0; page < AF_CLUSTER_RESIDENT_PAGES; ++page) {
                const af_gpu_mesh *gpu = &m->gpu.residency->pages[page].gpu;
                mesh_bytes += gpu->vertices.allocation_bytes + gpu->indices.allocation_bytes;
            }
        for (unsigned f = 0; f < AF_FRAMES_IN_FLIGHT; ++f)
            mesh_bytes += m->streams[f].gpu.vertices.allocation_bytes + m->streams[f].gpu.indices.allocation_bytes;
    }
    for (unsigned f = 0; f < AF_FRAMES_IN_FLIGHT; ++f)
        other += renderer->frames[f].uniform.allocation_bytes + renderer->frames[f].skin_buffer.allocation_bytes +
                 renderer->frames[f].motion_buffer.allocation_bytes + renderer->frames[f].morph_buffer.allocation_bytes +
                 renderer->frames[f].light_buffer.allocation_bytes + renderer->frames[f].light_tile_buffer.allocation_bytes +
                 renderer->frames[f].cluster_input.allocation_bytes + renderer->frames[f].cluster_transforms.allocation_bytes +
                 renderer->frames[f].cluster_output.allocation_bytes;
    for (uint32_t i = 0; i < renderer->swapchain.image_count; ++i)
        other += renderer->swapchain.depths[i].allocation_bytes;
    af_vk_texture *fixed[] = {renderer->white, renderer->flat_normal, renderer->environment, renderer->irradiance,
                              renderer->brdf};
    for (unsigned i = 0; i < 5; ++i)
        if (fixed[i])
            other += af_vk_texture_info(fixed[i]).allocation_bytes;
    for (size_t i = 0; i < renderer->environment_count; ++i) {
        other += af_vk_texture_info(renderer->environments[i].sky).allocation_bytes +
                 af_vk_texture_info(renderer->environments[i].specular).allocation_bytes +
                 af_vk_texture_info(renderer->environments[i].diffuse).allocation_bytes;
    }
    af_ray_stats ray = af_vk_ray_query_stats(renderer->rays);
    other += ray.bytes;
    af_vk_allocator_stats alloc_stats = af_vk_allocator_get_stats(renderer->allocator);
    uint64_t post = af_vk_post_allocation_bytes(renderer->swapchain.post),
             shadows = af_vk_shadows_allocation_bytes(renderer->shadows) +
                       af_vk_shadows_allocation_bytes(renderer->local_shadows),
             refraction = af_vk_refraction_allocation_bytes(renderer->swapchain.refraction),
             tracked = post + shadows + refraction + mesh_bytes + texture_bytes + other;
    return (af_engine_stats){
        .active_backend = AF_RENDER_BACKEND_VULKAN,
        .vk_allocations = alloc_stats.vk_allocations,
        .vk_staged_uploads = alloc_stats.staged_uploads,
        .ray_query_supported = renderer->ray_supported,
        .ray_traced_shadows_active =
            (renderer->swapchain.resource_config & AF_RESOURCE_RAY_SHADOWS) != 0 && !renderer->ray_failed,
        .ray_traced_water_reflections_active =
            (renderer->swapchain.resource_config & AF_RESOURCE_RAY_REFLECTIONS) != 0 && !renderer->ray_failed,
        .ray_traced_material_effects_active =
            (renderer->swapchain.resource_config & AF_RESOURCE_RAY_MATERIALS) != 0 && !renderer->ray_failed,
        .ray_traced_gi_active =
            (renderer->swapchain.resource_config & AF_RESOURCE_RAY_GI) != 0 && !renderer->ray_failed,
        .ray_tracing_failed = renderer->ray_failed,
        .ray_tracing_bytes = ray.bytes,
        .ray_blas_builds = ray.blas_builds,
        .ray_tlas_builds = ray.tlas_builds,
        .ray_instances = ray.instances,
        .ray_meshes = ray.meshes,
        .ray_raster_casters = renderer->ray_raster_casters,
        .gpu_ray_build_ms = ray.gpu_build_ms,
        .post_bytes = post,
        .shadow_bytes = shadows,
        .refraction_bytes = refraction,
        .mesh_bytes = mesh_bytes,
        .tracked_gpu_bytes = tracked,
        .gpu_budget_ms = renderer->gpu_budget_ms,
        .gpu_budget_bytes = renderer->gpu_budget_bytes,
        .gpu_time_over_budget = renderer->timing_valid && renderer->gpu_ms[0] > renderer->gpu_budget_ms,
        .gpu_memory_over_budget = tracked > renderer->gpu_budget_bytes,
        .debug_view = renderer->debug_view,
        .effective_aa = (af_anti_aliasing)(renderer->swapchain.resource_config & AF_RESOURCE_AA_MASK),
        .visible_objects = renderer->visible_objects,
        .culled_objects = renderer->culled_objects,
        .gpu_timing_supported = renderer->timestamp_bits != 0,
        .gpu_timing_valid = renderer->timing_valid,
        .gpu_frame_ms = renderer->gpu_ms[0],
        .gpu_shadow_ms = renderer->gpu_ms[1],
        .gpu_scene_ms = renderer->gpu_ms[2],
        .gpu_post_ms = renderer->gpu_ms[3],
        .gpu_overlay_ms = renderer->gpu_ms[4],
        .cpu_fence_ms = renderer->cpu_fence_ms,
        .cpu_acquire_ms = renderer->cpu_acquire_ms,
        .cpu_record_ms = renderer->cpu_record_ms,
        .hdr_enabled = renderer->swapchain.post != NULL,
        .local_shadow_lights = renderer->local_shadow_lights,
        .local_shadow_maps = renderer->local_shadow_lights ? renderer->local_shadow_layers : 0,
        .local_shadow_resolution = renderer->local_shadow_resolution,
        .local_shadow_overflow = renderer->local_shadow_overflow,
        .texture_bytes = texture_bytes,
        .texture_mip_levels = mip_levels,
        .texture_anisotropy = renderer->anisotropy,
        .rendered_frames = renderer->rendered_frames,
        .swapchain_rebuilds = renderer->swapchain_rebuilds,
        .loaded_meshes = active_meshes,
        .water_upload_bytes = renderer->water_upload_bytes,
        .water_stream_uploads = renderer->water_stream_uploads,
        .mesh_idle_waits = renderer->mesh_idle_waits,
        .water_upload_ms = renderer->water_upload_ms,
        .loaded_textures = renderer->texture_count,
        .missing_textures = renderer->missing_textures,
        .draw_calls = renderer->draw_calls,
        .shadow_draw_calls = renderer->shadow_draw_calls,
        .shadow_passes = renderer->shadow_passes,
        .shadow_cached_passes = renderer->shadow_cached_passes,
        .shadow_cascades = renderer->shadow_layers,
        .shadow_resolution = renderer->shadow_resolution,
        .missing_meshes = renderer->missing_meshes,
        .validation_errors = atomic_load(&renderer->validation_errors),
        .validation_enabled = renderer->validation_enabled};
}

static bool upload_texture(af_vk_renderer *renderer, const char *name, const unsigned char *rgba, uint32_t width,
                           uint32_t height, bool normal) {
    if (renderer)
        ++renderer->texture_revision;
    if (!name || !name[0] || strlen(name) >= AF_MESH_NAME_CAPACITY || !rgba || !width || !height || width > 8192 ||
        height > 8192)
        return false;
    size_t index = 0;
    while (index < renderer->texture_count && strcmp(renderer->textures[index].name, name))
        ++index;
    if (index == renderer->texture_count && renderer->texture_count >= AF_SCENE_MAX_ASSETS)
        return false;
    if (!af_vk_r_wait_idle(renderer))
        return false;
    af_vk_texture *texture =
        af_vk_texture_create(renderer->physical_device, renderer->device, renderer->allocator, rgba, width, height,
                             !normal, true, renderer->anisotropy);
    if (!texture)
        return false;
    if (index == renderer->texture_count) {
        if (renderer->texture_count == renderer->texture_capacity) {
            size_t n = renderer->texture_capacity ? renderer->texture_capacity * 2 : 8;
            af_texture_asset *assets = realloc(renderer->textures, n * sizeof(*assets));
            if (!assets) {
                af_vk_texture_destroy(texture);
                return false;
            }
            renderer->textures = assets;
            renderer->texture_capacity = n;
        }
        renderer->textures[index] = (af_texture_asset){0};
        snprintf(renderer->textures[index].name, sizeof(renderer->textures[index].name), "%s", name);
        ++renderer->texture_count;
    } else
        af_vk_texture_destroy(renderer->textures[index].texture);
    renderer->textures[index].texture = texture;
    renderer->textures[index].normal = normal;
    return true;
}

bool af_vk_r_remove_texture(af_vk_renderer *renderer, const char *name) {
    if (renderer)
        ++renderer->texture_revision;
    for (size_t i = 0; i < renderer->environment_count; ++i)
        if (!strcmp(renderer->environments[i].name, name)) {
            if (!af_vk_r_wait_idle(renderer))
                return false;
            af_environment_asset *asset = &renderer->environments[i];
            af_vk_texture_destroy(asset->specular);
            af_vk_texture_destroy(asset->diffuse);
            af_vk_texture_destroy(asset->sky);
            *asset = renderer->environments[--renderer->environment_count];
            af_r_update_environment_descriptors(renderer);
            return true;
        }

    if (!name)
        return false;
    size_t i = 0;
    while (i < renderer->texture_count && strcmp(renderer->textures[i].name, name))
        ++i;
    if (i == renderer->texture_count || !af_vk_r_wait_idle(renderer))
        return false;
    af_vk_texture_destroy(renderer->textures[i].texture);
    memmove(&renderer->textures[i], &renderer->textures[i + 1],
            (renderer->texture_count - i - 1) * sizeof(*renderer->textures));
    --renderer->texture_count;
    return true;
}

bool af_vk_r_test_capture_region(af_vk_renderer *renderer, af_vec2 position, uint32_t width, uint32_t height) {
    if (!renderer || !renderer->capture_supported || renderer->capture_pending || !isfinite(position.x) ||
        !isfinite(position.y) || !width || !height || width > 4096 || height > 4096)
        return false;
    size_t bytes = (size_t)width * height * 4;
    if (renderer->capture_buffer.capacity < bytes) {
        if (!af_vk_r_wait_idle(renderer))
            return false;
        af_r_destroy_buffer(renderer, &renderer->capture_buffer);
        unsigned char *zero = calloc(bytes, 1);
        if (!zero)
            return false;
        bool success =
            af_r_upload_buffer(renderer, zero, bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, &renderer->capture_buffer);
        free(zero);
        if (!success)
            return false;
        renderer->capture_buffer.capacity = bytes;
    }
    renderer->capture_motion = false;
    renderer->capture_extent = (VkExtent2D){width, height};
    renderer->capture_position = position;
    renderer->capture_pending = true;
    renderer->capture_ready = false;
    return true;
}
bool af_vk_r_test_capture_motion(af_vk_renderer *r, af_vec2 p) {
    if (!af_vk_r_test_capture_region(r, p, 2, 1))
        return false;
    r->capture_extent.width = 1;
    r->capture_motion = true;
    return true;
}
static float decode_half(uint16_t bits) {
    unsigned exponent = (bits >> 10u) & 31u, mantissa = bits & 1023u;
    float value = exponent == 0    ? ldexpf((float)mantissa, -24)
                  : exponent == 31 ? (mantissa ? NAN : INFINITY)
                                   : ldexpf(1 + (float)mantissa / 1024, (int)exponent - 15);
    return (bits & 32768u) ? -value : value;
}
bool af_vk_r_test_read_motion(af_vk_renderer *r, af_vec4 *out) {
    if (!r || !out || !r->capture_ready || r->capture_format != VK_FORMAT_R16G16B16A16_SFLOAT ||
        !af_vk_r_wait_idle(r))
        return false;
    void *data = r->capture_buffer.alloc.mapped;
    if (!data)
        return false;
    bool success = af_vk_invalidate(r->allocator, &r->capture_buffer.alloc, 0, VK_WHOLE_SIZE);
    if (success) {
        uint16_t values[4];
        memcpy(values, data, sizeof(values));
        *out =
            (af_vec4){decode_half(values[0]), decode_half(values[1]), decode_half(values[2]), decode_half(values[3])};
    }
    r->capture_ready = false;
    return success;
}
bool af_vk_r_test_capture_pixel(af_vk_renderer *renderer, af_vec2 position) {
    return af_vk_r_test_capture_region(renderer, position, 1, 1);
}
bool af_vk_r_test_read_pixels(af_vk_renderer *renderer, unsigned char *rgba, size_t capacity, uint32_t *width,
                                  uint32_t *height) {
    if (!renderer || !rgba || !renderer->capture_ready || renderer->capture_format == VK_FORMAT_R16G16B16A16_SFLOAT ||
        capacity < (size_t)renderer->capture_extent.width * renderer->capture_extent.height * 4 ||
        !af_vk_r_wait_idle(renderer))
        return false;
    void *data = renderer->capture_buffer.alloc.mapped;
    if (!data)
        return false;
    bool success = af_vk_invalidate(renderer->allocator, &renderer->capture_buffer.alloc, 0, VK_WHOLE_SIZE);
    if (success) {
        const unsigned char *p = data;
        bool bgra =
            renderer->capture_format == VK_FORMAT_B8G8R8A8_SRGB || renderer->capture_format == VK_FORMAT_B8G8R8A8_UNORM;
        size_t count = (size_t)renderer->capture_extent.width * renderer->capture_extent.height;
        size_t max_pixels = capacity / 4;
        if (count > max_pixels)
            count = max_pixels;
        for (size_t i = 0; i < count; ++i) {
            rgba[4 * i] = p[4 * i + (bgra ? 2u : 0u)];
            rgba[4 * i + 1] = p[4 * i + 1];
            rgba[4 * i + 2] = p[4 * i + (bgra ? 0u : 2u)];
            rgba[4 * i + 3] = p[4 * i + 3];
        }
        if (width)
            *width = renderer->capture_extent.width;
        if (height)
            *height = renderer->capture_extent.height;
    }
    renderer->capture_ready = false;
    return success;
}
bool af_vk_r_test_read_pixel(af_vk_renderer *renderer, unsigned char rgba[4]) {
    return af_vk_r_test_read_pixels(renderer, rgba, 4, NULL, NULL);
}

bool af_vk_r_texture_info(const af_vk_renderer *renderer, const char *name, af_texture_info *out) {
    if (!name || !out)
        return false;
    for (size_t i = 0; i < renderer->texture_count; ++i)
        if (!strcmp(renderer->textures[i].name, name)) {
            *out = af_vk_texture_info(renderer->textures[i].texture);
            return true;
        }
    return false;
}

bool af_vk_r_upload_texture(af_vk_renderer *r, const char *name, const unsigned char *rgba, uint32_t w, uint32_t h) {
    return upload_texture(r, name, rgba, w, h, false);
}
bool af_vk_r_upload_normal_map(af_vk_renderer *r, const char *name, const unsigned char *rgba, uint32_t w,
                                   uint32_t h) {
    return upload_texture(r, name, rgba, w, h, true);
}

void af_vk_r_set_interpolation(af_vk_renderer *renderer, const af_scene *previous, float alpha) {
    renderer->previous_scene = previous;
    renderer->interpolation_alpha = alpha;
}

bool af_vk_r_commit_asset(af_vk_renderer *renderer, const char *staged, const char *target, bool mesh) {
    if (!renderer || !staged || !target || !target[0] || strlen(target) >= AF_MESH_NAME_CAPACITY)
        return false;
    if (!mesh)
        for (size_t from = 0; from < renderer->environment_count; ++from)
            if (!strcmp(renderer->environments[from].name, staged)) {
                if (!af_vk_r_wait_idle(renderer))
                    return false;
                size_t to = 0;
                while (to < renderer->environment_count && strcmp(renderer->environments[to].name, target))
                    ++to;
                if (to < renderer->environment_count) {
                    af_environment_asset swap = renderer->environments[to];
                    renderer->environments[to] = renderer->environments[from];
                    renderer->environments[from] = swap;
                    snprintf(renderer->environments[from].name, AF_MESH_NAME_CAPACITY, "%s", staged);
                } else
                    to = from;
                snprintf(renderer->environments[to].name, AF_MESH_NAME_CAPACITY, "%s", target);
                af_r_update_environment_descriptors(renderer);
                return true;
            }
    if (mesh) {
        size_t from = af_r_find_mesh_index(renderer, staged), to = af_r_find_mesh_index(renderer, target);
        if (from == renderer->mesh_count)
            return false;
        ++renderer->mesh_revision;
        if (to < renderer->mesh_count) {
            af_gpu_mesh old = renderer->meshes[to].gpu;
            renderer->meshes[to].gpu = renderer->meshes[from].gpu;
            renderer->meshes[from].gpu = old;
        } else
            snprintf(renderer->meshes[from].name, sizeof(renderer->meshes[from].name), "%s", target);
    } else {
        size_t from = 0, to = 0;
        while (from < renderer->texture_count && strcmp(renderer->textures[from].name, staged))
            ++from;
        while (to < renderer->texture_count && strcmp(renderer->textures[to].name, target))
            ++to;
        if (from == renderer->texture_count)
            return false;
        if (to < renderer->texture_count) {
            af_vk_texture *old = renderer->textures[to].texture;
            bool normal = renderer->textures[to].normal;
            renderer->textures[to].texture = renderer->textures[from].texture;
            renderer->textures[to].normal = renderer->textures[from].normal;
            renderer->textures[from].texture = old;
            renderer->textures[from].normal = normal;
        } else
            snprintf(renderer->textures[from].name, sizeof(renderer->textures[from].name), "%s", target);
    }
    return true;
}

bool af_vk_r_upload_overlay_texture(af_vk_renderer *renderer, uint32_t id, const unsigned char *rgba, uint32_t width,
                                        uint32_t height) {
    return renderer && id >= 2 && rgba && width && height && width <= 8192 && height <= 8192 &&
           af_vk_ui_upload_texture(renderer->ui, renderer->graphics_queue, renderer->graphics_family, id, rgba, width,
                                   height);
}
bool af_vk_r_remove_overlay_texture(af_vk_renderer *renderer, uint32_t id) {
    return renderer && af_vk_ui_remove_texture(renderer->ui, id);
}

bool af_vk_r_upload_environment(af_vk_renderer *renderer, const char *name, const float *rgba, uint32_t width,
                                    uint32_t height) {
    if (!renderer || !name || !name[0] || strlen(name) >= AF_MESH_NAME_CAPACITY)
        return false;
    size_t index = 0;
    while (index < renderer->environment_count && strcmp(renderer->environments[index].name, name))
        ++index;
    if (index == 16)
        return false;
    af_ibl_images images;
    if (!af_ibl_bake_hdr(rgba, width, height, &images))
        return false;
    bool result =
        af_vk_r_upload_environment_baked(renderer, name, &(af_hdr_file){(float *)rgba, width, height}, &images);
    af_ibl_free(&images);
    return result;
}
bool af_vk_r_upload_environment_baked(af_vk_renderer *renderer, const char *name, const af_hdr_file *source,
                                          const af_ibl_images *baked) {
    size_t index = 0;
    while (index < renderer->environment_count && strcmp(renderer->environments[index].name, name))
        ++index;
    if (index == 16)
        return false;
    af_ibl_images images = *baked;
    const float *rgba = source->rgba;
    uint32_t width = source->width, height = source->height;
    af_environment_asset asset = {0};
    snprintf(asset.name, sizeof(asset.name), "%s", name);
    asset.specular = af_vk_texture_create_hdr(renderer->physical_device, renderer->device, renderer->allocator,
                                              images.specular, AF_IBL_WIDTH, AF_IBL_HEIGHT, AF_IBL_LEVELS, true);
    asset.diffuse = af_vk_texture_create_hdr(renderer->physical_device, renderer->device, renderer->allocator,
                                             images.diffuse, AF_IBL_DIFFUSE_WIDTH, AF_IBL_DIFFUSE_HEIGHT, 1, true);
    asset.sky = af_vk_texture_create_hdr(renderer->physical_device, renderer->device, renderer->allocator, rgba, width,
                                         height, 1, true);
    if (!asset.specular || !asset.diffuse || !asset.sky || !af_vk_r_wait_idle(renderer)) {
        af_vk_texture_destroy(asset.specular);
        af_vk_texture_destroy(asset.diffuse);
        af_vk_texture_destroy(asset.sky);
        return false;
    }
    if (index < renderer->environment_count) {
        af_vk_texture_destroy(renderer->environments[index].specular);
        af_vk_texture_destroy(renderer->environments[index].diffuse);
        af_vk_texture_destroy(renderer->environments[index].sky);
    } else
        ++renderer->environment_count;
    renderer->environments[index] = asset;
    af_r_update_environment_descriptors(renderer);
    return true;
}

VkPipeline af_r_material_pipeline(const af_vk_renderer *renderer, const char *shader_name, bool blend) {
    if (renderer && shader_name && shader_name[0]) {
        for (size_t i = 0; i < renderer->custom_shader_count; ++i) {
            if (!strcmp(renderer->custom_shaders[i].name, shader_name)) {
                VkPipeline custom =
                    blend ? renderer->custom_shaders[i].blend_pipeline : renderer->custom_shaders[i].opaque_pipeline;
                if (custom)
                    return custom;
                break;
            }
        }
    }
    return blend ? renderer->swapchain.blend_pipeline : renderer->swapchain.pipeline;
}

bool af_vk_r_upload_material_shader(af_vk_renderer *renderer, const char *name, const uint32_t *spirv,
                                        size_t word_count) {
    if (!renderer || !name || !name[0] || strlen(name) >= AF_MESH_NAME_CAPACITY || !spirv || word_count < 5 ||
        word_count > (4u * 1024u * 1024u) || spirv[0] != 0x07230203u)
        return false;
    size_t index = 0;
    while (index < renderer->custom_shader_count && strcmp(renderer->custom_shaders[index].name, name))
        ++index;
    if (index == renderer->custom_shader_count && renderer->custom_shader_count >= AF_SCENE_MAX_ASSETS)
        return false;
    if (!af_vk_r_wait_idle(renderer))
        return false;
    uint32_t *copy = malloc(word_count * sizeof(uint32_t));
    if (!copy)
        return false;
    memcpy(copy, spirv, word_count * sizeof(uint32_t));
    VkPipeline opaque = VK_NULL_HANDLE, blend = VK_NULL_HANDLE;
    if (renderer->swapchain.pipeline_layout && renderer->swapchain.render_pass) {
        if (!af_r_build_mesh_pipelines(renderer, copy, word_count * sizeof(uint32_t), &opaque, &blend)) {
            free(copy);
            return false;
        }
    }
    if (index == renderer->custom_shader_count) {
        if (renderer->custom_shader_count == renderer->custom_shader_capacity) {
            size_t next = renderer->custom_shader_capacity ? renderer->custom_shader_capacity * 2 : 8;
            af_material_shader_asset *grown = realloc(renderer->custom_shaders, next * sizeof(*grown));
            if (!grown) {
                if (opaque)
                    vkDestroyPipeline(renderer->device, opaque, NULL);
                if (blend)
                    vkDestroyPipeline(renderer->device, blend, NULL);
                free(copy);
                return false;
            }
            renderer->custom_shaders = grown;
            renderer->custom_shader_capacity = next;
        }
        renderer->custom_shaders[index] = (af_material_shader_asset){0};
        snprintf(renderer->custom_shaders[index].name, sizeof(renderer->custom_shaders[index].name), "%s", name);
        ++renderer->custom_shader_count;
    } else {
        if (renderer->custom_shaders[index].opaque_pipeline)
            vkDestroyPipeline(renderer->device, renderer->custom_shaders[index].opaque_pipeline, NULL);
        if (renderer->custom_shaders[index].blend_pipeline)
            vkDestroyPipeline(renderer->device, renderer->custom_shaders[index].blend_pipeline, NULL);
        free(renderer->custom_shaders[index].spirv);
    }
    renderer->custom_shaders[index].spirv = copy;
    renderer->custom_shaders[index].word_count = word_count;
    renderer->custom_shaders[index].opaque_pipeline = opaque;
    renderer->custom_shaders[index].blend_pipeline = blend;
    renderer->temporal_valid = false;
    ++renderer->texture_revision;
    return true;
}

bool af_vk_r_remove_material_shader(af_vk_renderer *renderer, const char *name) {
    if (!renderer || !name || !name[0])
        return false;
    size_t i = 0;
    while (i < renderer->custom_shader_count && strcmp(renderer->custom_shaders[i].name, name))
        ++i;
    if (i == renderer->custom_shader_count || !af_vk_r_wait_idle(renderer))
        return false;
    if (renderer->custom_shaders[i].opaque_pipeline)
        vkDestroyPipeline(renderer->device, renderer->custom_shaders[i].opaque_pipeline, NULL);
    if (renderer->custom_shaders[i].blend_pipeline)
        vkDestroyPipeline(renderer->device, renderer->custom_shaders[i].blend_pipeline, NULL);
    free(renderer->custom_shaders[i].spirv);
    memmove(&renderer->custom_shaders[i], &renderer->custom_shaders[i + 1],
            (renderer->custom_shader_count - i - 1) * sizeof(*renderer->custom_shaders));
    --renderer->custom_shader_count;
    renderer->temporal_valid = false;
    ++renderer->texture_revision;
    return true;
}

