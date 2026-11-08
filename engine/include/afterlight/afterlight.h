/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef AFTERLIGHT_H
#define AFTERLIGHT_H

#include <stdbool.h>
#include <stdint.h>
#include <afterlight/math.h>
#include <afterlight/input.h>
#include <afterlight/physics.h>
#include <afterlight/water.h>
#include <afterlight/camera.h>
#include <afterlight/mesh.h>
#include <afterlight/scene.h>
#include <afterlight/overlay.h>
#include <afterlight/simulation.h>
#include <afterlight/character.h>
#include <afterlight/swim.h>
#include <afterlight/audio.h>
#include <afterlight/animation.h>
#include <afterlight/locomotion.h>
#include <afterlight/landscape.h>
#include <afterlight/blueprint.h>

/* All engine/window operations run on the main thread. One engine at a time. */
typedef struct af_engine af_engine;

typedef struct af_engine_config {
    const char *title;
    int width;
    int height;
    bool enable_validation;
    bool show_fps; /* Append FPS and average frame time to the window title. */
} af_engine_config;

typedef enum af_mouse_button { AF_MOUSE_LEFT, AF_MOUSE_RIGHT, AF_MOUSE_MIDDLE, AF_MOUSE_BUTTON_COUNT } af_mouse_button;

typedef struct af_engine_stats {
    uint64_t rendered_frames;
    uint64_t swapchain_rebuilds;
    size_t visible_objects, culled_objects;
    uint64_t post_bytes, shadow_bytes, refraction_bytes, mesh_bytes, tracked_gpu_bytes, gpu_budget_bytes;
    double gpu_budget_ms;
    bool gpu_time_over_budget, gpu_memory_over_budget;
    af_debug_view debug_view;
    af_anti_aliasing effective_aa;
    bool ray_query_supported, ray_traced_shadows_active, ray_tracing_failed;
    bool ray_traced_water_reflections_active;
    bool ray_traced_material_effects_active;
    bool ray_traced_gi_active;
    uint64_t ray_tracing_bytes, ray_blas_builds, ray_tlas_builds;
    size_t ray_instances, ray_meshes, ray_raster_casters;
    double gpu_ray_build_ms;
    size_t loaded_meshes;
    size_t loaded_textures;
    uint64_t texture_bytes; /* Loaded scene textures, excluding UI/shadow maps. */
    uint32_t texture_mip_levels;
    float texture_anisotropy;
    size_t shadow_draw_calls;
    uint32_t shadow_passes, shadow_cached_passes; /* Recorded/reused map layers this frame. */
    uint32_t shadow_cascades, shadow_resolution;
    uint32_t local_shadow_lights, local_shadow_maps, local_shadow_resolution, local_shadow_overflow;
    bool hdr_enabled;
    size_t missing_textures;
    size_t draw_calls; /* Visible objects actually drawn in the last submitted frame. */
    uint64_t water_upload_bytes, water_stream_uploads, mesh_idle_waits;
    double water_upload_ms;
    size_t missing_meshes; /* Visible objects skipped for unresolved mesh names. */
    /* Successful presentation submissions / real elapsed time, sampled each
       second. Zero before the first sample. Includes pacing/vsync, not GPU time. */
    double frames_per_second;
    double average_frame_ms;
    double cpu_physics_ms;
    double cpu_animation_ms; /* Pose/world/palette update; also included in render total. */
    double cpu_events_ms, cpu_update_ms,
        cpu_render_ms; /* Last completed CPU iteration; render includes waits/present. */
    double gpu_frame_ms, gpu_shadow_ms, gpu_scene_ms, gpu_post_ms, gpu_overlay_ms;
    double cpu_fence_ms, cpu_acquire_ms;         /* Renderer waits, separate from command recording. */
    bool gpu_timing_supported, gpu_timing_valid; /* Delayed completed-frame sample; excludes presentation. */
    uint64_t simulation_ticks;
    unsigned simulation_steps;
    double simulation_alpha, simulation_dropped_seconds;
    bool simulation_paused;
    uint32_t validation_errors;
    bool validation_enabled;
} af_engine_stats;

size_t af_engine_push_character(af_engine *, af_object_id, const af_character *, af_character_config, af_vec3, float,
                                float);
