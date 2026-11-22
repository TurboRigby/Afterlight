/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "renderer_backend.h"
#include "core/extension_runtime.h"
#include "core/log.h"
#include <stdlib.h>
#include <string.h>

#if defined(AF_ENABLE_VULKAN) && defined(AF_ENABLE_METAL)
static bool extensions_require_vulkan(const af_extensions *extensions) {
    if (!extensions)
        return false;
    for (size_t i = 0; i < extensions->count; ++i) {
        if (extensions->modules[i].upscaler || extensions->modules[i].startup)
            return true;
    }
    return false;
}
#endif

static bool str_eq_ci(const char *a, const char *b) {
    if (!a || !b)
        return false;
    while (*a && *b) {
        char ca = *a >= 'A' && *a <= 'Z' ? (char)(*a + ('a' - 'A')) : *a;
        char cb = *b >= 'A' && *b <= 'Z' ? (char)(*b + ('a' - 'A')) : *b;
        if (ca != cb)
            return false;
        ++a;
        ++b;
    }
    return *a == *b;
}

af_render_backend af_renderer_select_backend(af_render_backend requested, const af_extensions *extensions) {
    if (requested == AF_RENDER_BACKEND_AUTO) {
        const char *env = getenv("AFTERLIGHT_BACKEND");
        if (env && env[0]) {
            if (str_eq_ci(env, "metal") || str_eq_ci(env, "mtl"))
                requested = AF_RENDER_BACKEND_METAL;
            else if (str_eq_ci(env, "vulkan") || str_eq_ci(env, "vk"))
                requested = AF_RENDER_BACKEND_VULKAN;
            else
                af_log("warning", "Unknown AFTERLIGHT_BACKEND='%s'; using automatic selection.", env);
        }
    }

    if (requested == AF_RENDER_BACKEND_METAL) {
#ifdef AF_ENABLE_METAL
        return AF_RENDER_BACKEND_METAL;
#else
        af_log("error", "Metal backend requested, but Afterlight was built without Metal support.");
        return AF_RENDER_BACKEND_AUTO;
#endif
    }
    if (requested == AF_RENDER_BACKEND_VULKAN) {
#ifdef AF_ENABLE_VULKAN
        return AF_RENDER_BACKEND_VULKAN;
#else
        af_log("error", "Vulkan backend requested, but Afterlight was built without Vulkan support.");
        return AF_RENDER_BACKEND_AUTO;
#endif
    }

    /* Automatic selection: honor Vulkan-specific startup/upscaler extensions if Vulkan is compiled in. */
#ifdef AF_ENABLE_VULKAN
    if (extensions_require_vulkan(extensions))
        return AF_RENDER_BACKEND_VULKAN;
#else
    (void)extensions;
#endif
#ifdef AF_ENABLE_METAL
    return AF_RENDER_BACKEND_METAL;
#elif defined(AF_ENABLE_VULKAN)
    return AF_RENDER_BACKEND_VULKAN;
#else
    return AF_RENDER_BACKEND_AUTO;
#endif
}

af_renderer *af_renderer_create_backend(SDL_Window *window, af_render_backend backend, bool enable_validation,
                                        af_extensions *extensions) {
    af_render_backend chosen = af_renderer_select_backend(backend, extensions);
    if (chosen == AF_RENDER_BACKEND_METAL) {
#ifdef AF_ENABLE_METAL
        return af_mtl_renderer_create(window, enable_validation, extensions);
#else
        return NULL;
#endif
    }
    if (chosen == AF_RENDER_BACKEND_VULKAN) {
#ifdef AF_ENABLE_VULKAN
        return af_vk_renderer_create(window, enable_validation, extensions);
#else
        return NULL;
#endif
    }
    af_log("error", "No compiled rendering backend is available.");
    return NULL;
}

af_renderer *af_renderer_create(SDL_Window *window, bool enable_validation, af_extensions *extensions) {
    return af_renderer_create_backend(window, AF_RENDER_BACKEND_AUTO, enable_validation, extensions);
}

