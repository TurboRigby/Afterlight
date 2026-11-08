/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef AFTERLIGHT_CHARACTER_H
#define AFTERLIGHT_CHARACTER_H
#include <afterlight/collision.h>
typedef struct af_character_config {
    float radius, half_height, speed, jump_speed, gravity, slope_limit, skin, ground_snap;
} af_character_config;
typedef struct af_character {
    af_vec3 position, velocity;
    bool grounded;
    uint64_t ground;
    af_vec3 ground_local, ground_world;
} af_character;
typedef struct af_character_input {
    af_vec3 move;
    bool jump;
} af_character_input;
af_character_config af_character_config_default(void);
bool af_character_config_valid(af_character_config config);
/* Kinematic upright capsule; input move is a world-space XZ direction of length <= 1.
   Use fixed ticks. Static/kinematic scene colliders, no rigid-body impulses. */
bool af_character_step(const struct af_scene *scene, af_character *character, af_character_config config,
                       af_character_input input, float delta, af_query_filter filter);
/* World-space horizontal animation displacement goes through the same sweep,
   sliding, slopes and moving-platform logic. Gravity/jump still own Y. */
bool af_character_step_root_motion(const struct af_scene *scene, af_character *character, af_character_config config,
                                   af_vec3 displacement, bool jump, float delta, af_query_filter filter);
#endif
