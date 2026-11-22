/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "renderer_common.h"
#include <float.h>

int af_rc_blend_compare(const void *a, const void *b) {
    const af_blend_draw *x = a, *y = b;
    if (x->depth != y->depth)
        return x->depth > y->depth ? -1 : 1;
    return (x->index > y->index) - (x->index < y->index);
}

int af_rc_motion_compare(const void *a, const void *b) {
    af_object_id x = ((const af_motion_snapshot *)a)->id, y = ((const af_motion_snapshot *)b)->id;
    return (x > y) - (x < y);
}

uint32_t af_rc_mesh_face_flags(af_mesh_sidedness sidedness, af_mat4 model) {
    if (sidedness == AF_MESH_SIDES_DEFAULT)
        return 0;
    const float *m = model.elements;
    af_vec3 a = {m[0], m[1], m[2]}, b = {m[4], m[5], m[6]}, c = {m[8], m[9], m[10]};
    uint32_t face = sidedness == AF_MESH_SIDES_FRONT ? AF_DRAW_CULL_BACK : AF_DRAW_FLIP_BACK_NORMAL;
    return face | (af_vec3_dot(a, af_vec3_cross(b, c)) < 0 ? AF_DRAW_MIRRORED : 0);
}

uint32_t af_rc_shadow_material_flags(af_alpha_mode alpha_mode, af_mesh_sidedness sidedness, af_mat4 model) {
    return (uint32_t)alpha_mode | (af_rc_mesh_face_flags(sidedness, model) << 8);
}

float af_rc_halton(unsigned n, unsigned base) {
    float value = 0, scale = 1;
    while (n) {
        scale /= (float)base;
        value += scale * (float)(n % base);
        n /= base;
    }
    return value;
}

uint64_t af_rc_temporal_settings_hash(uint64_t h, af_render_settings value) {
    h = af_r_hash(h, &value.ray_traced_shadows, sizeof(value.ray_traced_shadows));
    h = af_r_hash(h, &value.ray_traced_water_reflections, sizeof(value.ray_traced_water_reflections));
    h = af_r_hash(h, &value.ray_traced_gi, sizeof(value.ray_traced_gi));
    h = af_r_hash(h, &value.rtgi_intensity, sizeof(value.rtgi_intensity));
    h = af_r_hash(h, &value.rtgi_ray_distance, sizeof(value.rtgi_ray_distance));
    h = af_r_hash(h, &value.rtgi_samples, sizeof(value.rtgi_samples));
    h = af_r_hash(h, &value.rtgi_bounces, sizeof(value.rtgi_bounces));
    h = af_r_hash(h, &value.rtgi_emissive, sizeof(value.rtgi_emissive));
    h = af_r_hash(h, &value.anti_aliasing, sizeof(value.anti_aliasing));
    h = af_r_hash(h, &value.aa_sharpness, sizeof(value.aa_sharpness));
    h = af_r_hash(h, &value.taa_history_weight, sizeof(value.taa_history_weight));
    h = af_r_hash(h, &value.exposure, sizeof(value.exposure));
    h = af_r_hash(h, &value.tone_mapping, sizeof(value.tone_mapping));
    h = af_r_hash(h, &value.underwater, sizeof(value.underwater));
    h = af_r_hash(h, &value.refraction, sizeof(value.refraction));
    h = af_r_hash(h, &value.refraction_strength, sizeof(value.refraction_strength));
    h = af_r_hash(h, &value.bloom, sizeof(value.bloom));
    h = af_r_hash(h, &value.bloom_strength, sizeof(value.bloom_strength));
    h = af_r_hash(h, &value.bloom_threshold, sizeof(value.bloom_threshold));
    h = af_r_hash(h, &value.bloom_radius, sizeof(value.bloom_radius));
    h = af_r_hash(h, &value.local_shadow_resolution, sizeof(value.local_shadow_resolution));
    h = af_r_hash(h, &value.environment_enabled, sizeof(value.environment_enabled));
    h = af_r_hash(h, &value.environment_profile, sizeof(value.environment_profile));
    h = af_r_hash(h, &value.skybox, sizeof(value.skybox));
    h = af_r_hash(h, &value.skybox_intensity, sizeof(value.skybox_intensity));
    h = af_r_hash(h, &value.environment_intensity, sizeof(value.environment_intensity));
    h = af_r_hash(h, &value.environment_rotation, sizeof(value.environment_rotation));
    h = af_r_hash(h, &value.motion_blur, sizeof(value.motion_blur));
    h = af_r_hash(h, &value.motion_blur_strength, sizeof(value.motion_blur_strength));
    h = af_r_hash(h, value.environment_asset, strlen(value.environment_asset));
    return h;
}

