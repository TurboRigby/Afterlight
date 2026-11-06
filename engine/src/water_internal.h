/* SPDX-License-Identifier: MPL-2.0 */
#ifndef AF_WATER_INTERNAL_H
#define AF_WATER_INTERNAL_H
typedef struct cJSON cJSON;
#include <afterlight/water.h>
#define AF_WATER_GRID_MAX 64u
typedef struct wa_particles wa_particles;
typedef struct wa_slot {
    wa_particles *particles;
    af_water config;
    af_transform transform;
    unsigned n;
    float time, quiet_time;
    bool sleeping;
    float h[AF_WATER_GRID_MAX * AF_WATER_GRID_MAX];
    float previous[AF_WATER_GRID_MAX * AF_WATER_GRID_MAX];
    float v[AF_WATER_GRID_MAX * AF_WATER_GRID_MAX];
} wa_slot;
typedef struct af_water_runtime {
    wa_slot slots[AF_SCENE_MAX_WATERS];
    size_t count;
    af_water_stats stats;
    uint64_t key, disturbance;
} af_water_runtime;
float wa_river_length(const af_water *);
af_vec3 wa_river_point(const af_water *, float, af_vec3 *);
bool wa_river_coordinates(const af_water *, af_vec3, float *, float *, float *, af_vec3 *);
bool wa_owner_valid(const af_scene *, af_object_id, af_water);
bool wa_decode(af_scene *, const cJSON *);
bool wa_encode(const af_scene *, cJSON *);
void wa_destroy(af_water_runtime *);
void wa_particles_free(wa_particles *);
bool wa_particles_sync(wa_slot *);
void ph_particles_step(af_physics *, wa_slot *, float);
bool wa_particles_mesh(const wa_slot *, float, af_vertex **, uint32_t **, af_mesh_data *);
bool wa_sync(af_water_runtime *, const af_scene *);
void wa_step(af_water_runtime *, float);
af_water_sample wa_sample(const af_water_runtime *, const af_scene *, af_vec3, float);
bool wa_contains(const af_water_runtime *, const af_scene *, af_object_id, af_vec3);
bool wa_disturb(af_water_runtime *, const af_scene *, af_object_id, af_vec3, float, float);
bool wa_mesh(const af_water_runtime *, const af_scene *, af_object_id, float, af_vertex **, uint32_t **,
             af_mesh_data *);
const af_water_runtime *ph_water_runtime(const af_physics *);
#endif
