/* SPDX-License-Identifier: MPL-2.0 */
#ifndef AF_PHYSICS_INTERNAL_H
#define AF_PHYSICS_INTERNAL_H
#include <afterlight/physics.h>
#include <afterlight/animation.h>
#include <math.h>
typedef struct ph_body {af_object_id id;size_t scene_index;af_rigid_body config;af_transform published;af_world_collider shape;af_vec3 p,v,w,force,torque,inertia;af_vec4 q;float inv_mass,sleep_time;bool sleeping,supported,external;} ph_body;
typedef struct ph_contact {af_physics_contact view;size_t a,b;af_vec3 local_a,local_b,tangent;float normal,bounce;} ph_contact;
typedef struct ph_bound {size_t body;af_vec3 low,high;} ph_bound;
struct af_physics {af_object_id held;af_vec3 grab_local,grab_target;af_scene *scene;ph_body bodies[AF_PHYSICS_MAX_BODIES],scratch[AF_PHYSICS_MAX_BODIES];size_t count;ph_contact contacts[AF_PHYSICS_MAX_CONTACTS],cache[AF_PHYSICS_MAX_CONTACTS];size_t contact_count,cache_count;ph_bound bounds[AF_PHYSICS_MAX_BODIES];af_physics_stats stats;float last_dt;char error[256];};
static inline bool ph_finite(af_vec3 a){return isfinite(a.x)&&isfinite(a.y)&&isfinite(a.z);}
static inline float ph_get(af_vec3 a,unsigned i){return i==0?a.x:i==1?a.y:a.z;}
static inline af_vec3 ph_limit(af_vec3 a,float max){float n=af_vec3_length(a);return n>max?af_vec3_scale(a,max/n):a;}
static inline af_vec3 ph_axis(unsigned i){return (af_vec3){i==0?1.f:0,i==1?1.f:0,i==2?1.f:0};}
af_vec3 ph_local(const ph_body*,af_vec3);
af_vec3 ph_vector(const ph_body*,af_vec3);
af_vec3 ph_inverse_inertia(const ph_body*,af_vec3);
void ph_impulse(ph_body*,af_vec3,af_vec3);
void ph_shape(ph_body*);
void ph_hold(af_physics*,float);
void ph_move(af_physics*,size_t,float);
void ph_sleep(af_physics*,float);
void ph_solve(af_physics*,float);
bool ph_detect(af_physics*);
bool ph_sync(af_physics*,af_scene*,float);
#endif