bool af_engine_grab_body(af_engine *, af_object_id, af_vec3);
bool af_engine_move_grab(af_engine *, af_vec3);
bool af_engine_release_body(af_engine *, af_vec3);
af_object_id af_engine_held_body(const af_engine *);
bool af_engine_apply_force(af_engine *, af_object_id, af_vec3, af_vec3);
bool af_engine_apply_impulse(af_engine *, af_object_id, af_vec3, af_vec3);
bool af_engine_wake_body(af_engine *, af_object_id);
af_physics_state af_engine_body_state(const af_engine *, af_object_id);
af_physics_joint_state af_engine_joint_state(const af_engine *, af_joint_id);
af_physics_stats af_engine_physics_stats(const af_engine *);
size_t af_engine_physics_contacts(const af_engine *, af_physics_contact *, size_t);
af_input_map af_engine_input_map(const af_engine *);
bool af_engine_save_input_map(const af_engine *, const char *, af_asset_error *);
bool af_engine_spawn_prefab(af_engine *, af_scene *, const char *, af_object_id, af_transform, af_object_id *,
                            af_scene_error *);
bool af_engine_bind_action(af_engine *, af_action_binding);
bool af_engine_load_input_map(af_engine *, const char *, af_asset_error *);
float af_engine_action_value(const af_engine *, const char *);
bool af_engine_action_pressed(const af_engine *, const char *);
typedef struct af_game {
    /* Optional callbacks. shutdown also runs when init returns false, so it
       must tolerate partially initialized game state. user_data is borrowed. */
    bool (*init)(af_engine *engine, void *user_data);
    void (*update)(af_engine *engine, double delta_seconds, void *user_data);
    void (*shutdown)(af_engine *engine, void *user_data);
    void *user_data;
    /* Optional close veto, used for unsaved editor documents. Explicit
       af_engine_request_quit bypasses this callback. */
    bool (*should_close)(af_engine *engine, void *user_data);
    /* Optional 60 Hz simulation, after update and before rendering. */
    void (*fixed_update)(af_engine *engine, double fixed_delta, void *user_data);
} af_game;

af_engine_config af_engine_config_default(void);
/* NULL config uses defaults. Window stays hidden until game initialization
   succeeds in run. Returns NULL on failure; details go to stderr. */
af_engine *af_engine_create(const af_engine_config *config);
/* Runs once per engine. Delta is capped at 0.1s. Updates suspend while the
   window is minimized and resume with a zero-delta update. Wayland also
   suspends when a previously focused window loses input focus.
   Returns 0 on normal exit, nonzero on initialization/render/validation error. */
int af_engine_run(af_engine *engine, const af_game *game);
void af_engine_destroy(af_engine *engine);
void af_engine_request_quit(af_engine *engine);
/* Borrowed UTF-8 data directory with a trailing separator (bundle Resources
   on macOS). Valid while the engine lives; NULL on failure. Not a save path. */
const char *af_engine_get_base_path(const af_engine *engine);
void af_engine_set_clear_color(af_engine *engine, af_color color);
/* Upload/replace a named mesh, shared by scene objects. Names are copied and
   must fit AF_MESH_NAME_CAPACITY. Invalid input/failure preserves old assets.
   Upload/remove wait for GPU work; changing object transforms does not. */
bool af_engine_upload_mesh(af_engine *engine, const char *name, const af_mesh_data *mesh);
/* Up to 128 skinned draw objects / 64 joints each. Copies mesh-local matrices.
   Count zero clears an override. Scene changes clear all palettes. */
bool af_engine_set_skin_palette(af_engine *engine, af_object_id object, const af_mat4 *matrices, size_t count);
typedef struct af_animation_status {
    bool available, playing, loop, finished, fading;
    uint32_t clip;
    size_t clip_count;
    double time, duration;
    float speed, blend;
    char name[64];
} af_animation_status;
typedef struct af_animation_clip_info {
    char name[64];
    double duration;
} af_animation_clip_info;
typedef struct af_animation_bone {
    uint32_t node;
    af_vec3 position, parent_position;
    bool has_parent;
} af_animation_bone;
typedef struct af_animation_debug {
    bool available, driven, root_motion;
    af_locomotion motion;
    float weights[3];
    af_vec3 root_delta;
    size_t pending_events;
    uint64_t emitted_events, dropped_events;
    char last_event[32];
} af_animation_debug;
af_animation_debug af_engine_animation_debug(const af_engine *, af_object_id);
/* Returns total joint count; writes up to capacity posed world-space joints.
   Queries do not drain events or mutate the authored scene. */
size_t af_engine_animation_bones(const af_engine *, af_object_id, af_animation_bone *, size_t capacity);
/* Playback poses do not change authored scene transforms. Gameplay follows
   fixed simulation time; real_time is intended for editor previews. */
