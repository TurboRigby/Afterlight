/* SPDX-License-Identifier: MPL-2.0 */

#ifndef AFTERLIGHT_COMPOUND_H
#define AFTERLIGHT_COMPOUND_H
#include <afterlight/scene.h>
#define AF_COMPOUND_MAX_PARTS 16u
#define AF_SCENE_MAX_COLLIDER_PARTS 1024u
typedef uint64_t af_collider_part_id;
typedef struct af_collider_part {
    af_collider_part_id id;
    af_object_id owner;
    char name[64];
    af_transform local;
    af_collider collider; /* Centered box/sphere; part pose supplies the offset. */
    float density; /* kg/m^3; relative weights when body.mass overrides total. */
} af_collider_part;
af_collider_part af_collider_part_default(void);
bool af_collider_part_valid(af_collider_part);
af_collider_part_id af_scene_add_collider_part(af_scene*,af_object_id,af_collider_part);
bool af_scene_set_collider_part(af_scene*,af_collider_part_id,af_collider_part);
bool af_scene_remove_collider_part(af_scene*,af_collider_part_id);
size_t af_scene_collider_part_count(const af_scene*,af_object_id);
const af_collider_part *af_scene_collider_part_at(const af_scene*,af_object_id,size_t);
const af_collider_part *af_scene_get_collider_part(const af_scene*,af_collider_part_id);
size_t af_scene_collider_shape_count(const af_scene*,af_object_id);
bool af_scene_collider_shape_at(const af_scene*,af_object_id,size_t,af_world_collider*);
typedef struct af_mass_properties {float volume,mass;af_vec3 center;af_mat4 inertia;} af_mass_properties;
/* Physical units in normalized owner axes, including world scale; inertia is
   about the center of mass. Overlapping parts contribute additively. */
bool af_scene_compound_mass(const af_scene*,af_object_id,af_mass_properties*);
#endif
