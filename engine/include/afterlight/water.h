/* SPDX-License-Identifier: MPL-2.0 */
#ifndef AFTERLIGHT_WATER_H
#define AFTERLIGHT_WATER_H
#include <afterlight/scene.h>
#define AF_SCENE_MAX_WATERS 8u
#define AF_WATER_MAX_PATH 8u
typedef enum af_water_mode { AF_WATER_POOL, AF_WATER_RIVER, AF_WATER_PARTICLES } af_water_mode;
typedef enum af_water_quality { AF_WATER_LOW, AF_WATER_MEDIUM, AF_WATER_HIGH } af_water_quality;
/* Metres, kilograms, seconds. Surface owner: root node, unit scale, yaw only.
   Definitions are saved; waves belong to the physics world and reset on Stop. */
typedef struct af_water {
    af_object_id owner;
    bool enabled;
    af_water_mode mode;
    af_water_quality quality;
    af_vec2 half_size;
    float depth, density, drag, wave_speed, damping, wind;
    af_vec3 absorption, scattering;
    float roughness, ior;
    float flow_speed;
    unsigned path_count;
    af_vec3 path[AF_WATER_MAX_PATH];
    float particle_spacing, initial_volume, emission_rate, viscosity;
    af_vec3 emission_velocity;
    float ripple_scale,ripple_strength;
} af_water;
typedef struct af_water_sample {
    bool inside;
    af_object_id owner;
    float height, bottom, density;
    af_vec3 velocity, normal;
} af_water_sample;
typedef struct af_water_stats {
    size_t surfaces, cells, wet_bodies, particles,sleeping_surfaces,sleeping_particles;
    float max_wave, displaced_volume;
    double milliseconds;
    float emitted_volume, escaped_volume, particle_volume;
} af_water_stats;
typedef struct af_water_particle {
    af_vec3 position, velocity;
    float volume;
} af_water_particle;
typedef struct af_physics af_physics;
size_t af_physics_water_particles(const af_physics *, af_object_id, af_water_particle *, size_t);
af_water af_water_default(void);
bool af_water_valid(af_water);
size_t af_scene_water_count(const af_scene *);
const af_water *af_scene_water_at(const af_scene *, size_t);
const af_water *af_scene_get_water(const af_scene *, af_object_id);
bool af_scene_set_water(af_scene *, af_object_id, af_water);
bool af_scene_remove_water(af_scene *, af_object_id);
/* Surface query uses XZ; caller compares Y to height/bottom. Disabled water is skipped. */
af_water_sample af_physics_sample_water(const af_physics *, const af_scene *, af_vec3);
bool af_physics_disturb_water(af_physics *, const af_scene *, af_object_id, af_vec3, float radius,
                              float velocity);
af_water_stats af_physics_water_stats(const af_physics *);
#endif
