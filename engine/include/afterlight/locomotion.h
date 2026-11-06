/* SPDX-License-Identifier: MPL-2.0 */
#ifndef AFTERLIGHT_LOCOMOTION_H
#define AFTERLIGHT_LOCOMOTION_H
#include <afterlight/character.h>
typedef enum af_locomotion_state {AF_LOCOMOTION_IDLE,AF_LOCOMOTION_WALK,AF_LOCOMOTION_RUN,AF_LOCOMOTION_JUMP,AF_LOCOMOTION_FALL} af_locomotion_state;
typedef struct af_locomotion_input {float speed,vertical_speed;bool grounded;} af_locomotion_input;
typedef struct af_locomotion {
    af_locomotion_state state,previous;
    float speed,walk_speed,run_speed,transition;
    double time,phase;
    bool initialized,landed,took_off;
} af_locomotion;
af_locomotion af_locomotion_default(void);
/* Call after collision on fixed ticks. Speed is measured horizontal displacement,
   excluding platform carry. Hysteresis prevents noise at rest and run thresholds. */
bool af_locomotion_update(af_locomotion *motion,af_locomotion_input input,float delta);
const char *af_locomotion_state_name(af_locomotion_state state);
#endif
