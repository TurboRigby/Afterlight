/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef AFTERLIGHT_COLLISION_H
#define AFTERLIGHT_COLLISION_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <afterlight/math.h>
#include <afterlight/collision_mesh.h>
struct af_scene;
typedef enum af_collider_type {AF_COLLIDER_NONE,AF_COLLIDER_BOX,AF_COLLIDER_SPHERE,AF_COLLIDER_CAPSULE,AF_COLLIDER_MESH,AF_COLLIDER_COMPOUND} af_collider_type;
typedef struct af_collider {
    af_collider_type type;
    bool enabled,trigger;
    af_vec3 center,half_extents;
    float radius,half_height; /* Capsule segment half-length; total height = 2*(half_height+radius). */
    uint32_t layer;
} af_collider;
typedef struct af_query_filter {uint64_t ignore,ignore_also;uint32_t mask;bool include_triggers;} af_query_filter;
typedef struct af_collision_hit {uint64_t object;af_vec3 point,normal;float distance,fraction,penetration;bool trigger;} af_collision_hit;
typedef struct af_world_collider {
    uint64_t object;af_collider_type type;bool trigger;uint32_t layer;
    af_vec3 center,axes[3],half_extents,a,b;float radius;
    const af_collision_mesh *mesh;af_mat4 matrix,inverse;
} af_world_collider;
af_collider af_collider_default(void);
bool af_collider_valid(af_collider collider);
af_query_filter af_query_filter_default(void);
/* Hidden geometry can still collide. Disabled, singular and sheared collider transforms are skipped.
   Boxes are OBBs. Sphere/capsule radii conservatively use the largest world scale. */
bool af_scene_world_collider(const struct af_scene *scene,uint64_t object,af_world_collider *out);
bool af_scene_raycast(const struct af_scene *scene,af_vec3 origin,af_vec3 direction,float distance,af_query_filter filter,af_collision_hit *hit);
/* Query capsules are upright in world Y. Returns total count even when output capacity is smaller. */
size_t af_scene_overlap_capsule(const struct af_scene *scene,af_vec3 center,float radius,float half_height,af_query_filter filter,af_collision_hit *hits,size_t capacity);
size_t af_scene_overlap_sphere(const struct af_scene *scene,af_vec3 center,float radius,af_query_filter filter,af_collision_hit *hits,size_t capacity);
bool af_collider_sweep_sphere(af_world_collider,af_vec3 center,float radius,af_vec3 displacement,float skin,af_collision_hit*);
bool af_scene_sweep_capsule(const struct af_scene *scene,af_vec3 center,float radius,float half_height,af_vec3 displacement,float skin,af_query_filter filter,af_collision_hit *hit);
#endif
