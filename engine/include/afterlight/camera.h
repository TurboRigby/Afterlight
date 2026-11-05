/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef AFTERLIGHT_CAMERA_H
#define AFTERLIGHT_CAMERA_H

#include <stdbool.h>
#include <afterlight/math.h>

typedef struct af_camera {
    af_vec3 position;
    float yaw;
    float pitch;
    float vertical_fov;
    float near_clip;
    float far_clip;
} af_camera;

/* Radians; default is (0,0,3), looking down -Z with a 60-degree vertical FOV. */
af_camera af_camera_default(void);
bool af_camera_is_valid(const af_camera *camera);
af_vec3 af_camera_forward(const af_camera *camera);
af_mat4 af_camera_view(const af_camera *camera);
/* Rotation clamps pitch short of the poles and wraps yaw. Local movement is
   (right, world up, forward); forward includes pitch for a flying camera. */
void af_camera_rotate(af_camera *camera, float yaw_delta, float pitch_delta);
void af_camera_move(af_camera *camera, af_vec3 local_motion);

#endif
