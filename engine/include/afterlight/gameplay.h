/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef AFTERLIGHT_GAMEPLAY_H
#define AFTERLIGHT_GAMEPLAY_H
#include <afterlight/character.h>
#include <afterlight/camera.h>
typedef enum af_gameplay_role {AF_ROLE_NONE,AF_ROLE_SPAWN,AF_ROLE_CHECKPOINT,AF_ROLE_GOAL,AF_ROLE_RESTART,AF_ROLE_PLATFORM,AF_ROLE_COUNT} af_gameplay_role;
typedef struct af_player_component {
    bool enabled;
    uint64_t spawn;
    af_character_config controller;
    float mouse_sensitivity;
} af_player_component;
typedef struct af_camera_component {
    bool enabled,primary;
    float eye_height,vertical_fov,near_clip,far_clip;
} af_camera_component;
typedef struct af_gameplay_component {
    af_player_component player;
    af_camera_component camera;
    af_gameplay_role role;
    unsigned checkpoint_order;
} af_gameplay_component;
typedef struct af_gameplay_status {
    double elapsed,finish_time;
    unsigned checkpoint,checkpoint_count,deaths;
    bool running,finished;
} af_gameplay_status;
af_gameplay_component af_gameplay_component_default(void);
bool af_gameplay_component_valid(af_gameplay_component component);
#endif
