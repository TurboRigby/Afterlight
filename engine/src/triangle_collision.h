/* SPDX-License-Identifier: MPL-2.0 */
#ifndef AF_TRIANGLE_COLLISION_H
#define AF_TRIANGLE_COLLISION_H
#include <afterlight/collision.h>
af_vec3 tc_closest_triangle(af_triangle, af_vec3);
float tc_capsule_triangle(af_triangle, af_vec3, float, float, af_collision_hit *);
/* Candidate list uses caller scratch or a bounded allocation; free if !=scratch. */
bool tc_candidates(af_world_collider, af_vec3 low, af_vec3 high, uint32_t **list, size_t *count,
                   uint32_t *scratch, size_t capacity);
af_triangle tc_world_triangle(af_world_collider, uint32_t);
float tc_mesh_distance(af_world_collider, af_vec3, float, float, af_collision_hit *);
bool tc_mesh_sweep(af_world_collider, af_vec3, float, float, af_vec3, float, af_collision_hit *);
bool tc_mesh_ray(af_world_collider, af_vec3, af_vec3, float, af_collision_hit *);
#endif
