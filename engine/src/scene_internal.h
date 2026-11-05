/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef AF_SCENE_INTERNAL_H
#define AF_SCENE_INTERNAL_H
#include <afterlight/scene.h>

struct af_scene {
    af_scene_object *objects;
    size_t count;
    size_t capacity;
    af_object_id next_id;
    af_scene_asset *assets;
    size_t asset_count,asset_capacity;
    af_directional_light light;
    af_shadow_settings shadows;
    af_render_settings render_settings;
    af_local_light local_lights[AF_SCENE_MAX_LOCAL_LIGHTS];
    size_t local_light_count;
};
#endif