af_render_backend af_renderer_backend(const af_renderer *renderer) {
    return renderer && renderer->ops ? renderer->ops->backend : AF_RENDER_BACKEND_AUTO;
}

void af_renderer_destroy(af_renderer *renderer) {
    if (renderer && renderer->ops && renderer->ops->destroy)
        renderer->ops->destroy(renderer);
}

void af_renderer_set_upscaler(af_renderer *r, const struct af_vulkan_upscaler_api *api, void *state) {
    if (r && r->ops && r->ops->set_upscaler)
        r->ops->set_upscaler(r, api, state);
}

bool af_renderer_set_upscaler_enabled(af_renderer *r, bool enabled) {
    return r && r->ops && r->ops->set_upscaler_enabled && r->ops->set_upscaler_enabled(r, enabled);
}

bool af_renderer_upscaler_active(const af_renderer *r) {
    return r && r->ops && r->ops->upscaler_active && r->ops->upscaler_active(r);
}

uint32_t af_renderer_upscaler_quality_count(const af_renderer *r) {
    return r && r->ops && r->ops->upscaler_quality_count ? r->ops->upscaler_quality_count(r) : 0;
}

const char *af_renderer_upscaler_quality_name(const af_renderer *r, uint32_t index) {
    return r && r->ops && r->ops->upscaler_quality_name ? r->ops->upscaler_quality_name(r, index) : NULL;
}

uint32_t af_renderer_upscaler_quality(const af_renderer *r) {
    return r && r->ops && r->ops->upscaler_quality ? r->ops->upscaler_quality(r) : 0;
}

bool af_renderer_set_upscaler_quality(af_renderer *r, uint32_t index) {
    return r && r->ops && r->ops->set_upscaler_quality && r->ops->set_upscaler_quality(r, index);
}

void af_renderer_release_upscaler(af_renderer *r) {
    if (r && r->ops && r->ops->release_upscaler)
        r->ops->release_upscaler(r);
}

bool af_renderer_wait_idle(af_renderer *r) {
    return r && r->ops && r->ops->wait_idle && r->ops->wait_idle(r);
}

af_temporal_test_stats af_renderer_test_temporal(const af_renderer *r) {
    return r && r->ops && r->ops->test_temporal ? r->ops->test_temporal(r) : (af_temporal_test_stats){0};
}

void af_renderer_reset_temporal(af_renderer *r) {
    if (r && r->ops && r->ops->reset_temporal)
        r->ops->reset_temporal(r);
}

void af_renderer_resize(af_renderer *r) {
    if (r && r->ops && r->ops->resize)
        r->ops->resize(r);
}

bool af_renderer_upload_mesh(af_renderer *r, const char *name, const af_mesh_data *mesh) {
    return r && r->ops && r->ops->upload_mesh && r->ops->upload_mesh(r, name, mesh);
}

bool af_renderer_upload_mesh_file(af_renderer *r, const char *name, af_mesh_file *mesh, af_mesh_sidedness sidedness) {
    return r && r->ops && r->ops->upload_mesh_file && r->ops->upload_mesh_file(r, name, mesh, sidedness);
}

bool af_renderer_set_skin_palette(af_renderer *r, af_object_id object, const af_mat4 *matrices, size_t count) {
    return r && r->ops && r->ops->set_skin_palette && r->ops->set_skin_palette(r, object, matrices, count);
}

void af_renderer_clear_skin_palettes(af_renderer *r) {
    if (r && r->ops && r->ops->clear_skin_palettes)
        r->ops->clear_skin_palettes(r);
}

void af_renderer_set_object_worlds(af_renderer *r, const af_scene *scene, const af_mat4 *world,
                                   const af_animation_pose *const *morph_poses, size_t count) {
    if (r && r->ops && r->ops->set_object_worlds)
        r->ops->set_object_worlds(r, scene, world, morph_poses, count);
}

bool af_renderer_remove_texture(af_renderer *r, const char *name) {
    return r && r->ops && r->ops->remove_texture && r->ops->remove_texture(r, name);
}