void af_engine_set_animation_playback(af_engine *engine, bool enabled, bool real_time);
bool af_engine_load_animation(af_engine *engine, const char *name, const char *path, af_asset_error *error);
af_object_id af_engine_animation_root(const af_engine *engine, af_object_id object);
af_animation_status af_engine_animation_status(const af_engine *engine, af_object_id root);
bool af_engine_animation_clip(const af_engine *engine, af_object_id root, size_t clip, af_animation_clip_info *info);
/* Explicit fixed-tick driver. It takes over automatic playback until Play/Stop/Seek.
   Inputs use the preceding collision result; file reload/scene changes reset it. */
bool af_engine_locomotion_step(af_engine *engine, af_object_id root, af_locomotion_input input, float delta,
                               af_locomotion *status);
/* Drains the bounded event queue; dropped reports overflow since the last drain. */
bool af_engine_animation_events(af_engine *, af_object_id, af_animation_events *);
bool af_engine_locomotion_root_motion(af_engine *, af_object_id, uint32_t node, bool enabled);
bool af_engine_locomotion_root_delta(af_engine *, af_object_id, af_vec3 *);
bool af_engine_animation_play(af_engine *engine, af_object_id root, size_t clip, float blend_seconds);
bool af_engine_animation_pause(af_engine *engine, af_object_id root, bool paused);
bool af_engine_animation_stop(af_engine *engine, af_object_id root);
bool af_engine_animation_seek(af_engine *engine, af_object_id root, double seconds);
bool af_engine_animation_options(af_engine *engine, af_object_id root, float speed, bool loop);
bool af_engine_animated_world_matrix(const af_engine *engine, af_object_id object, af_mat4 *world);
bool af_engine_upload_texture(af_engine *engine, const char *name, const unsigned char *rgba, uint32_t width,
                              uint32_t height);
bool af_engine_remove_texture(af_engine *engine, const char *name);
bool af_engine_load_environment(af_engine *engine, const char *name, const char *path, af_asset_error *error);
typedef enum af_environment_job_state {
    AF_ENV_IDLE,
    AF_ENV_LOADING,
    AF_ENV_BAKING,
    AF_ENV_UPLOADING,
    AF_ENV_COMPLETE,
    AF_ENV_FAILED,
    AF_ENV_CANCELED
} af_environment_job_state;
typedef struct af_environment_job_status {
    af_environment_job_state state;
    float progress;
    double upload_ms;
    char error[256];
} af_environment_job_status;
/* One background HDR decode/bake at a time. GPU publication runs on the main
   thread between updates. Failure/cancellation retains the previous GPU asset.
   Poll status from update; COMPLETE means the named environment can be used. */
bool af_engine_start_environment(af_engine *engine, const char *name, const char *path, af_asset_error *error);
void af_engine_cancel_environment(af_engine *engine);
af_environment_job_status af_engine_environment_status(const af_engine *engine);
bool af_engine_load_texture(af_engine *engine, const char *name, const char *path, af_asset_error *error);
bool af_engine_load_mesh(af_engine *engine, const char *name, const char *path, af_asset_error *error);
bool af_engine_load_asset(af_engine *engine, const af_scene_asset *asset, af_asset_error *error);
/* Atomically replace all GPU mesh/image entries originating in one source. */
bool af_engine_reload_asset_source(af_engine *engine, const af_scene *scene, const char *source, af_asset_error *error);
bool af_engine_load_scene_assets(af_engine *engine, const af_scene *scene, af_asset_error *error);
bool af_engine_remove_mesh(af_engine *engine, const char *name);
/* Borrowed, CPU-only scene. Detach with NULL before destroying it, or keep it
   alive until engine destruction. Scene mutations appear on the next frame.
   Visible objects with missing assets are skipped and reported in stats. */
void af_engine_set_scene(af_engine *engine, const af_scene *scene);
const af_scene *af_engine_get_scene(const af_engine *engine);
/* Copies a valid camera. Projection aspect follows the scene viewport. */
bool af_engine_set_camera(af_engine *engine, const af_camera *camera);
af_camera af_engine_get_camera(const af_engine *engine);
bool af_engine_set_title(af_engine *engine, const char *title);
void af_engine_get_window_size(const af_engine *engine, int *width, int *height);
/* NULL resets to full window. Rectangles use logical window coordinates. */
void af_engine_set_viewport(af_engine *engine, const af_rect *viewport);
/* One atlas for 2D overlays; copied into owned GPU memory. Overlay packets
   borrow arrays through rendering after the current update returns.
   Set a packet each update, or NULL to clear; packets do not persist. */
