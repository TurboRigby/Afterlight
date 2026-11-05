/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <afterlight/math.h>
#include <math.h>
#include <float.h>

af_vec3 af_vec3_add(af_vec3 a, af_vec3 b) { return (af_vec3){a.x+b.x, a.y+b.y, a.z+b.z}; }
af_vec3 af_vec3_sub(af_vec3 a, af_vec3 b) { return (af_vec3){a.x-b.x, a.y-b.y, a.z-b.z}; }
af_vec3 af_vec3_scale(af_vec3 v, float s) { return (af_vec3){v.x*s, v.y*s, v.z*s}; }
float af_vec3_dot(af_vec3 a, af_vec3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
af_vec3 af_vec3_cross(af_vec3 a, af_vec3 b)
{
    return (af_vec3){a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x};
}
float af_vec3_length(af_vec3 v) { return hypotf(hypotf(v.x, v.y), v.z); }
af_vec3 af_vec3_normalize(af_vec3 v)
{
    float length = af_vec3_length(v);
    return length > 0 && isfinite(length) ? (af_vec3){v.x/length, v.y/length, v.z/length} : (af_vec3){0};
}

af_mat4 af_mat4_identity(void)
{
    return (af_mat4){.elements = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}};
}

af_mat4 af_mat4_mul(af_mat4 a, af_mat4 b)
{
    af_mat4 result = {0};
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            for (int k = 0; k < 4; ++k)
                result.elements[column*4+row] += a.elements[k*4+row] * b.elements[column*4+k];
    return result;
}

af_vec4 af_mat4_transform(af_mat4 matrix, af_vec4 vector)
{
    const float *m = matrix.elements;
    return (af_vec4){
        m[0]*vector.x + m[4]*vector.y + m[8]*vector.z + m[12]*vector.w,
        m[1]*vector.x + m[5]*vector.y + m[9]*vector.z + m[13]*vector.w,
        m[2]*vector.x + m[6]*vector.y + m[10]*vector.z + m[14]*vector.w,
        m[3]*vector.x + m[7]*vector.y + m[11]*vector.z + m[15]*vector.w
    };
}

af_mat4 af_mat4_translation(af_vec3 translation)
{
    af_mat4 result = af_mat4_identity();
    result.elements[12] = translation.x;
    result.elements[13] = translation.y;
    result.elements[14] = translation.z;
    return result;
}

af_mat4 af_mat4_scale(af_vec3 scale)
{
    af_mat4 result = af_mat4_identity();
    result.elements[0] = scale.x;
    result.elements[5] = scale.y;
    result.elements[10] = scale.z;
    return result;
}

af_mat4 af_mat4_rotation_x(float radians)
{
    af_mat4 result = af_mat4_identity();
    result.elements[5] = result.elements[10] = cosf(radians);
    result.elements[6] = sinf(radians);
    result.elements[9] = -sinf(radians);
    return result;
}

af_mat4 af_mat4_rotation_y(float radians)
{
    af_mat4 result = af_mat4_identity();
    result.elements[0] = result.elements[10] = cosf(radians);
    result.elements[8] = sinf(radians);
    result.elements[2] = -sinf(radians);
    return result;
}

af_mat4 af_mat4_rotation_z(float radians)
{
    af_mat4 result = af_mat4_identity();
    result.elements[0] = result.elements[5] = cosf(radians);
    result.elements[1] = sinf(radians);
    result.elements[4] = -sinf(radians);
    return result;
}

af_transform af_transform_identity(void)
{
    return (af_transform){.scale = {1,1,1}};
}

af_mat4 af_transform_matrix(af_transform transform)
{
    af_mat4 rotation = af_mat4_mul(af_mat4_rotation_z(transform.rotation.z),
        af_mat4_mul(af_mat4_rotation_y(transform.rotation.y), af_mat4_rotation_x(transform.rotation.x)));
    return af_mat4_mul(af_mat4_translation(transform.position),
        af_mat4_mul(rotation, af_mat4_scale(transform.scale)));
}

af_mat4 af_mat4_perspective(float fov, float aspect, float near_clip, float far_clip)
{
    af_mat4 result = {0};
    if (!isfinite(fov) || !isfinite(aspect) || !isfinite(near_clip) || !isfinite(far_clip) ||
        fov <= 0 || fov >= AF_PI || aspect <= 0 || near_clip <= 0 || far_clip <= near_clip) return result;
    float focal = 1.0f / tanf(fov * 0.5f);
    result.elements[0] = focal / aspect;
    result.elements[5] = -focal;
    result.elements[10] = far_clip / (near_clip - far_clip);
    result.elements[11] = -1;
    result.elements[14] = near_clip * result.elements[10];
    return result;
}

af_mat4 af_mat4_look_at(af_vec3 eye, af_vec3 target, af_vec3 up)
{
    af_vec3 forward = af_vec3_normalize(af_vec3_sub(target, eye));
    af_vec3 right = af_vec3_normalize(af_vec3_cross(forward, up));
    if (af_vec3_length(forward) == 0 || af_vec3_length(right) == 0) return af_mat4_identity();
    af_vec3 vertical = af_vec3_cross(right, forward);
    return (af_mat4){.elements = {
        right.x, vertical.x, -forward.x, 0,
        right.y, vertical.y, -forward.y, 0,
        right.z, vertical.z, -forward.z, 0,
        -af_vec3_dot(right, eye), -af_vec3_dot(vertical, eye), af_vec3_dot(forward, eye), 1
    }};
}

bool af_mat4_inverse(af_mat4 matrix, af_mat4 *output)
{
    if (!output) return false;
    double work[4][8] = {{0}};
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            float value = matrix.elements[column*4+row];
            if (!isfinite(value)) return false;
            work[row][column] = value;
        }
        work[row][row+4] = 1;
    }
    for (int column = 0; column < 4; ++column) {
        int pivot = column;
        for (int row = column+1; row < 4; ++row)
            if (fabs(work[row][column]) > fabs(work[pivot][column])) pivot = row;
        if (work[pivot][column] == 0) return false;
        if (pivot != column) for (int j = 0; j < 8; ++j) {
            double swap = work[pivot][j]; work[pivot][j] = work[column][j]; work[column][j] = swap;
        }
        double divisor = work[column][column];
        for (int j = 0; j < 8; ++j) work[column][j] /= divisor;
        for (int row = 0; row < 4; ++row) if (row != column) {
            double factor = work[row][column];
            for (int j = 0; j < 8; ++j) work[row][j] -= factor * work[column][j];
        }
    }
    af_mat4 result;
    for (int row = 0; row < 4; ++row) for (int column = 0; column < 4; ++column) {
        double value = work[row][column+4];
        if (!isfinite(value) || fabs(value) > FLT_MAX) return false;
        result.elements[column*4+row] = (float)value;
    }
    *output = result;
    return true;
}
