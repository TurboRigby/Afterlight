/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef AF_RENDERER_H
#define AF_RENDERER_H
#include <afterlight/afterlight.h>
#include <SDL3/SDL.h>

typedef struct af_renderer af_renderer;
af_renderer *af_renderer_create(SDL_Window *window, bool enable_validation);
void af_renderer_destroy(af_renderer *renderer);
bool af_renderer_wait_idle(af_renderer *renderer);
void af_renderer_resize(af_renderer *renderer);
bool af_renderer_upload_mesh(af_renderer *renderer, const char *name, const af_mesh_data *mesh);
bool af_renderer_remove_texture(af_renderer *renderer,const char *name);
bool af_renderer_upload_texture(af_renderer *renderer,const char *name,const unsigned char *rgba,uint32_t width,uint32_t height);
bool af_renderer_remove_mesh(af_renderer *renderer, const char *name);
bool af_renderer_upload_overlay_atlas(af_renderer *renderer, const unsigned char *rgba, uint32_t width, uint32_t height);
af_object_id af_renderer_pick_object(af_renderer *renderer, const af_scene *scene, const af_camera *camera, af_rect viewport, af_vec2 point);
bool af_renderer_draw(af_renderer *renderer, af_color color, const af_scene *scene, const af_camera *camera, af_rect viewport, af_object_id highlighted, const af_overlay_frame *overlay, bool *rendered);
bool af_renderer_upload_overlay_texture(af_renderer *renderer,uint32_t id,const unsigned char *rgba,uint32_t width,uint32_t height);
bool af_renderer_remove_overlay_texture(af_renderer *renderer,uint32_t id);
bool af_renderer_test_capture_pixel(af_renderer *renderer,af_vec2 position);
bool af_renderer_test_read_pixel(af_renderer *renderer,unsigned char rgba[4]);
af_engine_stats af_renderer_stats(const af_renderer *renderer);
bool af_renderer_texture_info(const af_renderer *renderer,const char *name,af_texture_info *out);
bool af_renderer_upload_normal_map(af_renderer *renderer,const char *name,const unsigned char *rgba,uint32_t width,uint32_t height);
bool af_renderer_commit_asset(af_renderer *renderer,const char *staged,const char *target,bool mesh);
void af_renderer_set_interpolation(af_renderer *renderer,const af_scene *previous,float alpha);
#endif
