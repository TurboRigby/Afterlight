/* SPDX-License-Identifier: MPL-2.0 */
#include <afterlight/rig.h>
#include <math.h>
af_character_rig af_character_rig_default(void) {
    return (af_character_rig){.clips = {0, 1, 2, 3, 4},
                              .walk_speed = 2,
                              .run_speed = 5,
                              .camera_distance = 5.5f,
                              .camera_height = .65f,
                              .turn_speed = 12};
}
bool af_character_rig_valid(af_character_rig r) {
    float v[] = {r.walk_speed, r.run_speed, r.camera_distance, r.camera_height, r.turn_speed};
    for (unsigned i = 0; i < 5; ++i)
        if (!isfinite(v[i]))
            return false;
    for (unsigned i = 0; i < 5; ++i)
        if (r.clips[i] >= AF_ANIMATION_MAX_CLIPS)
            return false;
    return r.root_node < AF_ANIMATION_MAX_NODES && r.walk_speed > 0 && r.run_speed > r.walk_speed &&
           r.run_speed <= 100 && r.camera_distance >= .2f && r.camera_distance <= 30 && r.camera_height >= 0 &&
           r.camera_height <= 10 && r.turn_speed >= 0 && r.turn_speed <= 100;
}