bool af_renderer_upload_texture(af_renderer *r, const char *name, const unsigned char *rgba, uint32_t width,
                                uint32_t height) {
    return r && r->ops && r->ops->upload_texture && r->ops->upload_texture(r, name, rgba, width, height);
}

bool af_renderer_upload_material_shader(af_renderer *r, const char *name, const uint32_t *spirv, size_t word_count) {
    return r && r->ops && r->ops->upload_material_shader && r->ops->upload_material_shader(r, name, spirv, word_count);
}

bool af_renderer_remove_material_shader(af_renderer *r, const char *name) {
    return r && r->ops && r->ops->remove_material_shader && r->ops->remove_material_shader(r, name);
}

bool af_renderer_remove_mesh(af_renderer *r, const char *name) {
    return r && r->ops && r->ops->remove_mesh && r->ops->remove_mesh(r, name);
}

bool af_renderer_upload_overlay_atlas(af_renderer *r, const unsigned char *rgba, uint32_t width, uint32_t height) {
    return r && r->ops && r->ops->upload_overlay_atlas && r->ops->upload_overlay_atlas(r, rgba, width, height);
}

af_object_id af_renderer_pick_object(af_renderer *r, const af_scene *scene, const af_camera *camera, af_rect viewport,
                                     af_vec2 point) {
    return r && r->ops && r->ops->pick_object ? r->ops->pick_object(r, scene, camera, viewport, point) : AF_OBJECT_NONE;
}

bool af_renderer_update_water(af_renderer *r, const af_physics *physics, const af_scene *scene, float alpha) {
    return r && r->ops && r->ops->update_water && r->ops->update_water(r, physics, scene, alpha);
}

bool af_renderer_update_landscape(af_renderer *r, const af_scene *scene) {
    return r && r->ops && r->ops->update_landscape && r->ops->update_landscape(r, scene);
}

bool af_renderer_draw(af_renderer *r, af_color color, const af_scene *scene, const af_camera *camera, af_rect viewport,
                      af_object_id highlighted, const af_overlay_frame *overlay, float frame_time_ms, bool *rendered) {
    return r && r->ops && r->ops->draw &&
           r->ops->draw(r, color, scene, camera, viewport, highlighted, overlay, frame_time_ms, rendered);
}

bool af_renderer_upload_overlay_texture(af_renderer *r, uint32_t id, const unsigned char *rgba, uint32_t width,
                                        uint32_t height) {
    return r && r->ops && r->ops->upload_overlay_texture && r->ops->upload_overlay_texture(r, id, rgba, width, height);
}

bool af_renderer_remove_overlay_texture(af_renderer *r, uint32_t id) {
    return r && r->ops && r->ops->remove_overlay_texture && r->ops->remove_overlay_texture(r, id);
}

bool af_renderer_test_capture_motion(af_renderer *r, af_vec2 position) {
    return r && r->ops && r->ops->test_capture_motion && r->ops->test_capture_motion(r, position);
}

bool af_renderer_test_read_motion(af_renderer *r, af_vec4 *out) {
    return r && r->ops && r->ops->test_read_motion && r->ops->test_read_motion(r, out);
}

bool af_renderer_test_capture_region(af_renderer *r, af_vec2 position, uint32_t width, uint32_t height) {
    return r && r->ops && r->ops->test_capture_region && r->ops->test_capture_region(r, position, width, height);
}

bool af_renderer_test_read_pixels(af_renderer *r, unsigned char *rgba, size_t capacity, uint32_t *width,
                                  uint32_t *height) {
    return r && r->ops && r->ops->test_read_pixels && r->ops->test_read_pixels(r, rgba, capacity, width, height);
}

bool af_renderer_test_capture_pixel(af_renderer *r, af_vec2 position) {
    return r && r->ops && r->ops->test_capture_pixel && r->ops->test_capture_pixel(r, position);
}

bool af_renderer_test_read_pixel(af_renderer *r, unsigned char rgba[4]) {
    return r && r->ops && r->ops->test_read_pixel && r->ops->test_read_pixel(r, rgba);
}