bool af_engine_upload_overlay_atlas(af_engine *engine, const unsigned char *rgba, uint32_t width, uint32_t height);
/* Overlay IDs >=2 are caller-selected; at most 128 independent images.
   Upload copies pixels; transfer is queued for the next frame (one image/frame).
   Pending pixels are limited to 64 MiB total. Replacement/removal retires old
   GPU images after fences. Missing IDs draw the atlas. */
bool af_engine_upload_overlay_texture(af_engine *engine, uint32_t id, const unsigned char *rgba, uint32_t width,
                                      uint32_t height);
bool af_engine_remove_overlay_texture(af_engine *engine, uint32_t id);
bool af_engine_set_overlay(af_engine *engine, const af_overlay_frame *frame);
/* Absolute position in logical window units; wheel/text reset each update. */
af_vec2 af_engine_mouse_position(const af_engine *engine);
af_vec2 af_engine_mouse_scroll(const af_engine *engine);
bool af_engine_mouse_down(const af_engine *engine, af_mouse_button button);
bool af_engine_set_text_input(af_engine *engine, bool enabled);
const char *af_engine_text_input(const af_engine *engine);
bool af_engine_set_clipboard_text(af_engine *engine, const char *text);
/* Borrowed until the next clipboard read or engine destruction. */
const char *af_engine_get_clipboard_text(af_engine *engine);
/* Closest transformed mesh bounding box under a logical viewport position. */
af_object_id af_engine_pick_object(af_engine *engine, af_vec2 point);
void af_engine_highlight_object(af_engine *engine, af_object_id id);
bool af_engine_set_window_size(af_engine *engine, int width, int height);
bool af_engine_key_down(const af_engine *engine, af_key key);
/* Pressed means at least one non-repeat down event this update, even if a
   release arrived in the same frame. Held/pressed state clears on focus loss. */
bool af_engine_key_pressed(const af_engine *engine, af_key key);
/* OS key-repeat events this update; distinct from the initial press. */
bool af_engine_key_repeated(const af_engine *engine, af_key key);
bool af_engine_mouse_pressed(const af_engine *engine, af_mouse_button button);
af_vec2 af_engine_mouse_delta(const af_engine *engine);
bool af_engine_set_mouse_capture(af_engine *engine, bool enabled);
bool af_engine_mouse_captured(const af_engine *engine);
size_t af_engine_water_particles(const af_engine *, af_object_id, af_water_particle *, size_t);
bool af_engine_swim_character(af_engine *, af_character *, af_character_config, af_swim_state *, af_swim_config,
                              af_swim_input, float, af_query_filter);
af_water_sample af_engine_sample_water(const af_engine *, af_vec3);
bool af_engine_disturb_water(af_engine *, af_object_id, af_vec3, float, float);
af_water_stats af_engine_water_stats(const af_engine *);
bool af_engine_set_debug_view(af_engine *, af_debug_view);
void af_engine_set_visibility_culling(af_engine *, bool enabled);
bool af_engine_set_render_budget(af_engine *, double gpu_ms, uint64_t bytes);
af_engine_stats af_engine_get_stats(const af_engine *engine);

bool af_engine_texture_info(const af_engine *engine, const char *name, af_texture_info *out);
bool af_engine_upload_normal_map(af_engine *engine, const char *name, const unsigned char *rgba, uint32_t width,
                                 uint32_t height);
bool af_engine_load_normal_map(af_engine *engine, const char *name, const char *path, af_asset_error *error);
bool af_engine_load_sound(af_engine *engine, const char *name, const char *path, af_asset_error *error);
bool af_engine_play_sound_at(af_engine *, const char *, af_sound_emitter);
bool af_engine_play_sound(af_engine *engine, const char *name, float gain);
void af_engine_stop_sounds(af_engine *engine);
af_audio_stats af_engine_audio_stats(const af_engine *engine);
af_gameplay_status af_engine_gameplay_status(const af_engine *engine);
void af_engine_set_gameplay_status(af_engine *engine, af_gameplay_status status);
bool af_engine_mouse_in_viewport(const af_engine *engine);
void af_engine_set_simulation_paused(af_engine *engine, bool paused);
bool af_engine_step_simulation(af_engine *engine);
/* Call after teleporting. Scene changes reset interpolation automatically. */
void af_engine_reset_interpolation(af_engine *engine);
#endif
