/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef AF_SCENE_INTERNAL_H
#define AF_SCENE_INTERNAL_H
#include <afterlight/scene.h>
#include "collision_mesh_internal.h"
#include <afterlight/joint.h>
#include <afterlight/compound.h>
#include <afterlight/water.h>
#include <afterlight/landscape.h>

struct af_scene {
    cm_slot collision_meshes[AF_COLLISION_MESH_MAX_ASSETS];
    size_t collision_mesh_count;
    af_joint joints[AF_SCENE_MAX_JOINTS];
    size_t joint_count;
    af_joint_id next_joint_id;
    af_collider_part parts[AF_SCENE_MAX_COLLIDER_PARTS];
    size_t part_count;
    af_collider_part_id next_part_id;
    af_water waters[AF_SCENE_MAX_WATERS];
    size_t water_count;
    af_wind wind;
    af_landscape *landscape;
    size_t *object_slots;
    size_t object_slot_capacity;
    af_scene_object *objects;
    size_t count;
    size_t capacity;
    af_object_id next_id;
    af_scene_asset *assets;
    size_t asset_count, asset_capacity;
    af_directional_light light;
    af_shadow_settings shadows;
    af_render_settings render_settings;
    af_local_light local_lights[AF_SCENE_MAX_LOCAL_LIGHTS];
    size_t local_light_count;
};
bool af_scene_reindex(af_scene *);
bool af_scene_joints_valid(const af_scene *);
#endif