uint64_t af_rc_temporal_material_hash(uint64_t h, af_material value) {
    h = af_r_hash(h, &value.alpha_mode, sizeof(value.alpha_mode));
    h = af_r_hash(h, &value.alpha_cutoff, sizeof(value.alpha_cutoff));
    h = af_r_hash(h, &value.base_color, sizeof(value.base_color));
    h = af_r_hash(h, &value.unlit, sizeof(value.unlit));
    h = af_r_hash(h, &value.cast_shadows, sizeof(value.cast_shadows));
    h = af_r_hash(h, &value.receive_shadows, sizeof(value.receive_shadows));
    h = af_r_hash(h, &value.physically_based, sizeof(value.physically_based));
    h = af_r_hash(h, &value.roughness, sizeof(value.roughness));
    h = af_r_hash(h, &value.metallic, sizeof(value.metallic));
    h = af_r_hash(h, &value.emission, sizeof(value.emission));
    h = af_r_hash(h, &value.emission_strength, sizeof(value.emission_strength));
    h = af_r_hash(h, &value.normal_strength, sizeof(value.normal_strength));
    h = af_r_hash(h, value.texture, strlen(value.texture));
    h = af_r_hash(h, value.normal_map, strlen(value.normal_map));
    h = af_r_hash(h, value.metallic_roughness_map, strlen(value.metallic_roughness_map));
    h = af_r_hash(h, value.shader, strlen(value.shader));
    return h;
}

uint64_t af_rc_temporal_light_hash(uint64_t h, af_local_light value) {
    h = af_r_hash(h, &value.type, sizeof(value.type));
    h = af_r_hash(h, &value.enabled, sizeof(value.enabled));
    h = af_r_hash(h, &value.position, sizeof(value.position));
    h = af_r_hash(h, &value.color, sizeof(value.color));
    h = af_r_hash(h, &value.intensity, sizeof(value.intensity));
    h = af_r_hash(h, &value.range, sizeof(value.range));
    h = af_r_hash(h, &value.direction, sizeof(value.direction));
    h = af_r_hash(h, &value.cast_shadows, sizeof(value.cast_shadows));
    h = af_r_hash(h, &value.shadow_bias, sizeof(value.shadow_bias));
    h = af_r_hash(h, &value.inner_angle, sizeof(value.inner_angle));
    h = af_r_hash(h, &value.outer_angle, sizeof(value.outer_angle));
    return h;
}

float af_rc_cluster_error_scale(af_mat4 world, float pixel_scale) {
    af_vec3 axes[3];
    for (unsigned i = 0; i < 3; ++i)
        axes[i] = (af_vec3){world.elements[i * 4], world.elements[i * 4 + 1], world.elements[i * 4 + 2]};
    float squared = 0;
    for (unsigned i = 0; i < 3; ++i) {
        float sum = 0;
        for (unsigned j = 0; j < 3; ++j)
            sum += fabsf(af_vec3_dot(axes[i], axes[j]));
        squared = fmaxf(squared, sum);
    }
    return sqrtf(squared) * pixel_scale;
}

