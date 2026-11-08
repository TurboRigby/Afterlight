/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "shadow_math.h"
#include <float.h>
#include <math.h>
#include <string.h>
af_shadow_settings af_shadow_settings_default(void) {
    return (af_shadow_settings){.enabled = true,
                                .mode = AF_SHADOW_CASCADED,
                                .resolution = 2048,
                                .distance = 40,
                                .constant_bias = 1.25f,
                                .slope_bias = 1.75f,
                                .normal_bias = 0.015f,
                                .filter_radius = 1,
                                .split_lambda = 0.65f,
                                .blend_fraction = 0.1f};
}
bool af_shadow_settings_valid(af_shadow_settings s) {
    return (s.mode == AF_SHADOW_SINGLE || s.mode == AF_SHADOW_CASCADED) &&
           (s.resolution == 512 || s.resolution == 1024 || s.resolution == 2048 || s.resolution == 4096) &&
           isfinite(s.distance) && s.distance >= 1 && s.distance <= 10000 && isfinite(s.constant_bias) &&
           s.constant_bias >= 0 && s.constant_bias <= 16 && isfinite(s.slope_bias) && s.slope_bias >= 0 &&
           s.slope_bias <= 16 && isfinite(s.normal_bias) && s.normal_bias >= 0 && s.normal_bias <= 1 &&
           s.filter_radius <= 2 && isfinite(s.split_lambda) && s.split_lambda >= 0 && s.split_lambda <= 1 &&
           isfinite(s.blend_fraction) && s.blend_fraction >= 0 && s.blend_fraction <= 0.3f;
}
af_shadow_bounds af_shadow_transform_bounds(af_mat4 model, af_vec3 minimum, af_vec3 maximum) {
    af_shadow_bounds bounds;
    for (unsigned i = 0; i < 8; ++i) {
        af_vec4 p = af_mat4_transform(model, (af_vec4){i & 1 ? maximum.x : minimum.x, i & 2 ? maximum.y : minimum.y,
                                                       i & 4 ? maximum.z : minimum.z, 1});
        bounds.corners[i] = (af_vec3){p.x, p.y, p.z};
    }
    return bounds;
}
bool af_shadow_intersects(af_mat4 matrix, const af_shadow_bounds *bounds) {
    unsigned outside[6] = {0};
    for (unsigned i = 0; i < 8; ++i) {
        af_vec3 p = bounds->corners[i];
        af_vec4 q = af_mat4_transform(matrix, (af_vec4){p.x, p.y, p.z, 1});
        if (!isfinite(q.x) || !isfinite(q.y) || !isfinite(q.z))
            return false;
        outside[0] += (unsigned)(q.x < -q.w);
        outside[1] += (unsigned)(q.x > q.w);
        outside[2] += (unsigned)(q.y < -q.w);
        outside[3] += (unsigned)(q.y > q.w);
        outside[4] += (unsigned)(q.z < 0);
        outside[5] += (unsigned)(q.z > q.w);
    }
    for (unsigned i = 0; i < 6; ++i)
        if (outside[i] == 8)
            return false;
    return true;
}
static af_mat4 orthographic(float left, float right, float bottom, float top, float near, float far) {
    af_mat4 m = {0};
    m.elements[0] = 2 / (right - left);
    m.elements[5] = -2 / (top - bottom);
    m.elements[10] = 1 / (near - far);
    m.elements[12] = -(right + left) / (right - left);
    m.elements[13] = (top + bottom) / (top - bottom);
    m.elements[14] = near / (near - far);
    m.elements[15] = 1;
    return m;
}
static af_vec3 transform_point(af_mat4 m, af_vec3 p) {
    af_vec4 q = af_mat4_transform(m, (af_vec4){p.x, p.y, p.z, 1});
    return (af_vec3){q.x, q.y, q.z};
}
static bool fit_slice(const af_camera *camera, float aspect, af_vec3 direction, uint32_t resolution, float near,
                      float far, const af_shadow_bounds *casters, size_t count, af_mat4 *out, float *texel_size) {
    af_vec3 forward = af_camera_forward(camera),
            center = af_vec3_add(camera->position, af_vec3_scale(forward, (near + far) / 2));
    float height = far * tanf(camera->vertical_fov / 2), width = height * aspect;
    float radius = sqrtf(width * width + height * height + (far - near) * (far - near) / 4);
    radius = ceilf(radius * 16) / 16;
    radius *= (float)resolution / (float)(resolution - 2);
    if (!isfinite(radius) || radius <= 0)
        return false;
    af_vec3 up = fabsf(direction.y) > 0.95f ? (af_vec3){0, 0, 1} : (af_vec3){0, 1, 0};
    af_mat4 view = af_mat4_look_at((af_vec3){0}, direction, up);
    af_vec3 light_center = transform_point(view, center);
    float texel = 2 * radius / (float)resolution;
    float x = roundf(light_center.x / texel) * texel, y = roundf(light_center.y / texel) * texel;
    float z_min = light_center.z - radius, z_max = light_center.z + radius;
    for (size_t i = 0; i < count; ++i) {
        float lo[3] = {FLT_MAX, FLT_MAX, FLT_MAX}, hi[3] = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
        for (unsigned j = 0; j < 8; ++j) {
            af_vec3 p = transform_point(view, casters[i].corners[j]);
            float v[] = {p.x, p.y, p.z};
            for (unsigned k = 0; k < 3; ++k) {
                lo[k] = fminf(lo[k], v[k]);
                hi[k] = fmaxf(hi[k], v[k]);
            }
        }
        if (hi[0] < x - radius || lo[0] > x + radius || hi[1] < y - radius || lo[1] > y + radius)
            continue;
        z_min = fminf(z_min, lo[2]);
        z_max = fmaxf(z_max, hi[2]);
    }
    float padding = fmaxf(2, radius * 0.1f);
    view.elements[14] = -(z_max + padding + 0.1f);
    af_mat4 projection =
        orthographic(x - radius, x + radius, y - radius, y + radius, 0.1f, z_max - z_min + 2 * padding + 0.1f);
    *out = af_mat4_mul(projection, view);
    *texel_size = texel;
    for (unsigned i = 0; i < 16; ++i)
        if (!isfinite(out->elements[i]))
            return false;
    return true;
}
bool af_shadow_build(const af_camera *camera, float aspect, af_vec3 direction, af_shadow_settings settings,
                     const af_shadow_bounds *casters, size_t caster_count, af_shadow_view *out) {
    if (!out || !af_camera_is_valid(camera) || !isfinite(aspect) || aspect <= 0 ||
        !af_shadow_settings_valid(settings) || (caster_count && !casters) || !isfinite(af_vec3_length(direction)) ||
        af_vec3_length(direction) < 0.000001f)
        return false;
    af_shadow_view result = {.count = settings.mode == AF_SHADOW_CASCADED ? 4u : 1u};
    direction = af_vec3_normalize(direction);
    float far = fminf(camera->far_clip, fmaxf(camera->near_clip + 0.001f, settings.distance));
    if (far <= camera->near_clip)
        return false;
    for (uint32_t i = 0; i < result.count; ++i) {
        float fraction = (float)(i + 1) / (float)result.count;
        float logarithmic = camera->near_clip * powf(far / camera->near_clip, fraction),
              linear = camera->near_clip + (far - camera->near_clip) * fraction;
        result.splits[i] =
            i + 1 == result.count ? far : settings.split_lambda * logarithmic + (1 - settings.split_lambda) * linear;
        float near = i ? result.splits[i - 1] : camera->near_clip;
        if (i) {
            float previous = i > 1 ? result.splits[i - 2] : camera->near_clip;
            near -= settings.blend_fraction * (near - previous);
        }
        if (!fit_slice(camera, aspect, direction, settings.resolution, near, result.splits[i], casters, caster_count,
                       &result.matrices[i], &result.texel_sizes[i]))
            return false;
    }
    for (uint32_t i = result.count; i < 4; ++i) {
        result.splits[i] = far;
        result.matrices[i] = result.matrices[result.count - 1];
    }
    *out = result;
    return true;
}
