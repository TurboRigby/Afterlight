/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef AF_RENDERER_H
#define AF_RENDERER_H
#include <afterlight/afterlight.h>
#include <SDL3/SDL.h>
#include "ibl.h"
#include "render_test.h"

typedef struct af_renderer af_renderer;
struct af_extensions;
struct af_vulkan_upscaler_api;
af_renderer *af_renderer_create(SDL_Window *window, bool enable_validation, struct af_extensions *);
void af_renderer_destroy(af_renderer *renderer);
void af_renderer_set_upscaler(af_renderer *, const struct af_vulkan_upscaler_api *, void *state);
bool af_renderer_set_upscaler_enabled(af_renderer *, bool enabled);
bool af_renderer_upscaler_active(const af_renderer *);
uint32_t af_renderer_upscaler_quality_count(const af_renderer *);
const char *af_renderer_upscaler_quality_name(const af_renderer *, uint32_t index);
uint32_t af_renderer_upscaler_quality(const af_renderer *);
bool af_renderer_set_upscaler_quality(af_renderer *, uint32_t index);
void af_renderer_release_upscaler(af_renderer *);
bool af_renderer_wait_idle(af_renderer *renderer);
af_temporal_test_stats af_renderer_test_temporal(const af_renderer *);
void af_renderer_reset_temporal(af_renderer *);
void af_renderer_resize(af_renderer *renderer);
bool af_renderer_upload_mesh(af_renderer *renderer, const char *name, const af_mesh_data *mesh);
bool af_renderer_upload_mesh_file(af_renderer *renderer, const char *name, af_mesh_file *mesh);
bool af_renderer_set_skin_palette(af_renderer *renderer, af_object_id object, const af_mat4 *matrices, size_t count);
void af_renderer_clear_skin_palettes(af_renderer *renderer);
/* Borrowed through the next draw, in current scene object order. */
void af_renderer_set_object_worlds(af_renderer *renderer, const af_scene *scene, const af_mat4 *world,
                                   const af_animation_pose *const *morph_poses, size_t count);
bool af_renderer_remove_texture(af_renderer *renderer, const char *name);
bool af_renderer_upload_texture(af_renderer *renderer, const char *name, const unsigned char *rgba, uint32_t width,
                                uint32_t height);
bool af_renderer_remove_mesh(af_renderer *renderer, const char *name);
bool af_renderer_upload_overlay_atlas(af_renderer *renderer, const unsigned char *rgba, uint32_t width,
                                      uint32_t height);
af_object_id af_renderer_pick_object(af_renderer *renderer, const af_scene *scene, const af_camera *camera,
                                     af_rect viewport, af_vec2 point);
bool af_renderer_update_water(af_renderer *, const af_physics *, const af_scene *, float);
bool af_renderer_update_landscape(af_renderer *renderer, const af_scene *scene);
bool af_renderer_draw(af_renderer *renderer, af_color color, const af_scene *scene, const af_camera *camera,
                      af_rect viewport, af_object_id highlighted, const af_overlay_frame *overlay, float frame_time_ms,
                      bool *rendered);
bool af_renderer_upload_overlay_texture(af_renderer *renderer, uint32_t id, const unsigned char *rgba, uint32_t width,
                                        uint32_t height);
bool af_renderer_remove_overlay_texture(af_renderer *renderer, uint32_t id);
bool af_renderer_test_capture_motion(af_renderer *, af_vec2);
bool af_renderer_test_read_motion(af_renderer *, af_vec4 *);
bool af_renderer_test_capture_region(af_renderer *, af_vec2, uint32_t, uint32_t);
bool af_renderer_test_read_pixels(af_renderer *, unsigned char *, size_t, uint32_t *, uint32_t *);
bool af_renderer_test_capture_pixel(af_renderer *renderer, af_vec2 position);
bool af_renderer_test_read_pixel(af_renderer *renderer, unsigned char rgba[4]);
bool af_renderer_set_debug_view(af_renderer *, af_debug_view);
void af_renderer_set_visibility_culling(af_renderer *, bool);
bool af_renderer_set_render_budget(af_renderer *, double, uint64_t);
af_engine_stats af_renderer_stats(const af_renderer *renderer);
bool af_renderer_texture_info(const af_renderer *renderer, const char *name, af_texture_info *out);
bool af_renderer_upload_environment(af_renderer *renderer, const char *name, const float *rgba, uint32_t width,
                                    uint32_t height);
bool af_renderer_upload_normal_map(af_renderer *renderer, const char *name, const unsigned char *rgba, uint32_t width,
                                   uint32_t height);
bool af_renderer_commit_asset(af_renderer *renderer, const char *staged, const char *target, bool mesh);
bool af_renderer_upload_environment_baked(af_renderer *renderer, const char *name, const af_hdr_file *source,
                                          const af_ibl_images *images);
void af_renderer_set_interpolation(af_renderer *renderer, const af_scene *previous, float alpha);
#endif