bool af_rc_cluster_wants_coarse(af_mat4 clip, af_vec3 minimum, af_vec3 maximum, float pixels, bool lod) {
    if (!lod)
        return false;
    float x = clip.elements[3], y = clip.elements[7], z = clip.elements[11];
    float nearest = clip.elements[15] + x * (x >= 0 ? minimum.x : maximum.x) + y * (y >= 0 ? minimum.y : maximum.y) +
                    z * (z >= 0 ? minimum.z : maximum.z);
    return nearest > 0 && pixels <= 2.f * nearest;
}

af_cluster_view af_rc_cluster_view(const af_frame_uniform *uniform, const af_camera *camera) {
    float width = (uniform->water_viewport.z - uniform->water_viewport.x) * uniform->water_screen.x;
    float height = (uniform->water_viewport.w - uniform->water_viewport.y) * uniform->water_screen.y;
    float scale_x = 1.f / (1.f + 4.f / fmaxf(width, 1.f));
    float scale_y = 1.f / (1.f + 4.f / fmaxf(height, 1.f));
    af_mat4 projection = af_mat4_mul(af_mat4_scale((af_vec3){scale_x, scale_y, 1.f}), uniform->view_projection);
    return (af_cluster_view){projection, height / (2.f * tanf(camera->vertical_fov * .5f))};
}

af_rect af_rc_resolve_viewport(SDL_Window *window, af_rect rect) {
    int width = 0, height = 0;
    SDL_GetWindowSize(window, &width, &height);
    float w = (float)(width > 0 ? width : 1), h = (float)(height > 0 ? height : 1);
    if (!isfinite(rect.x) || !isfinite(rect.y) || !isfinite(rect.width) || !isfinite(rect.height) || rect.width <= 0 ||
        rect.height <= 0)
        return (af_rect){0, 0, w, h};
    rect.x = fmaxf(0, fminf(w - 1, rect.x));
    rect.y = fmaxf(0, fminf(h - 1, rect.y));
    rect.width = fmaxf(1, fminf(w - rect.x, rect.width));
    rect.height = fmaxf(1, fminf(h - rect.y, rect.height));
    return rect;
}

bool af_rc_validate_mesh_data(const af_mesh_data *mesh, af_vec3 *out_min, af_vec3 *out_max, bool *out_skinned,
                              uint32_t *out_max_joint) {
    if (!mesh || !mesh->vertices || !mesh->indices || mesh->vertex_count == 0 || mesh->index_count == 0 ||
        mesh->index_count % 3 != 0)
        return false;
    af_vec3 minimum = {FLT_MAX, FLT_MAX, FLT_MAX};
    af_vec3 maximum = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
    bool skinned = false;
    uint32_t max_joint = 0;
    for (uint32_t i = 0; i < mesh->vertex_count; ++i) {
        const af_vertex *v = &mesh->vertices[i];
        float w[4] = {v->weights.x, v->weights.y, v->weights.z, v->weights.w};
        float vals[] = {v->position.x, v->position.y, v->position.z, v->color.r, v->color.g, v->color.b,
                        v->color.a,    v->normal.x,   v->normal.y,   v->normal.z, v->uv.x,   v->uv.y,
                        w[0],          w[1],          w[2],          w[3]};
        for (size_t k = 0; k < sizeof(vals) / sizeof(vals[0]); ++k)
            if (!isfinite(vals[k]))
                return false;
        minimum.x = fminf(minimum.x, v->position.x);
        maximum.x = fmaxf(maximum.x, v->position.x);
        minimum.y = fminf(minimum.y, v->position.y);
        maximum.y = fmaxf(maximum.y, v->position.y);
        minimum.z = fminf(minimum.z, v->position.z);
        maximum.z = fmaxf(maximum.z, v->position.z);
        float wsum = w[0] + w[1] + w[2] + w[3];
        if (wsum > 0.0f) {
            skinned = true;
            for (unsigned j = 0; j < 4; ++j)
                if (w[j] > 0.0f && v->joints[j] > max_joint)
                    max_joint = v->joints[j];
        }
    }
    for (uint32_t i = 0; i < mesh->index_count; ++i)
        if (mesh->indices[i] >= mesh->vertex_count)
            return false;
    if (out_min)
        *out_min = minimum;
    if (out_max)
        *out_max = maximum;
    if (out_skinned)
        *out_skinned = skinned;
    if (out_max_joint)
        *out_max_joint = max_joint;
    return true;
}

