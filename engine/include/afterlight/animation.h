/* SPDX-License-Identifier: MPL-2.0 */
#ifndef AFTERLIGHT_ANIMATION_H
#define AFTERLIGHT_ANIMATION_H
#include <afterlight/asset.h>

#define AF_ANIMATION_MAX_NODES 16384u
#define AF_ANIMATION_MAX_CLIPS 128u
#define AF_ANIMATION_MAX_JOINTS 64u
/* Quaternion is xyzw. Pose composition is T * quaternion rotation * S. */
typedef struct af_animation_pose {af_vec3 translation,scale;af_vec4 rotation;} af_animation_pose;
typedef enum af_animation_path {AF_ANIM_TRANSLATION,AF_ANIM_ROTATION,AF_ANIM_SCALE} af_animation_path;
typedef enum af_animation_interpolation {AF_ANIM_LINEAR,AF_ANIM_STEP,AF_ANIM_CUBIC} af_animation_interpolation;
typedef struct af_animation_channel {
    uint32_t node;
    af_animation_path path;
    af_animation_interpolation interpolation;
    size_t key_count;
    float *times;
    af_vec4 *values; /* CUBIC: in tangent, value, out tangent for each key. */
} af_animation_channel;
#define AF_ANIMATION_MAX_EVENTS 64u
#define AF_ANIMATION_EVENT_BATCH 16u
typedef struct af_animation_event {char name[32];double time;} af_animation_event;
typedef struct af_animation_events {af_animation_event items[AF_ANIMATION_EVENT_BATCH];size_t count;uint64_t dropped;} af_animation_events;
typedef struct af_animation_clip {char name[64];double duration;af_animation_channel *channels;size_t channel_count;af_animation_event events[AF_ANIMATION_MAX_EVENTS];size_t event_count;} af_animation_clip;
/* Crossing interval excludes the old time and includes the new time. Loop
   markers use [0,duration); reverse playback preserves traversal order. */
bool af_animation_collect_events(const af_animation_clip *clip,double time,double delta,bool loop,af_animation_events *out);
typedef struct af_animation_skin {uint32_t joints[AF_ANIMATION_MAX_JOINTS];af_mat4 inverse_bind[AF_ANIMATION_MAX_JOINTS];size_t joint_count;} af_animation_skin;
typedef struct af_animation_file {
    af_animation_pose *bind_pose;
    int32_t *parents;size_t node_count;
    af_animation_clip *clips;size_t clip_count;
    af_animation_skin *skins;size_t skin_count;
} af_animation_file;
bool af_animation_file_load(const char *path,af_animation_file *out,af_asset_error *error);
void af_animation_file_free(af_animation_file *file);
/* Output has file->node_count entries. Time clamps per channel. */
bool af_animation_sample(const af_animation_file *file,size_t clip,double time,af_animation_pose *out,size_t capacity);
/* Extracts translation across loop boundaries, including full cycles. Rotation
   and vertical character physics remain caller-controlled. No pose allocation. */
bool af_animation_root_delta(const af_animation_file *file,size_t clip,uint32_t node,double time,double delta,bool loop,af_vec3 *out);
af_vec4 af_quaternion_normalize(af_vec4 q);
af_vec4 af_quaternion_slerp(af_vec4 a,af_vec4 b,float t);
af_vec4 af_quaternion_from_euler(af_vec3 radians);
af_mat4 af_animation_pose_matrix(af_animation_pose pose);
bool af_animation_world_matrices(const af_animation_file *file,const af_animation_pose *poses,af_mat4 *world,size_t capacity);
/* Mesh-local palette: inverse(mesh world) * joint world * inverse bind. */
bool af_animation_skin_palette(const af_animation_file *file,size_t skin,uint32_t mesh_node,const af_mat4 *world,af_mat4 *palette,size_t capacity);
/* Owned playback buffers; initialize with default and destroy once. Do not
   copy a live player by value. File data remains borrowed for each call. */
typedef struct af_animation_player {
    size_t clip,previous_clip;
    double time,previous_time;
    float speed,fade_duration,fade_elapsed;
    bool playing,loop,finished,fading;
    af_animation_pose *blend_pose,*frozen_pose;size_t pose_capacity,frozen_count;
} af_animation_player;
af_animation_player af_animation_player_default(void);
void af_animation_player_destroy(af_animation_player *player);
bool af_animation_player_play(af_animation_player *player,const af_animation_file *file,size_t clip,float blend_seconds);
bool af_animation_player_seek(af_animation_player *player,const af_animation_file *file,double seconds);
bool af_animation_player_update(af_animation_player *player,const af_animation_file *file,double delta);
bool af_animation_player_sample(af_animation_player *player,const af_animation_file *file,af_animation_pose *out,size_t capacity);
void af_animation_player_pause(af_animation_player *player,bool paused);
void af_animation_player_stop(af_animation_player *player);
#endif
