/* SPDX-License-Identifier: MPL-2.0 */
#ifndef AF_WATER_PARTICLES_INTERNAL_H
#define AF_WATER_PARTICLES_INTERNAL_H
#include "water_internal.h"
#define WA_PARTICLE_MAX 4096u
#define WA_HASH_SIZE 8192u
typedef struct wa_particle {
    af_vec3 p, previous, q, v, delta, normal, point;
    float lambda;
    size_t hit_body;
    int cell[3];
    uint32_t next;
} wa_particle;
struct wa_particles {
    wa_particle values[WA_PARTICLE_MAX];
    uint32_t heads[WA_HASH_SIZE];
    size_t count, capacity;
    float spacing, volume, initial_volume, emitted, escaped, pending;
    uint64_t births, geometry_key;
    float quiet_time;
    bool sleeping;
};
#endif