bool af_rc_rgba32f_to_rgba16f(const float *src, uint16_t *dst, size_t float_count) {
    for (size_t i = 0; i < float_count; ++i) {
        float value = src[i];
        if (!isfinite(value) || value < 0.0f || value > 65504.0f)
            return false;
        if (value == 0.0f) {
            dst[i] = 0;
            continue;
        }
        int exponent = 0;
        float fraction = frexpf(value, &exponent);
        unsigned mantissa;
        if (exponent < -13)
            mantissa = (unsigned)lrintf(ldexpf(value, 24));
        else {
            unsigned significand = (unsigned)lrintf((fraction * 2.0f - 1.0f) * 1024.0f);
            mantissa = ((unsigned)(exponent + 14) << 10) + significand;
        }
        dst[i] = (uint16_t)mantissa;
    }
    return true;
}

void af_rc_bin_local_lights(const af_gpu_light *lights, uint32_t count, const af_camera *camera,
                            const af_frame_uniform *uniform, uint32_t render_w, uint32_t render_h, uint32_t cols,
                            uint32_t rows, af_light_tile_mask *tiles) {
    memset(tiles, 0, (size_t)cols * rows * sizeof(*tiles));
    if (count == 0)
        return;
    float rw = (float)render_w;
    float rh = (float)render_h;
    float vp_x = uniform->water_viewport.x * rw;
    float vp_y = uniform->water_viewport.y * rh;
    float vp_w = (uniform->water_viewport.z - uniform->water_viewport.x) * rw;
    float vp_h = (uniform->water_viewport.w - uniform->water_viewport.y) * rh;
    const float *m = uniform->view_projection.elements;
    for (uint32_t i = 0; i < count; ++i) {
        const af_gpu_light *l = &lights[i];
        af_vec3 center = {l->position_range.x, l->position_range.y, l->position_range.z};
        float range = l->position_range.w;
        if (!(range > 0.0f) || !(l->color_intensity.w > 0.0f))
            continue;
        af_vec3 ext = {range, range, range};
        af_shadow_bounds bounds =
            af_shadow_transform_bounds(af_mat4_identity(), af_vec3_sub(center, ext), af_vec3_add(center, ext));
        if (!af_shadow_intersects(uniform->view_projection, &bounds))
            continue;
        uint32_t min_tx = 0, max_tx = cols - 1u;
        uint32_t min_ty = 0, max_ty = rows - 1u;
        af_vec3 to_light = af_vec3_sub(center, camera->position);
        float dist_sq = af_vec3_dot(to_light, to_light);
        float safe_r = range + camera->near_clip + 0.05f;
        if (dist_sq > safe_r * safe_r) {
            bool crosses_near = false;
            float ndc_min_x = 1e9f, ndc_max_x = -1e9f, ndc_min_y = 1e9f, ndc_max_y = -1e9f;
            for (unsigned c = 0; c < 8u; ++c) {
                float x = center.x + ((c & 1u) ? range : -range);
                float y = center.y + ((c & 2u) ? range : -range);
                float z = center.z + ((c & 4u) ? range : -range);
                float cx = m[0] * x + m[4] * y + m[8] * z + m[12];
                float cy = m[1] * x + m[5] * y + m[9] * z + m[13];
                float cw = m[3] * x + m[7] * y + m[11] * z + m[15];
                if (cw <= 0.001f) {
                    crosses_near = true;
                    break;
                }
                float nx = cx / cw;
                float ny = cy / cw;
                if (nx < ndc_min_x)
                    ndc_min_x = nx;
                if (nx > ndc_max_x)
                    ndc_max_x = nx;
                if (ny < ndc_min_y)
                    ndc_min_y = ny;
                if (ny > ndc_max_y)
                    ndc_max_y = ny;
            }
            if (!crosses_near) {
                float px0 = vp_x + (ndc_min_x * 0.5f + 0.5f) * vp_w;
                float px1 = vp_x + (ndc_max_x * 0.5f + 0.5f) * vp_w;
                float py0 = vp_y + (ndc_min_y * 0.5f + 0.5f) * vp_h;
                float py1 = vp_y + (ndc_max_y * 0.5f + 0.5f) * vp_h;
                if (px1 < 0.0f || px0 >= rw || py1 < 0.0f || py0 >= rh)
                    continue;
                int t0x = (int)floorf(px0 / (float)AF_LIGHT_TILE_SIZE) - 1;
                int t1x = (int)floorf(px1 / (float)AF_LIGHT_TILE_SIZE) + 1;
                int t0y = (int)floorf(py0 / (float)AF_LIGHT_TILE_SIZE) - 1;
                int t1y = (int)floorf(py1 / (float)AF_LIGHT_TILE_SIZE) + 1;
                if (t0x < 0)
                    t0x = 0;
                if (t0y < 0)
                    t0y = 0;
                if (t1x >= (int)cols)
                    t1x = (int)cols - 1;
                if (t1y >= (int)rows)
                    t1y = (int)rows - 1;
                if (t0x > t1x || t0y > t1y)
                    continue;
                min_tx = (uint32_t)t0x;
                max_tx = (uint32_t)t1x;
                min_ty = (uint32_t)t0y;
                max_ty = (uint32_t)t1y;
            }
        }
        uint32_t word = i >> 5u;
        uint32_t bit = 1u << (i & 31u);
        for (uint32_t ty = min_ty; ty <= max_ty; ++ty) {
            af_light_tile_mask *row = &tiles[(size_t)ty * cols];
            for (uint32_t tx = min_tx; tx <= max_tx; ++tx)
                row[tx].words[word] |= bit;
        }
    }
}

