// SPDX-License-Identifier: MPL-2.0
// Original GLSL adapter; algorithm and tables retain upstream MIT notices.
#define SMAA_GLSL_4
#define SMAA_PRESET_HIGH
#define SMAA_RT_METRICS vec4(1.0 / vec2(textureSize(source_image, 0)), vec2(textureSize(source_image, 0)))
#include "../../third_party/smaa/SMAA.hlsl"
vec4 smaa_pass(int mode) {
    vec2 size = vec2(textureSize(source_image, 0));
    bool inside =
        all(greaterThanEqual(uv, post.viewport.xy + 2.0 / size)) && all(lessThan(uv, post.viewport.zw - 2.0 / size));
    if (mode == 7) {
        if (!inside)
            return vec4(0);
        vec4 offsets[3];
        SMAAEdgeDetectionVS(uv, offsets);
        return vec4(SMAAColorEdgeDetectionPS(uv, offsets, source_image), 0, 0);
    }
    if (mode == 8) {
        if (!inside)
            return vec4(0);
        vec4 offsets[3];
        vec2 pixel;
        SMAABlendingWeightCalculationVS(uv, pixel, offsets);
        return SMAABlendingWeightCalculationPS(uv, pixel, offsets, source_image, bloom_image, lookup_image, vec4(0));
    }
    vec4 offset;
    SMAANeighborhoodBlendingVS(uv, offset);
    vec3 color =
        inside ? SMAANeighborhoodBlendingPS(uv, offset, source_image, bloom_image).rgb : texture(source_image, uv).rgb;
    if (inside && post.aa.y > 0) {
        vec3 lo = color, hi = color, sum = vec3(0);
        for (int i = 0; i < 4; ++i) {
            vec2 d = i < 2 ? vec2(i == 0 ? -1 : 1, 0) : vec2(0, i == 2 ? -1 : 1);
            vec3 c = texture(source_image, uv + d / size).rgb;
            lo = min(lo, c);
            hi = max(hi, c);
            sum += c;
        }
        color = clamp(color + (color - sum * .25) * post.aa.y, lo, hi);
    }
    return vec4(color * color, 1);
}
