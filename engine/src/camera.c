/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <afterlight/camera.h>
#include <math.h>

static bool finite_vector(af_vec3 value)
{
    return isfinite(value.x) && isfinite(value.y) && isfinite(value.z);
}

af_camera af_camera_default(void)
{
    return (af_camera){
        .position = {0, 0, 3}, .yaw = -AF_PI/2, .pitch = 0,
        .vertical_fov = AF_PI/3, .near_clip = 0.1f, .far_clip = 100
    };
}

bool af_camera_is_valid(const af_camera *camera)
{
    return camera && finite_vector(camera->position) &&
        isfinite(camera->yaw) && isfinite(camera->pitch) && fabsf(camera->pitch) < AF_PI/2 &&
        isfinite(camera->vertical_fov) && camera->vertical_fov > 0 && camera->vertical_fov < AF_PI &&
        isfinite(camera->near_clip) && camera->near_clip > 0 &&
        isfinite(camera->far_clip) && camera->far_clip > camera->near_clip;
}

af_vec3 af_camera_forward(const af_camera *camera)
{
    if (!af_camera_is_valid(camera)) return (af_vec3){0};
    float horizontal = cosf(camera->pitch);
    return (af_vec3){cosf(camera->yaw)*horizontal, sinf(camera->pitch), sinf(camera->yaw)*horizontal};
}

af_mat4 af_camera_view(const af_camera *camera)
{
    if (!af_camera_is_valid(camera)) return af_mat4_identity();
    return af_mat4_look_at(camera->position,
        af_vec3_add(camera->position, af_camera_forward(camera)), (af_vec3){0,1,0});
}

void af_camera_rotate(af_camera *camera, float yaw_delta, float pitch_delta)
{
    if (!af_camera_is_valid(camera) || !isfinite(yaw_delta) || !isfinite(pitch_delta)) return;
    /* Wrap before adding to avoid overflow for large finite deltas. */
    camera->yaw = remainderf(remainderf(camera->yaw, 2*AF_PI) + remainderf(yaw_delta, 2*AF_PI), 2*AF_PI);
    const float limit = AF_PI * (89.0f/180.0f);
    camera->pitch = fmaxf(-limit, fminf(limit, camera->pitch + pitch_delta));
}

void af_camera_move(af_camera *camera, af_vec3 local_motion)
{
    if (!af_camera_is_valid(camera) || !finite_vector(local_motion)) return;
    af_vec3 forward = af_camera_forward(camera);
    af_vec3 right = af_vec3_normalize(af_vec3_cross(forward, (af_vec3){0,1,0}));
    af_vec3 motion = af_vec3_add(af_vec3_scale(right, local_motion.x), af_vec3_scale(forward, local_motion.z));
    motion.y += local_motion.y;
    af_vec3 position = af_vec3_add(camera->position, motion);
    if (finite_vector(position)) camera->position = position;
}
