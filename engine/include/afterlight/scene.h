/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef AFTERLIGHT_SCENE_H
#define AFTERLIGHT_SCENE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <afterlight/math.h>
#include <afterlight/mesh.h>
#include <afterlight/asset.h>
#include <afterlight/material.h>
#include <afterlight/shadow.h>
#include <afterlight/collision.h>
#include <afterlight/gameplay.h>
#include <afterlight/rig.h>

#define AF_OBJECT_NAME_CAPACITY 64
#define AF_OBJECT_NONE UINT64_C(0)

typedef uint64_t af_object_id;
typedef struct af_scene af_scene;
typedef struct af_animation_component {char asset[AF_MESH_NAME_CAPACITY];uint32_t clip;float speed;bool autoplay,loop;} af_animation_component;
typedef struct af_model_binding {af_object_id root;uint32_t node;int32_t skin;bool primitive;} af_model_binding;
af_animation_component af_animation_component_default(void);
af_model_binding af_model_binding_default(void);
typedef struct af_scene_object {
    af_object_id id;
    char name[AF_OBJECT_NAME_CAPACITY];
    char mesh[AF_MESH_NAME_CAPACITY];
    af_transform transform;
    bool visible;
    af_material material;
    af_collider collider;
    af_gameplay_component gameplay;
    af_object_id parent; /* Zero means scene root. Transform is parent-local. */
    af_animation_component animation;
    af_model_binding model;
    af_character_rig rig;
} af_scene_object;

/* CPU-only scene data; no engine, window, or game callbacks required. IDs are
   local to a scene, start at one, and are never reused after removal. Names
   are copied, must be nonempty, and must fit including the terminating zero.
   Object display names may repeat; mesh asset names are case-sensitive. */
af_scene *af_scene_create(void);
/* Independent copy, including object IDs and the next-ID counter. */
af_scene *af_scene_clone(const af_scene *scene);
void af_scene_destroy(af_scene *scene);
af_object_id af_scene_add_object(af_scene *scene, const char *name, const char *mesh);
bool af_scene_remove_object(af_scene *scene, af_object_id id);
size_t af_scene_object_count(const af_scene *scene);
/* Read-only views are invalidated by add/remove/destroy. Do not retain them
   across structural changes; keep the ID and look up the object again. */
const af_scene_object *af_scene_get_object(const af_scene *scene, af_object_id id);
const af_scene_object *af_scene_object_at(const af_scene *scene, size_t index);
bool af_scene_set_object_name(af_scene *scene, af_object_id id, const char *name);
bool af_scene_set_object_mesh(af_scene *scene, af_object_id id, const char *mesh);
bool af_scene_set_object_transform(af_scene *scene, af_object_id id, af_transform transform);
bool af_scene_set_object_animation(af_scene *scene,af_object_id id,af_animation_component component);
bool af_scene_set_character_rig(af_scene*,af_object_id,af_character_rig);
bool af_scene_set_model_binding(af_scene *scene,af_object_id id,af_model_binding binding);
bool af_scene_set_object_visible(af_scene *scene, af_object_id id, bool visible);

#define AF_SCENE_MAX_DEPTH 128
bool af_scene_set_parent(af_scene *scene,af_object_id child,af_object_id parent);
bool af_scene_is_descendant(const af_scene *scene,af_object_id object,af_object_id ancestor);
bool af_scene_world_matrix(const af_scene *scene,af_object_id id,af_mat4 *out);
bool af_scene_interpolated_world_matrix(const af_scene *scene,const af_scene *previous,af_object_id id,float alpha,af_mat4 *out);
bool af_scene_object_visible(const af_scene *scene,af_object_id id);

bool af_scene_set_object_gameplay(af_scene *scene,af_object_id id,af_gameplay_component component);
bool af_scene_game_camera(const af_scene *scene,af_camera *out);
bool af_scene_set_object_collider(af_scene *scene,af_object_id id,af_collider collider);
bool af_scene_set_object_material(af_scene *scene,af_object_id id,af_material material);
bool af_scene_set_light(af_scene *scene,af_directional_light light);
af_directional_light af_scene_get_light(const af_scene *scene);

size_t af_scene_local_light_count(const af_scene *scene);
const af_local_light *af_scene_local_light_at(const af_scene *scene,size_t index);
bool af_scene_add_local_light(af_scene *scene,af_local_light light);
bool af_scene_set_local_light(af_scene *scene,size_t index,af_local_light light);
bool af_scene_remove_local_light(af_scene *scene,size_t index);

bool af_scene_set_render_settings(af_scene *scene,af_render_settings settings);
af_render_settings af_scene_get_render_settings(const af_scene *scene);

bool af_scene_set_shadows(af_scene *scene,af_shadow_settings settings);
af_shadow_settings af_scene_get_shadows(const af_scene *scene);

size_t af_scene_asset_count(const af_scene *scene);
const af_scene_asset *af_scene_asset_at(const af_scene *scene,size_t index);
const af_scene_asset *af_scene_find_asset(const af_scene *scene,const char *name);
bool af_scene_set_asset(af_scene *scene,const char *name,af_asset_kind kind,const char *path);

#define AF_SCENE_FILE_VERSION 12
#define AF_SCENE_FILE_MAX_BYTES (16u * 1024u * 1024u)
#define AF_SCENE_FILE_MAX_OBJECTS 65536u

typedef enum af_scene_error_code {
    AF_SCENE_ERROR_NONE,
    AF_SCENE_ERROR_ARGUMENT,
    AF_SCENE_ERROR_IO,
    AF_SCENE_ERROR_FORMAT,
    AF_SCENE_ERROR_VERSION,
    AF_SCENE_ERROR_MEMORY
} af_scene_error_code;
typedef struct af_scene_error {
    af_scene_error_code code;
    char message[256];
} af_scene_error;

/* Versioned UTF-8 JSON. Preserves IDs, next ID, order, names, mesh references,
   transforms, and visibility. Loading allocates a new CPU-only scene and never
   changes an existing one or starts game code. Returns NULL on any failure.
   Saving replaces the destination only after a complete sibling temporary file
   is closed. The old file is preserved on failure. error may be NULL. */
af_scene *af_scene_load(const char *path, af_scene_error *error);
bool af_scene_save(const af_scene *scene, const char *path, af_scene_error *error);
#endif
