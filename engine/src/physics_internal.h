/* SPDX-License-Identifier: MPL-2.0 */
#ifndef AF_PHYSICS_INTERNAL_H
#define AF_PHYSICS_INTERNAL_H
#include <afterlight/animation.h>
#include <afterlight/compound.h>
#include <afterlight/joint.h>
#include <afterlight/physics.h>
#include <math.h>
#include <time.h>
typedef struct af_water_runtime af_water_runtime;
typedef struct ph_body {
    af_object_id id;
    size_t scene_index;
    af_rigid_body config;
    af_transform published;
    af_world_collider shape;
    af_vec3 p, v, w, force, torque, inertia, axes[3], com_offset;
    af_mat4 inverse_tensor;
    uint64_t geometry_key;
    af_world_collider *parts;
    size_t part_count, part_start;
    af_vec4 q;af_vec3 water_previous_p,water_previous_axes[3];bool water_motion;
    float inv_mass, sleep_time;
    bool sleeping, supported, external, character_proxy, wet,water_calm,water_support;
    uint64_t water_key;
} ph_body;
typedef struct ph_contact {
    af_physics_contact view;
    size_t a, b;
    af_vec3 local_a, local_b, tangent;
    float normal, bounce;
} ph_contact;
typedef struct ph_bound {
    size_t body;
    af_vec3 low, high;
    float key_low, key_high;
} ph_bound;
typedef struct ph_joint_row {
    af_vec3 n, ra, rb;
    float inverse, bias, lambda, lo, hi;
} ph_joint_row;
typedef struct ph_joint {
    af_joint config;
    size_t a, b;
    ph_joint_row rows[7];
} ph_joint;
struct af_physics {
    af_water_runtime *water;
    af_object_id held;
    af_vec3 grab_local, grab_target;
    af_scene *scene;
    ph_body bodies[AF_PHYSICS_MAX_BODIES], scratch[AF_PHYSICS_MAX_BODIES];
    size_t count;
    ph_contact contacts[AF_PHYSICS_MAX_CONTACTS], cache[AF_PHYSICS_MAX_CONTACTS];
    size_t contact_count, cache_count;
    af_world_collider parts[AF_SCENE_MAX_COLLIDER_PARTS], part_scratch[AF_SCENE_MAX_COLLIDER_PARTS];
    ph_bound bounds[AF_PHYSICS_MAX_BODIES], ccd_bounds[AF_PHYSICS_MAX_BODIES];
    uint32_t cache_heads[16384], cache_next[AF_PHYSICS_MAX_CONTACTS];
    ph_joint joints[AF_SCENE_MAX_JOINTS];
    size_t joint_count;
    ph_body world;
    size_t parents[AF_PHYSICS_MAX_BODIES], island_sizes[AF_PHYSICS_MAX_BODIES];
    bool island_wake[AF_PHYSICS_MAX_BODIES], island_anchor[AF_PHYSICS_MAX_BODIES],
        island_quiet[AF_PHYSICS_MAX_BODIES];
    float island_time[AF_PHYSICS_MAX_BODIES];
    af_physics_stats stats;
    unsigned water_substep;
    float last_dt;
    double simulation_time;
    bool profiling;
    char error[256];
};
static inline double ph_clock(const af_physics *p) {
    return p->profiling ? 1000.0 * (double)clock() / CLOCKS_PER_SEC : 0;
}
static inline bool ph_finite(af_vec3 a) {
    return isfinite(a.x) && isfinite(a.y) && isfinite(a.z);
}
static inline float ph_get(af_vec3 a, unsigned i) {
    return i == 0 ? a.x : i == 1 ? a.y : a.z;
}
static inline af_vec3 ph_limit(af_vec3 a, float max) {
    float n = af_vec3_length(a);
    return n > max ? af_vec3_scale(a, (max * .999999f) / n) : a;
}
static inline af_vec3 ph_axis(unsigned i) {
    return (af_vec3){i == 0 ? 1.f : 0, i == 1 ? 1.f : 0, i == 2 ? 1.f : 0};
}
af_vec3 ph_local(const ph_body *, af_vec3);
af_vec3 ph_shape_local(const ph_body *, af_vec3);
af_vec3 ph_shape_vector(const ph_body *, af_vec3);
af_world_collider ph_part_world(const ph_body *, size_t);
af_vec3 ph_vector(const ph_body *, af_vec3);
af_vec3 ph_inverse_inertia(const ph_body *, af_vec3);
void ph_impulse(ph_body *, af_vec3, af_vec3);
void ph_wind_step(af_physics *,float);
void ph_shape(ph_body *);
bool ph_water_sync(af_physics *, const af_scene *);
void ph_water_step(af_physics *, float);
void ph_hold(af_physics *, float);
void ph_advance(af_physics *, float);
void ph_islands_build(af_physics *);
void ph_wake_island(af_physics *, size_t);
void ph_sleep(af_physics *, float);
bool ph_joints_sync(af_physics *);
bool ph_collision_allowed(const af_physics *, af_object_id, af_object_id);
void ph_joints_report(af_physics *);
void ph_joints_warm_start(af_physics *, float);
void ph_joints_solve(af_physics *, float, bool);
void ph_solve(af_physics *, float);
bool ph_emit(af_physics *, size_t, size_t, af_vec3, af_vec3, float);
bool ph_mesh_contacts(af_physics *, size_t, size_t);
void ph_bounds(af_physics *, ph_bound *, float, bool);
bool ph_bounds_overlap(ph_bound, ph_bound);
bool ph_detect(af_physics *);
bool ph_sync(af_physics *, af_scene *, float);
#endif
