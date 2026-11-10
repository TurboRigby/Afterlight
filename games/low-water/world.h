/* SPDX-License-Identifier: MPL-2.0 */
#ifndef LOW_WATER_WORLD_H
#define LOW_WATER_WORLD_H

#include <afterlight/afterlight.h>

typedef struct tide_world {
    af_scene *scene;
    af_object_id cover[2], gate, pump_lamp, gate_lamp, signal;
    af_water basin;
    size_t pump_light, archive_light;
} tide_world;

extern const af_vec3 tide_spawn, tide_valve, tide_sight, tide_keys, tide_archive, tide_manifest, tide_slip;
extern const af_camera tide_optics;

bool tide_world_build(tide_world *world, af_engine *engine, const char *directory, bool rays, bool cinematic);
bool tide_world_pump(tide_world *world, bool on);
bool tide_world_archive(tide_world *world, bool open);
bool tide_world_transmit(tide_world *world, unsigned choice);
void tide_world_animate(tide_world *world, float cover, float gate);

#endif
