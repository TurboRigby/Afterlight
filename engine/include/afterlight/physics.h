/* SPDX-License-Identifier: MPL-2.0 */
#ifndef AFTERLIGHT_PHYSICS_H
#define AFTERLIGHT_PHYSICS_H
#include <afterlight/scene.h>
#define AF_PHYSICS_MAX_BODIES 1024u
#define AF_PHYSICS_MAX_CONTACTS 8192u
typedef struct af_physics af_physics;
typedef struct af_physics_state {bool available,sleeping;af_body_type type;af_vec3 position,velocity,angular_velocity;af_vec4 orientation;float mass;} af_physics_state;
typedef struct af_physics_contact {af_object_id a,b;af_vec3 point,normal;float penetration,impulse;} af_physics_contact;
typedef struct af_physics_stats {size_t bodies,dynamic_bodies,kinematic_bodies,sleeping,contacts,broadphase_pairs;unsigned substeps,iterations,ccd_hits;uint64_t ticks;} af_physics_stats;
af_physics *af_physics_create(void);
void af_physics_destroy(af_physics*);
void af_physics_reset(af_physics*);
bool af_physics_active(const af_scene*);
/* Fixed ticks <= .1 s; internal substeps <=1/240 s. Dynamic roots are updated
   in place, including their runtime velocity fields for cloning/hot reload. */
bool af_physics_step(af_physics*,af_scene*,float delta);
bool af_physics_apply_force(af_physics*,af_scene*,af_object_id,af_vec3 force,af_vec3 world_point);
bool af_physics_apply_impulse(af_physics*,af_scene*,af_object_id,af_vec3 impulse,af_vec3 world_point);
bool af_physics_wake(af_physics*,af_object_id);
af_physics_state af_physics_get_state(const af_physics*,af_object_id);
af_physics_stats af_physics_get_stats(const af_physics*);
size_t af_physics_contacts(const af_physics*,af_physics_contact*,size_t);
const char *af_physics_error(const af_physics*);
#endif
