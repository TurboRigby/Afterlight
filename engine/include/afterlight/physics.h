/* SPDX-License-Identifier: MPL-2.0 */
#ifndef AFTERLIGHT_PHYSICS_H
#define AFTERLIGHT_PHYSICS_H
#include <afterlight/character.h>
#include <afterlight/joint.h>
#include <afterlight/scene.h>
#define AF_PHYSICS_MAX_BODIES 1024u
#define AF_PHYSICS_MAX_CONTACTS 8192u
typedef struct af_physics af_physics;
typedef struct af_physics_state {
    bool available, sleeping;
    af_body_type type;
    af_vec3 position, velocity, angular_velocity;
    af_vec4 orientation;
    float mass;
} af_physics_state;
typedef struct af_physics_contact {
    af_object_id a, b;
    af_vec3 point, normal;
    float penetration, impulse;
} af_physics_contact;
typedef struct af_physics_joint_state {
    bool available, sleeping;
    af_joint_id id;
    af_vec3 anchor_a, anchor_b;
    float linear_error, angular_error, angle, speed, force, torque, motor_torque;
} af_physics_joint_state;
/* Last fixed-step state. Reaction estimates use cached solver impulses / substep
   duration and retain their last solved values while the island is asleep. */
af_physics_joint_state af_physics_get_joint_state(const af_physics *, af_joint_id);
typedef struct af_physics_stats {
    size_t bodies, dynamic_bodies, kinematic_bodies, sleeping, contacts, broadphase_pairs;
    unsigned substeps, iterations, ccd_hits, ccd_clamped;
    uint64_t ticks;
    double sync_ms, broadphase_ms, narrowphase_ms, ccd_ms, solver_ms, total_ms;
    size_t ccd_pairs, cache_probes;
    size_t islands, awake_islands, largest_island;
    float max_joint_error, max_joint_force, max_joint_torque;
} af_physics_stats;
af_physics *af_physics_create(void);
void af_physics_destroy(af_physics *);
void af_physics_reset(af_physics *);
/* Optional process-CPU timings. Disabled by default; counters always available. */
void af_physics_set_profiling(af_physics *, bool);
bool af_physics_validate_scene(const af_scene *, char *error, size_t capacity);
bool af_physics_active(const af_scene *);
/* Fixed ticks <= .1 s; internal substeps <=1/240 s. Dynamic roots are updated
   in place, including their runtime velocity fields for cloning/hot reload. */
bool af_physics_step(af_physics *, af_scene *, float delta);
bool af_physics_apply_force(af_physics *, af_scene *, af_object_id, af_vec3 force, af_vec3 world_point);
bool af_physics_apply_impulse(af_physics *, af_scene *, af_object_id, af_vec3 impulse,
                              af_vec3 world_point);
size_t af_physics_push_character(af_physics *, af_scene *, af_object_id ignore, const af_character *,
                                 af_character_config, af_vec3 desired_velocity, float force,
                                 float delta);
bool af_physics_grab(af_physics *, af_scene *, af_object_id, af_vec3 point);
bool af_physics_move_grab(af_physics *, af_vec3 target);
bool af_physics_release(af_physics *, af_scene *, af_vec3 throw_velocity);
af_object_id af_physics_held(const af_physics *);
bool af_physics_wake(af_physics *, af_object_id);
af_physics_state af_physics_get_state(const af_physics *, af_object_id);
af_physics_stats af_physics_get_stats(const af_physics *);
size_t af_physics_contacts(const af_physics *, af_physics_contact *, size_t);
const char *af_physics_error(const af_physics *);
#endif
