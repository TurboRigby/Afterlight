/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef AFTERLIGHT_GAME_MODULE_H
#define AFTERLIGHT_GAME_MODULE_H
#include <afterlight/afterlight.h>
#include <afterlight/third_person.h>
#include <afterlight/prefab.h>
#include <afterlight/joint.h>
#include <afterlight/compound.h>
#define AF_GAME_ABI_VERSION 26u
#define AF_GAME_STATE_MAX_BYTES (16u * 1024u * 1024u)
#ifdef _WIN32
#define AF_GAME_EXPORT __declspec(dllexport)
#else
#define AF_GAME_EXPORT __attribute__((visibility("default")))
#endif
/* Host tables and scene views are borrowed for the callback only. Game modules
   call this table instead of linking an independent copy of the engine. */
typedef struct af_game_host {
    uint32_t abi_version, struct_size;
    af_scene *scene;
    af_engine *engine;
    bool input_enabled, camera_enabled;
    void *runtime_context;
    const char *project_directory;
    bool (*request_scene)(const struct af_game_host *, const char *name);
    bool (*spawn_prefab)(af_engine *, af_scene *, const char *, af_object_id, af_transform, af_object_id *,
                         af_scene_error *);
    bool (*instantiate_prefab)(af_scene *, const char *, af_object_id, af_transform, af_object_id *, af_scene_error *);
    bool (*load_scene_assets)(af_engine *, const af_scene *, af_asset_error *);
    float (*action_value)(const struct af_game_host *, const char *);
    bool (*action_pressed)(const struct af_game_host *, const char *);
    size_t (*push_character)(af_engine *, af_object_id, const af_character *, af_character_config, af_vec3, float,
                             float);
    bool (*grab_body)(af_engine *, af_object_id, af_vec3);
    bool (*move_grab)(af_engine *, af_vec3);
    bool (*release_body)(af_engine *, af_vec3);
    af_object_id (*held_body)(const af_engine *);
    bool (*apply_force)(af_engine *, af_object_id, af_vec3, af_vec3);
    bool (*apply_impulse)(af_engine *, af_object_id, af_vec3, af_vec3);
    af_physics_state (*body_state)(const af_engine *, af_object_id);
    af_physics_joint_state (*joint_state)(const af_engine *, af_joint_id);
    bool (*set_body)(af_scene *, af_object_id, af_rigid_body);
    af_collider_part_id (*add_part)(af_scene *, af_object_id, af_collider_part);
    bool (*set_part)(af_scene *, af_collider_part_id, af_collider_part);
    bool (*remove_part)(af_scene *, af_collider_part_id);
    const af_collider_part *(*get_part)(const af_scene *, af_collider_part_id);
    const af_collider_part *(*part_at)(const af_scene *, af_object_id, size_t);
    size_t (*part_count)(const af_scene *, af_object_id);
    af_wind (*wind_default)(void);
    af_wind (*get_wind)(const af_scene *);
    bool (*set_wind)(af_scene *, af_wind);
    af_vec3 (*wind_velocity)(af_wind, double);
    bool (*set_water)(af_scene *, af_object_id, af_water);
    bool (*remove_water)(af_scene *, af_object_id);
    const af_water *(*get_water)(const af_scene *, af_object_id);
    size_t (*water_particles)(const af_engine *, af_object_id, af_water_particle *, size_t);
    af_water_stats (*water_stats)(const af_engine *);
    af_swim_config (*swim_config_default)(void);
    bool (*swim_character)(af_engine *, af_character *, af_character_config, af_swim_state *, af_swim_config,
                           af_swim_input, float, af_query_filter);
    af_water_sample (*sample_water)(const af_engine *, af_vec3);
    bool (*disturb_water)(af_engine *, af_object_id, af_vec3, float, float);
    af_joint_id (*add_joint)(af_scene *, af_joint);
    bool (*set_joint)(af_scene *, af_joint_id, af_joint);
    bool (*remove_joint)(af_scene *, af_joint_id);
    const af_joint *(*get_joint)(const af_scene *, af_joint_id);
    const af_joint *(*joint_at)(const af_scene *, size_t);
    size_t (*joint_count)(const af_scene *);
    bool (*key_down)(const struct af_game_host *, af_key);
    size_t (*object_count)(const af_scene *);
    const af_scene_object *(*object_at)(const af_scene *, size_t);
    const af_scene_object *(*get_object)(const af_scene *, af_object_id);
    af_object_id (*add_object)(af_scene *, const char *, const char *);
    bool (*remove_object)(af_scene *, af_object_id);
    bool (*set_transform)(af_scene *, af_object_id, af_transform);
    bool (*set_material)(af_scene *, af_object_id, af_material);
    bool (*set_parent)(af_scene *, af_object_id, af_object_id);
    void (*log)(const char *);
    size_t (*local_light_count)(const af_scene *);
    const af_local_light *(*local_light_at)(const af_scene *, size_t);
    bool (*add_local_light)(af_scene *, af_local_light);
    bool (*set_local_light)(af_scene *, size_t, af_local_light);
    bool (*remove_local_light)(af_scene *, size_t);
    bool (*set_light)(af_scene *, af_directional_light);
    bool (*set_render_settings)(af_scene *, af_render_settings);
    af_render_settings (*get_render_settings)(const af_scene *);
    af_engine_stats (*get_stats)(const af_engine *);
    bool (*key_pressed)(const struct af_game_host *, af_key);
    af_vec2 (*mouse_delta)(const af_engine *);
    bool (*mouse_pressed)(const af_engine *, af_mouse_button);
    bool (*mouse_in_viewport)(const af_engine *);
    bool (*mouse_captured)(const af_engine *);
    bool (*capture_mouse)(af_engine *, bool);
    bool (*set_camera)(af_engine *, const af_camera *);
    void (*reset_interpolation)(af_engine *);
    bool (*set_collider)(af_scene *, af_object_id, af_collider);
    bool (*raycast)(const af_scene *, af_vec3, af_vec3, float, af_query_filter, af_collision_hit *);
    size_t (*overlap_capsule)(const af_scene *, af_vec3, float, float, af_query_filter, af_collision_hit *, size_t);
    af_character_config (*character_config_default)(void);
    bool (*move_character)(const af_scene *, af_character *, af_character_config, af_character_input, float,
                           af_query_filter);
    bool (*world_matrix)(const af_scene *, af_object_id, af_mat4 *);
    bool (*game_camera)(const af_scene *, af_camera *);
    void (*report_gameplay)(af_engine *, af_gameplay_status);
    bool (*play_sound_at)(af_engine *, const char *, af_sound_emitter);
    bool (*play_sound)(af_engine *, const char *, float);
    bool (*animation_play)(af_engine *, af_object_id, size_t, float);
    bool (*animation_pause)(af_engine *, af_object_id, bool);
    bool (*animation_stop)(af_engine *, af_object_id);
    bool (*animation_seek)(af_engine *, af_object_id, double);
    bool (*animation_options)(af_engine *, af_object_id, float, bool);
    af_animation_status (*animation_status)(const af_engine *, af_object_id);
    bool (*animated_world_matrix)(const af_engine *, af_object_id, af_mat4 *);
    bool (*animation_clip)(const af_engine *, af_object_id, size_t, af_animation_clip_info *);
    af_object_id (*animation_root)(const af_engine *, af_object_id);
    bool (*locomotion_root_motion)(af_engine *, af_object_id, uint32_t, bool);
    bool (*locomotion_root_delta)(af_engine *, af_object_id, af_vec3 *);
    bool (*move_character_root_motion)(const af_scene *, af_character *, af_character_config, af_vec3, bool, float,
                                       af_query_filter);
    bool (*animation_events)(af_engine *, af_object_id, af_animation_events *);
    bool (*locomotion_step)(af_engine *, af_object_id, af_locomotion_input, float, af_locomotion *);
    af_third_person_config (*third_person_config_default)(void);
    af_third_person (*third_person_default)(af_vec3);
    void (*third_person_look)(af_third_person *, af_third_person_config, af_vec2);
    bool (*third_person_step)(const af_scene *, af_third_person *, af_third_person_config, af_third_person_input, float,
                              af_query_filter);
    bool (*third_person_step_root_motion)(const af_scene *, af_third_person *, af_third_person_config,
                                          af_third_person_input, af_vec3, float, af_query_filter);
    bool (*third_person_camera)(const af_scene *, af_third_person *, af_third_person_config, float, af_query_filter,
                                af_camera *);
} af_game_host;
typedef struct af_game_module {
    uint32_t abi_version, struct_size;
    uint32_t host_size, object_size;
    const char *name;
    size_t state_size;
    bool (*init)(void *state, const af_game_host *host);
    void (*update)(void *state, const af_game_host *host, double delta_seconds);
    void (*shutdown)(void *state, const af_game_host *host);
    /* Optional per-render update for mouse look/camera; update above is a fixed tick. */
    void (*frame_update)(void *state, const af_game_host *host, double delta_seconds);
    /* Optional paired callbacks. Blob is module-owned format, bounded to 1 MiB.
       save(NULL,0) returns required size; load must validate version and bytes. */
    uint32_t state_version;
    size_t (*save_state)(const void *state, void *bytes, size_t capacity);
    bool (*load_state)(void *state, const af_game_host *host, uint32_t version, const void *bytes, size_t size);
} af_game_module;
af_game_host af_game_host_create(af_scene *scene, af_engine *engine);
typedef const af_game_module *(*af_game_get_api_fn)(uint32_t abi_version);
/* One exported entry point. Rebuild against matching headers on ABI changes. */
AF_GAME_EXPORT const af_game_module *af_game_get_api(uint32_t abi_version);
#endif