bool af_rc_ray_aabb_hit(const float o[3], const float d[3], const float lo[3], const float hi[3], float near_t,
                        float far_t, float *out_t) {
    float tmin = near_t, tmax = far_t;
    for (unsigned axis = 0; axis < 3; ++axis) {
        if (fabsf(d[axis]) < 1e-8f) {
            if (o[axis] < lo[axis] || o[axis] > hi[axis])
                return false;
        } else {
            float t0 = (lo[axis] - o[axis]) / d[axis];
            float t1 = (hi[axis] - o[axis]) / d[axis];
            if (t0 > t1) {
                float tmp = t0;
                t0 = t1;
                t1 = tmp;
            }
            if (t0 > tmin)
                tmin = t0;
            if (t1 < tmax)
                tmax = t1;
            if (tmin > tmax)
                return false;
        }
    }
    if (out_t)
        *out_t = tmin;
    return true;
}

float af_rc_f16_to_f32(uint16_t h) {
    uint32_t sign = (uint32_t)(h & 0x8000u) << 16;
    uint32_t exp = (uint32_t)(h >> 10) & 0x1Fu;
    uint32_t frac = (uint32_t)(h & 0x03FFu);
    uint32_t bits;
    if (exp == 0) {
        if (frac == 0)
            bits = sign;
        else {
            float v = ldexpf((float)frac, -24);
            return sign ? -v : v;
        }
    } else if (exp == 31) {
        bits = sign | 0x7F800000u | (frac << 13);
    } else {
        bits = sign | ((exp + 112u) << 23) | (frac << 13);
    }
    float out;
    memcpy(&out, &bits, sizeof(out));
    return out;
}
