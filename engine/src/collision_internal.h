/* SPDX-License-Identifier: MPL-2.0 */
#ifndef AF_COLLISION_INTERNAL_H
#define AF_COLLISION_INTERNAL_H
#include <afterlight/collision.h>
float af_collision_capsule_distance(af_world_collider,af_vec3,float,float,af_collision_hit*);
typedef struct af_collision_iterator {size_t object,shape;} af_collision_iterator;
bool af_collision_next(const struct af_scene*,af_collision_iterator*,af_world_collider*);
#endif
