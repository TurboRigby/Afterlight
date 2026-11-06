/* SPDX-License-Identifier: MPL-2.0 */
#ifndef AFTERLIGHT_RIG_H
#define AFTERLIGHT_RIG_H
#include <afterlight/animation.h>
typedef struct af_character_rig {
    bool enabled,root_motion;
    uint32_t root_node,clips[5]; /* Idle, Walk, Run, Jump, Fall. Source glTF indices. */
    float walk_speed,run_speed,camera_distance,camera_height,turn_speed;
} af_character_rig;
af_character_rig af_character_rig_default(void);
bool af_character_rig_valid(af_character_rig rig);
#endif
