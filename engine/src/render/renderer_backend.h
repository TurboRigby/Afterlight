/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef AF_RENDERER_BACKEND_H
#define AF_RENDERER_BACKEND_H

#include "renderer.h"

typedef struct af_renderer_ops {
    af_render_backend backend;
    void (*destroy)(af_renderer *renderer);
    void (*set_upscaler)(af_renderer *renderer, const struct af_vulkan_upscaler_api *api, void *state);
    bool (*set_upscaler_enabled)(af_renderer *renderer, bool enabled);
    bool (*upscaler_active)(const af_renderer *renderer);
    uint32_t (*upscaler_quality_count)(const af_renderer *renderer);
    const char *(*upscaler_quality_name)(const af_renderer *renderer, uint32_t index);
    uint32_t (*upscaler_quality)(const af_renderer *renderer);
    bool (*set_upscaler_quality)(af_renderer *renderer, uint32_t index);
    void (*release_upscaler)(af_renderer *renderer);
    bool (*wait_idle)(af_renderer *renderer);
    af_temporal_test_stats (*test_temporal)(const af_renderer *renderer);
    void (*reset_temporal)(af_renderer *renderer);
    void (*resize)(af_renderer *renderer);
    bool (*upload_mesh)(af_renderer *renderer, const char *name, const af_mesh_data *mesh);
    bool (*upload_mesh_file)(af_renderer *renderer, const char *name, af_mesh_file *mesh, af_mesh_sidedness sidedness);
    bool (*set_skin_palette)(af_renderer *renderer, af_object_id object, const af_mat4 *matrices, size_t count);
    void (*clear_skin_palettes)(af_renderer *renderer);
    void (*set_object_worlds)(af_renderer *renderer, const af_scene *scene, const af_mat4 *world,
                              const af_animation_pose *const *morph_poses, size_t count);
    bool (*remove_texture)(af_renderer *renderer, const char *name);
    bool (*upload_texture)(af_renderer *renderer, const char *name, const unsigned char *rgba, uint32_t width,
                           uint32_t height);
    bool (*upload_material_shader)(af_renderer *renderer, const char *name, const uint32_t *spirv, size_t word_count);
    bool (*remove_material_shader)(af_renderer *renderer, const char *name);
    bool (*remove_mesh)(af_renderer *renderer, const char *name);
    bool (*upload_overlay_atlas)(af_renderer *renderer, const unsigned char *rgba, uint32_t width, uint32_t height);
    af_object_id (*pick_object)(af_renderer *renderer, const af_scene *scene, const af_camera *camera, af_rect viewport,
                                af_vec2 point);
    bool (*update_water)(af_renderer *renderer, const af_physics *physics, const af_scene *scene, float alpha);
    bool (*update_landscape)(af_renderer *renderer, const af_scene *scene);
    bool (*draw)(af_renderer *renderer, af_color color, const af_scene *scene, const af_camera *camera,
                 af_rect viewport, af_object_id highlighted, const af_overlay_frame *overlay, float frame_time_ms,
                 bool *rendered);
    bool (*upload_overlay_texture)(af_renderer *renderer, uint32_t id, const unsigned char *rgba, uint32_t width,
                                   uint32_t height);
    bool (*remove_overlay_texture)(af_renderer *renderer, uint32_t id);
    bool (*test_capture_motion)(af_renderer *renderer, af_vec2 position);
    bool (*test_read_motion)(af_renderer *renderer, af_vec4 *out);
    bool (*test_capture_region)(af_renderer *renderer, af_vec2 position, uint32_t width, uint32_t height);
    bool (*test_read_pixels)(af_renderer *renderer, unsigned char *rgba, size_t capacity, uint32_t *width,
                             uint32_t *height);
    bool (*test_capture_pixel)(af_renderer *renderer, af_vec2 position);
    bool (*test_read_pixel)(af_renderer *renderer, unsigned char rgba[4]);
    bool (*set_debug_view)(af_renderer *renderer, af_debug_view view);
    void (*set_visibility_culling)(af_renderer *renderer, bool enabled);
    void (*set_prop_instancing)(af_renderer *renderer, bool enabled);
    bool (*upload_cluster_stream)(af_renderer *renderer, const char *name, af_cluster_mesh *mesh,
                                  af_cluster_stream *stream);
    bool (*upload_clusters)(af_renderer *renderer, const char *name, af_cluster_mesh *mesh);
    void (*set_cluster_lod)(af_renderer *renderer, bool enabled);
    void (*set_cluster_gpu_culling)(af_renderer *renderer, bool enabled);
    void (*set_cluster_rendering)(af_renderer *renderer, bool enabled, bool debug_colors);
    bool (*set_cluster_cache_budget)(af_renderer *renderer, uint64_t bytes);
    af_cluster_stats (*cluster_stats)(const af_renderer *renderer);
    bool (*set_render_budget)(af_renderer *renderer, double gpu_ms, uint64_t bytes);
    af_engine_stats (*stats)(const af_renderer *renderer);
    bool (*texture_info)(const af_renderer *renderer, const char *name, af_texture_info *out);
    bool (*upload_environment)(af_renderer *renderer, const char *name, const float *rgba, uint32_t width,
                               uint32_t height);
    bool (*upload_normal_map)(af_renderer *renderer, const char *name, const unsigned char *rgba, uint32_t width,
                              uint32_t height);
    bool (*commit_asset)(af_renderer *renderer, const char *staged, const char *target, bool mesh);
    bool (*upload_environment_baked)(af_renderer *renderer, const char *name, const af_hdr_file *source,
                                     const af_ibl_images *images);
    void (*set_interpolation)(af_renderer *renderer, const af_scene *previous, float alpha);
} af_renderer_ops;

struct af_renderer {
    const af_renderer_ops *ops;
};

#ifdef AF_ENABLE_VULKAN
af_renderer *af_vk_renderer_create(SDL_Window *window, bool enable_validation, struct af_extensions *extensions);
#endif

#ifdef AF_ENABLE_METAL
af_renderer *af_mtl_renderer_create(SDL_Window *window, bool enable_validation, struct af_extensions *extensions);
#endif

#endif
