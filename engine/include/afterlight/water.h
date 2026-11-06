/* SPDX-License-Identifier: MPL-2.0 */
#ifndef AFTERLIGHT_WATER_H
#define AFTERLIGHT_WATER_H
#include <afterlight/scene.h>
#define AF_SCENE_MAX_WATERS 8u
#define AF_WATER_MAX_PATH 8u
typedef enum af_water_mode { AF_WATER_POOL } af_water_mode;
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
} af_water;
typedef struct af_water_sample {
    bool inside;
    af_object_id owner;
    float height, bottom, density;
    af_vec3 velocity, normal;
} af_water_sample;
typedef struct af_water_stats {
    size_t surfaces, cells, wet_bodies, particles;
    float max_wave, displaced_volume;
    double milliseconds;
} af_water_stats;
typedef struct af_physics af_physics;
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