bool af_renderer_set_debug_view(af_renderer *r, af_debug_view view) {
    return r && r->ops && r->ops->set_debug_view && r->ops->set_debug_view(r, view);
}

void af_renderer_set_visibility_culling(af_renderer *r, bool enabled) {
    if (r && r->ops && r->ops->set_visibility_culling)
        r->ops->set_visibility_culling(r, enabled);
}

void af_renderer_set_prop_instancing(af_renderer *r, bool enabled) {
    if (r && r->ops && r->ops->set_prop_instancing)
        r->ops->set_prop_instancing(r, enabled);
}

bool af_renderer_upload_cluster_stream(af_renderer *r, const char *name, af_cluster_mesh *mesh,
                                       af_cluster_stream *stream) {
    return r && r->ops && r->ops->upload_cluster_stream && r->ops->upload_cluster_stream(r, name, mesh, stream);
}

bool af_renderer_upload_clusters(af_renderer *r, const char *name, af_cluster_mesh *mesh) {
    return r && r->ops && r->ops->upload_clusters && r->ops->upload_clusters(r, name, mesh);
}

void af_renderer_set_cluster_lod(af_renderer *r, bool enabled) {
    if (r && r->ops && r->ops->set_cluster_lod)
        r->ops->set_cluster_lod(r, enabled);
}

void af_renderer_set_cluster_gpu_culling(af_renderer *r, bool enabled) {
    if (r && r->ops && r->ops->set_cluster_gpu_culling)
        r->ops->set_cluster_gpu_culling(r, enabled);
}

void af_renderer_set_cluster_rendering(af_renderer *r, bool enabled, bool debug_colors) {
    if (r && r->ops && r->ops->set_cluster_rendering)
        r->ops->set_cluster_rendering(r, enabled, debug_colors);
}

bool af_renderer_set_cluster_cache_budget(af_renderer *r, uint64_t bytes) {
    return r && r->ops && r->ops->set_cluster_cache_budget && r->ops->set_cluster_cache_budget(r, bytes);
}

af_cluster_stats af_renderer_cluster_stats(const af_renderer *r) {
    return r && r->ops && r->ops->cluster_stats ? r->ops->cluster_stats(r) : (af_cluster_stats){0};
}

bool af_renderer_set_render_budget(af_renderer *r, double gpu_ms, uint64_t bytes) {
    return r && r->ops && r->ops->set_render_budget && r->ops->set_render_budget(r, gpu_ms, bytes);
}

af_engine_stats af_renderer_stats(const af_renderer *r) {
    return r && r->ops && r->ops->stats ? r->ops->stats(r) : (af_engine_stats){0};
}

bool af_renderer_texture_info(const af_renderer *r, const char *name, af_texture_info *out) {
    return r && r->ops && r->ops->texture_info && r->ops->texture_info(r, name, out);
}

bool af_renderer_upload_environment(af_renderer *r, const char *name, const float *rgba, uint32_t width,
                                    uint32_t height) {
    return r && r->ops && r->ops->upload_environment && r->ops->upload_environment(r, name, rgba, width, height);
}

bool af_renderer_upload_normal_map(af_renderer *r, const char *name, const unsigned char *rgba, uint32_t width,
                                   uint32_t height) {
    return r && r->ops && r->ops->upload_normal_map && r->ops->upload_normal_map(r, name, rgba, width, height);
}

bool af_renderer_commit_asset(af_renderer *r, const char *staged, const char *target, bool mesh) {
    return r && r->ops && r->ops->commit_asset && r->ops->commit_asset(r, staged, target, mesh);
}

bool af_renderer_upload_environment_baked(af_renderer *r, const char *name, const af_hdr_file *source,
                                          const af_ibl_images *images) {
    return r && r->ops && r->ops->upload_environment_baked &&
           r->ops->upload_environment_baked(r, name, source, images);
}

void af_renderer_set_interpolation(af_renderer *r, const af_scene *previous, float alpha) {
    if (r && r->ops && r->ops->set_interpolation)
        r->ops->set_interpolation(r, previous, alpha);
}
