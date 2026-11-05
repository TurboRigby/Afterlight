/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef AFTERLIGHT_GAME_MODULE_H
#define AFTERLIGHT_GAME_MODULE_H
#include <afterlight/afterlight.h>
#define AF_GAME_ABI_VERSION 6u
#define AF_GAME_STATE_MAX_BYTES (16u*1024u*1024u)
#ifdef _WIN32
#define AF_GAME_EXPORT __declspec(dllexport)
#else
#define AF_GAME_EXPORT __attribute__((visibility("default")))
#endif
/* Host tables and scene views are borrowed for the callback only. Game modules
   call this table instead of linking an independent copy of the engine. */
typedef struct af_game_host {
    uint32_t abi_version,struct_size;
    af_scene *scene;
    af_engine *engine;
    bool input_enabled,camera_enabled;
    bool (*key_down)(const struct af_game_host*,af_key);
    size_t (*object_count)(const af_scene*);
    const af_scene_object *(*object_at)(const af_scene*,size_t);
    const af_scene_object *(*get_object)(const af_scene*,af_object_id);
    af_object_id (*add_object)(af_scene*,const char*,const char*);
    bool (*remove_object)(af_scene*,af_object_id);
    bool (*set_transform)(af_scene*,af_object_id,af_transform);
    bool (*set_material)(af_scene*,af_object_id,af_material);
    bool (*set_parent)(af_scene*,af_object_id,af_object_id);
    void (*log)(const char*);
    size_t (*local_light_count)(const af_scene*);
    const af_local_light *(*local_light_at)(const af_scene*,size_t);
    bool (*add_local_light)(af_scene*,af_local_light);
    bool (*set_local_light)(af_scene*,size_t,af_local_light);
    bool (*remove_local_light)(af_scene*,size_t);
    bool (*set_light)(af_scene*,af_directional_light);
    bool (*set_render_settings)(af_scene*,af_render_settings);
    bool (*key_pressed)(const struct af_game_host*,af_key);
    af_vec2 (*mouse_delta)(const af_engine*);
    bool (*mouse_pressed)(const af_engine*,af_mouse_button);
    bool (*mouse_in_viewport)(const af_engine*);
    bool (*mouse_captured)(const af_engine*);
    bool (*capture_mouse)(af_engine*,bool);
    bool (*set_camera)(af_engine*,const af_camera*);
    void (*reset_interpolation)(af_engine*);
    bool (*set_collider)(af_scene*,af_object_id,af_collider);
    bool (*raycast)(const af_scene*,af_vec3,af_vec3,float,af_query_filter,af_collision_hit*);
    size_t (*overlap_capsule)(const af_scene*,af_vec3,float,float,af_query_filter,af_collision_hit*,size_t);
    af_character_config (*character_config_default)(void);
    bool (*move_character)(const af_scene*,af_character*,af_character_config,af_character_input,float,af_query_filter);
    bool (*world_matrix)(const af_scene*,af_object_id,af_mat4*);
    bool (*game_camera)(const af_scene*,af_camera*);
    void (*report_gameplay)(af_engine*,af_gameplay_status);
    bool (*play_sound)(af_engine*,const char*,float);
} af_game_host;
typedef struct af_game_module {
    uint32_t abi_version,struct_size;
    uint32_t host_size,object_size;
    const char *name;
    size_t state_size;
    bool (*init)(void *state,const af_game_host *host);
    void (*update)(void *state,const af_game_host *host,double delta_seconds);
    void (*shutdown)(void *state,const af_game_host *host);
    /* Optional per-render update for mouse look/camera; update above is a fixed tick. */
    void (*frame_update)(void *state,const af_game_host *host,double delta_seconds);
} af_game_module;
af_game_host af_game_host_create(af_scene *scene,af_engine *engine);
typedef const af_game_module *(*af_game_get_api_fn)(uint32_t abi_version);
/* One exported entry point. Rebuild against matching headers on ABI changes. */
AF_GAME_EXPORT const af_game_module *af_game_get_api(uint32_t abi_version);
#endif
