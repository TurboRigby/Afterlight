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
 * This pass performs a depth-aware spatial filter, then accumulates the
 * filtered result against a reprojected history buffer (motion vectors carry
 * UV-space displacement plus previous/current clip depth). The accumulated
 * result is written to the ping-pong history image for the next frame and the
 * difference against the raw sample is additively blended into the HDR scene.
 * Adding only the difference keeps the raw GI's underwater/fog treatment while
 * replacing its noise with the filtered signal. */

layout(set = 0, binding = 0) uniform sampler2D gi_image;
layout(set = 0, binding = 1) uniform sampler2D motion_image;
layout(set = 0, binding = 2) uniform sampler2D history_image;

layout(push_constant) uniform Push {
    vec4 viewport; /* xy = first pixel UV, zw = one past last pixel UV. */
    vec4 texel;    /* xy = 1/size, z = temporal blend, w = history valid. */
    vec4 params;   /* x = radius in pixels, y = spatial sigma. */
} push;

layout(location = 0) out vec4 out_history;
layout(location = 1) out vec4 out_color;

vec3 denoise_statistics(vec3 c, inout vec3 mean, inout vec3 square_mean) {
    mean += c;
    square_mean += c * c;
    return c;
}

void main() {
    ivec2 size = textureSize(gi_image, 0);
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    ivec2 lo = ivec2(ceil(push.viewport.xy * vec2(size)));
    ivec2 hi = ivec2(floor(push.viewport.zw * vec2(size))) - 1;
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

    vec2 uv = (vec2(pixel) + 0.5) / vec2(size);
    vec4 center_motion = texelFetch(motion_image, pixel, 0);
    float center_depth = center_motion.w;
    vec3 raw = texelFetch(gi_image, pixel, 0).rgb;

    if (center_depth >= 65000.0 || center_depth <= 0.0) {
        out_history = vec4(0);
        out_color = vec4(0);
        return;
    }

    float depth_scale = max(0.02, abs(center_depth) * 0.01);
    float sigma = max(push.params.y, 0.25);
    int radius = int(clamp(push.params.x, 1.0, 4.0));
    int taps = (2 * radius + 1) * (2 * radius + 1);

    vec3 sum = vec3(0);
    float weight_sum = 0;
    vec3 lower = vec3(1e20), upper = vec3(-1e20);
    vec3 mean = vec3(0), square_mean = vec3(0);
    for (int y = -radius; y <= radius; ++y)
        for (int x = -radius; x <= radius; ++x) {
            ivec2 p = clamp(pixel + ivec2(x, y), lo, hi);
            vec4 movement = texelFetch(motion_image, p, 0);
            vec3 c = texelFetch(gi_image, p, 0).rgb;
            denoise_statistics(c, mean, square_mean);
            lower = min(lower, c);
            upper = max(upper, c);
            float spatial = exp(-float(x * x + y * y) / (2.0 * sigma * sigma));
            float depth = exp(-abs(movement.w - center_depth) / depth_scale);
            float weight = spatial * depth;
            sum += c * weight;
            weight_sum += weight;
        }
    vec3 spatial_result = weight_sum > 1e-6 ? sum / weight_sum : raw;

    float n = float(taps);
    mean /= n;
    square_mean /= n;
    vec3 sigma_color = sqrt(max(square_mean - mean * mean, vec3(0)));
    lower = max(lower, mean - 1.5 * sigma_color);
    upper = min(upper, mean + 1.5 * sigma_color);

    vec2 old_uv = uv + center_motion.xy;
    bool valid = push.texel.w > 0.5 && center_motion.z > 0 &&
                 all(greaterThanEqual(old_uv, push.viewport.xy)) && all(lessThanEqual(old_uv, push.viewport.zw));
    vec4 history_sample = texture(history_image, clamp(old_uv, push.viewport.xy, push.viewport.zw));
    vec3 history = history_sample.rgb;
    float history_depth = history_sample.w;
    /* Reject disoccluded surfaces using the depth stored with the history. */
    valid = valid && abs(center_motion.z - history_depth) <= max(0.05, abs(center_motion.z) * 0.03);
    history = clamp(history, min(lower, upper), max(lower, upper));

    vec2 pixel_step = 1.0 / vec2(size);
    float speed = length(center_motion.xy / pixel_step);
    float alpha = mix(push.texel.z, 0.5, clamp(speed / 16.0, 0.0, 1.0));
    if (!valid)
        alpha = 1.0;
    vec3 result = mix(history, spatial_result, clamp(alpha, 0.0, 1.0));

    out_history = vec4(result, center_depth);
    /* Add only the difference; the scene already contains the raw GI term. */
    out_color = vec4(result - raw, 0.0);
}
