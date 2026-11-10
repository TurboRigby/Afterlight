/* SPDX-License-Identifier: MPL-2.0 */
#ifndef AF_ANIMATION_RUNTIME_H
#define AF_ANIMATION_RUNTIME_H
#include "render/renderer.h"
typedef struct af_animation_runtime af_animation_runtime;
af_animation_runtime *af_animation_runtime_create(af_renderer *renderer);
void af_animation_runtime_destroy(af_animation_runtime *runtime);
void af_animation_runtime_scene(af_animation_runtime *runtime, const af_scene *scene);
bool af_animation_runtime_load(af_animation_runtime *runtime, const char *name, const char *path,
                               af_asset_error *error);
bool af_animation_runtime_commit(af_animation_runtime *runtime, const char *staged, const char *target);
void af_animation_runtime_remove(af_animation_runtime *runtime, const char *name);
bool af_animation_runtime_update(af_animation_runtime *runtime, const af_scene *previous, float alpha, double delta,
                                 bool animate);
af_object_id af_animation_runtime_root(const af_animation_runtime *runtime, af_object_id object);
af_animation_status af_animation_runtime_status(af_animation_runtime *runtime, af_object_id object);
bool af_animation_runtime_clip(af_animation_runtime *runtime, af_object_id object, size_t clip,
                               af_animation_clip_info *info);
bool af_animation_runtime_locomotion(af_animation_runtime *runtime, af_object_id object, af_locomotion_input input,
                                     float delta, af_locomotion *status);
bool af_animation_runtime_events(af_animation_runtime *, af_object_id, af_animation_events *);
bool af_animation_runtime_root_motion(af_animation_runtime *, af_object_id, uint32_t node, bool enabled);
bool af_animation_runtime_root_delta(af_animation_runtime *, af_object_id, af_vec3 *);
bool af_animation_runtime_play(af_animation_runtime *runtime, af_object_id object, size_t clip, float blend);
bool af_animation_runtime_pause(af_animation_runtime *runtime, af_object_id object, bool paused);
bool af_animation_runtime_stop(af_animation_runtime *runtime, af_object_id object);
bool af_animation_runtime_seek(af_animation_runtime *runtime, af_object_id object, double seconds);
bool af_animation_runtime_options(af_animation_runtime *runtime, af_object_id object, float speed, bool loop);
bool af_animation_runtime_world(const af_animation_runtime *runtime, af_object_id object, af_mat4 *world);
af_animation_debug af_animation_runtime_debug(af_animation_runtime *, af_object_id);
size_t af_animation_runtime_bones(af_animation_runtime *, af_object_id, af_animation_bone *, size_t);
#endif
