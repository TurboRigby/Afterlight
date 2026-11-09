#version 460
/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/* Edge-aware spatiotemporal denoise for the ray-traced GI signal.
 *
 * The scene pass writes the raw per-pixel GI contribution to a dedicated
 * attachment. That signal is 1-4 stochastic rays per frame, so it is noisy.
 * This pass performs a plane-aware edge-stopping spatial filter, then accumulates
 * the filtered result against a reprojected history buffer (motion vectors carry
 * UV-space displacement plus previous/current clip depth). The accumulated
 * result is written to the ping-pong history image for the next frame and
 * additively blended into the HDR scene. */

layout(set = 0, binding = 0) uniform sampler2D gi_image;
layout(set = 0, binding = 1) uniform sampler2D motion_image;
layout(set = 0, binding = 2) uniform sampler2D history_image;

layout(push_constant) uniform Push {
    vec4 viewport; /* xy = first pixel UV, zw = one past last pixel UV. */
    vec4 texel;    /* xy = 1/size, z = temporal blend, w = history valid. */
    vec4 params;   /* xy = radius/sigma; zw = previous-minus-current TAA jitter in UV. */
} push;

layout(location = 0) out vec4 out_history;
layout(location = 1) out vec4 out_color;

bool reproject_history(vec2 old_uv, float depth, ivec2 size, ivec2 lo, ivec2 hi, out vec3 history) {
    history = vec3(0.0);
    if (push.texel.w < 0.5 || depth <= 0.0 || !all(greaterThanEqual(old_uv, push.viewport.xy)) ||
        !all(lessThan(old_uv, push.viewport.zw)))
        return false;

    vec2 position = old_uv * vec2(size) - 0.5;
    ivec2 base = ivec2(floor(position));
    vec2 fraction = fract(position);
    float tolerance = max(0.08, depth * 0.05);
    float weight_sum = 0.0;
    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 2; ++x) {
            ivec2 p = base + ivec2(x, y);
            if (any(lessThan(p, lo)) || any(greaterThan(p, hi)))
                continue;
            vec4 tap = texelFetch(history_image, p, 0);
            // Validate each depth before blending colors across a silhouette.
            if (tap.w <= 0.0 || abs(depth - tap.w) > tolerance)
                continue;
            float weight = (x == 0 ? 1.0 - fraction.x : fraction.x) *
                           (y == 0 ? 1.0 - fraction.y : fraction.y);
            history += tap.rgb * weight;
            weight_sum += weight;
        }
    }
    if (weight_sum <= 1e-6)
        return false;
    history /= weight_sum;
    return true;
}

void main() {
    ivec2 size = textureSize(gi_image, 0);
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    vec2 uv = (vec2(pixel) + 0.5) / vec2(size);

    /* Evaluate derivatives before any per-pixel early return. Adjacent lanes in
     * a fragment quad can cover different viewport/depth-validity branches; doing
     * dFdx/dFdy after those branches makes the result undefined at those edges. */
    vec4 center_motion = texelFetch(motion_image, pixel, 0);
    float center_depth = center_motion.w;
    float inv_z = 1.0 / max(center_depth, 0.001);
    vec2 d_inv_z = vec2(dFdx(inv_z), dFdy(inv_z));
    float max_d_inv_z = 0.5 * inv_z;
    d_inv_z = clamp(d_inv_z, -vec2(max_d_inv_z), vec2(max_d_inv_z));

    ivec2 lo = ivec2(round(push.viewport.xy * vec2(size)));
    ivec2 hi = ivec2(round(push.viewport.zw * vec2(size))) - 1;
    if (lo.x > hi.x || lo.y > hi.y) {
        out_history = vec4(0);
        out_color = vec4(0);
        return;
    }
    if (any(lessThan(pixel, lo)) || any(greaterThan(pixel, hi))) {
        out_history = vec4(0);
        out_color = vec4(0);
        return;
    }

    vec4 raw_sample = texelFetch(gi_image, pixel, 0);
    vec3 raw = raw_sample.rgb;
    if (center_depth >= 65000.0 || center_depth <= 0.0 || raw_sample.w < 0.5) {
        out_history = vec4(0);
        out_color = vec4(0);
        return;
    }

    float sigma = max(push.params.y, 0.25);
    int radius = int(clamp(push.params.x, 1.0, 4.0));

    vec3 sum = vec3(0.0);
    vec3 sum_sq = vec3(0.0);
    float weight_sum = 0.0;

    for (int y = -radius; y <= radius; ++y) {
        for (int x = -radius; x <= radius; ++x) {
            ivec2 p = pixel + ivec2(x, y);
            /* Skip taps outside the active 3D viewport without border clamping distortion. */
            if (any(lessThan(p, lo)) || any(greaterThan(p, hi)))
                continue;

            vec4 movement = texelFetch(motion_image, p, 0);
            vec4 c_sample = texelFetch(gi_image, p, 0);
            if (c_sample.w < 0.5)
                continue;
            vec3 c = c_sample.rgb;

            /* Exact plane prediction via reciprocal depth. */
            float neighbor_inv_z = 1.0 / max(movement.w, 0.001);
            float expected_inv_z = inv_z + float(x) * d_inv_z.x + float(y) * d_inv_z.y;
            float inv_z_diff = abs(neighbor_inv_z - expected_inv_z);
            float inv_z_scale = max(0.12 * inv_z, 0.002);
            float depth_weight = exp(-inv_z_diff / inv_z_scale);

            float spatial_weight = exp(-float(x * x + y * y) / (2.0 * sigma * sigma));
            float weight = spatial_weight * depth_weight;

            sum += c * weight;
            sum_sq += c * c * weight;
            weight_sum += weight;
        }
    }

    vec3 spatial_result = weight_sum > 1e-6 ? sum / weight_sum : raw;
    vec3 spatial_sq = weight_sum > 1e-6 ? sum_sq / weight_sum : raw * raw;
    vec3 sigma_color = sqrt(max(spatial_sq - spatial_result * spatial_result, vec3(0.0)));

    /* Motion is unjittered; history coordinates include each frame's TAA jitter. */
    vec2 old_uv = uv + center_motion.xy + push.params.zw;
    vec3 history;
    bool valid = reproject_history(old_uv, center_motion.z, size, lo, hi, history);

    /* Generous variance bounding around the spatially filtered signal:
     * protects against temporal ghosting on dynamic lighting changes without
     * clipping valid stochastic temporal convergence. */
    vec3 color_box = max(3.0 * sigma_color, vec3(0.12));
    vec3 lower = max(vec3(0.0), spatial_result - color_box);
    vec3 upper = spatial_result + color_box;
    history = clamp(history, lower, upper);

    float speed = length(center_motion.xy * vec2(size));
    /* Reprojection already tracks camera and object motion. Do not throw away
     * most of the GI history merely because a stable surface moves across the
     * screen: that makes emissive sampling noise visible while walking. Raise
     * the update rate gradually, and only for large per-frame displacements. */
    float alpha = mix(push.texel.z, 0.15, clamp(speed / 64.0, 0.0, 1.0));
    if (!valid)
        alpha = 1.0;
    vec3 result = mix(history, spatial_result, clamp(alpha, 0.0, 1.0));

    out_history = vec4(result, center_depth);
    /* Pure additive blending: output the clean denoised GI directly */
    out_color = vec4(result, 0.0);
}
