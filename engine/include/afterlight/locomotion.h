/* SPDX-License-Identifier: MPL-2.0 */
#ifndef AFTERLIGHT_LOCOMOTION_H
#define AFTERLIGHT_LOCOMOTION_H
#include <afterlight/character.h>
#include <afterlight/animation.h>
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
/* Owned sampling buffers. Do not copy a live player; destroy once. Clip names
   must be Idle, Walk, Run, Jump, Fall. Missing clips fail initialization. */
typedef struct af_locomotion_player {
    af_locomotion motion;
    size_t clips[5],node_count;
    float weights[3];
    af_animation_pose *scratch,*frozen;
    af_animation_events events;
    bool ready;
} af_locomotion_player;
bool af_locomotion_player_init(af_locomotion_player *player,const af_animation_file *file);
void af_locomotion_player_destroy(af_locomotion_player *player);
bool af_locomotion_player_step(af_locomotion_player *player,const af_animation_file *file,af_locomotion_input input,float delta);
bool af_locomotion_player_sample(af_locomotion_player *player,const af_animation_file *file,af_animation_pose *out,size_t capacity);
#endif
