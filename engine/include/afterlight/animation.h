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
typedef struct af_animation_clip {char name[64];double duration;af_animation_channel *channels;size_t channel_count;} af_animation_clip;
typedef struct af_animation_file {
    af_animation_pose *bind_pose;
    int32_t *parents;size_t node_count;
    af_animation_clip *clips;size_t clip_count;
} af_animation_file;
bool af_animation_file_load(const char *path,af_animation_file *out,af_asset_error *error);
void af_animation_file_free(af_animation_file *file);
/* Output has file->node_count entries. Time clamps per channel. */
bool af_animation_sample(const af_animation_file *file,size_t clip,double time,af_animation_pose *out,size_t capacity);
af_vec4 af_quaternion_normalize(af_vec4 q);
af_vec4 af_quaternion_slerp(af_vec4 a,af_vec4 b,float t);
af_vec4 af_quaternion_from_euler(af_vec3 radians);
af_mat4 af_animation_pose_matrix(af_animation_pose pose);
bool af_animation_world_matrices(const af_animation_file *file,const af_animation_pose *poses,af_mat4 *world,size_t capacity);
#endif
