/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef AFTERLIGHT_MATH_H
#define AFTERLIGHT_MATH_H
#include <stdbool.h>

#define AF_PI 3.14159265358979323846f

typedef struct af_vec2 {
    float x, y;
} af_vec2;
typedef struct af_vec3 {
    float x, y, z;
} af_vec3;
typedef struct af_vec4 {
    float x, y, z, w;
} af_vec4;
/* Column-major storage, column vectors; A * B applies B first. Right-handed,
   Y-up world. Angles are radians. Perspective looks down -Z, maps depth to
   [0, 1], and flips clip Y for Vulkan's positive-height viewport. */
typedef struct af_mat4 {
    float elements[16];
} af_mat4;
/* Euler rotation in radians; matrix composition is T * Rz * Ry * Rx * S. */
typedef struct af_transform {
    af_vec3 position;
    af_vec3 rotation;
    af_vec3 scale;
} af_transform;

af_vec3 af_vec3_add(af_vec3 a, af_vec3 b);
af_vec3 af_vec3_sub(af_vec3 a, af_vec3 b);
af_vec3 af_vec3_scale(af_vec3 v, float scale);
float af_vec3_dot(af_vec3 a, af_vec3 b);
af_vec3 af_vec3_cross(af_vec3 a, af_vec3 b);
float af_vec3_length(af_vec3 v);
/* Returns zero for a zero-length or nonfinite vector. */
af_vec3 af_vec3_normalize(af_vec3 v);
af_mat4 af_mat4_identity(void);
af_mat4 af_mat4_mul(af_mat4 a, af_mat4 b);
/* Singular/nonfinite matrices fail without changing output. */
bool af_mat4_inverse(af_mat4 matrix, af_mat4 *output);
af_vec4 af_mat4_transform(af_mat4 matrix, af_vec4 vector);
af_mat4 af_mat4_translation(af_vec3 translation);
af_mat4 af_mat4_scale(af_vec3 scale);
af_mat4 af_mat4_rotation_x(float radians);
af_mat4 af_mat4_rotation_y(float radians);
af_mat4 af_mat4_rotation_z(float radians);
af_transform af_transform_identity(void);
af_mat4 af_transform_matrix(af_transform transform);
/* Invalid frusta (including nonfinite arguments) produce a zero matrix. */
af_mat4 af_mat4_perspective(float vertical_fov, float aspect, float near_clip, float far_clip);
/* Degenerate direction/up vectors produce identity. */
af_mat4 af_mat4_look_at(af_vec3 eye, af_vec3 target, af_vec3 up);

#endif
