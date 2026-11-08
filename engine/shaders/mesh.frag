#version 460
#extension GL_GOOGLE_include_directive : require
#ifdef AF_RAY_QUERY
#extension GL_EXT_ray_query : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference_uvec2 : require
layout(set = 0, binding = 10) uniform accelerationStructureEXT scene_acceleration;
#endif
/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

layout(location = 0) in vec4 vertex_color;
layout(location = 1) in vec3 world_position;
layout(location = 2) in vec3 world_normal;
layout(location = 3) in vec2 uv;
layout(location = 0) out vec4 out_color;
layout(location = 1) out vec4 out_motion;
layout(location = 2) out vec4 out_gi;
layout(location = 4) in vec4 previous_clip;
layout(location = 5) in vec4 current_clip;
layout(location = 6) flat in uint motion_valid;
struct LocalLight {
    vec4 position_range;
    vec4 color_intensity;
    vec4 direction_inner;
    vec4 params;
};
layout(set = 0, binding = 0) uniform Frame {
    mat4 view_projection;
    vec4 direction_intensity;
    vec4 color_ambient;
    mat4 shadow_matrices[4];
    vec4 cascade_splits;
    vec4 camera_position;
    vec4 camera_forward;
    vec4 shadow_params;
    vec4 cascade_params;
    vec4 render_params;
    LocalLight lights[16];
    vec4 light_count;
    mat4 local_shadow_matrices[24];
    mat4 inverse_view_projection;
    vec4 sky_params;
    vec4 water_absorption, water_scattering, water_bounds, water_axes, water_screen, water_viewport, wind_time;
    vec4 rtgi_params;
    mat4 unjittered_view_projection, previous_view_projection;
}
frame;
layout(set = 0, binding = 1) uniform sampler2DArray local_shadow_map;
layout(set = 0, binding = 2) uniform sampler2D environment_map;
layout(set = 0, binding = 3) uniform sampler2D irradiance_map;
layout(set = 0, binding = 4) uniform sampler2D brdf_lut;
layout(set = 0, binding = 7) uniform sampler2D refracted_scene;
layout(set = 0, binding = 8) uniform sampler2D opaque_depth;
layout(set = 1, binding = 0) uniform sampler2D base_texture;
layout(push_constant) uniform Draw {
    mat4 model;
    vec4 tint;
    vec4 params;
    vec4 surface;
    vec4 emission;
}
draw;
layout(set = 2, binding = 0) uniform sampler2DArray shadow_map;
layout(set = 3, binding = 0) uniform sampler2D normal_texture;
layout(set = 4, binding = 0) uniform sampler2D metallic_roughness_texture;
float sample_shadow(int layer, vec3 position) {
    vec4 projected = frame.shadow_matrices[layer] * vec4(position, 1);
    vec3 p = projected.xyz / projected.w;
    vec2 uv = p.xy * 0.5 + 0.5;
    if (p.z <= 0 || p.z >= 1 || any(lessThan(uv, vec2(0))) || any(greaterThan(uv, vec2(1))))
        return 1;
    int radius = int(frame.cascade_params.y);
    float sum = 0;
    float count = 0;
    for (int y = -radius; y <= radius; ++y)
        for (int x = -radius; x <= radius; ++x) {
            float depth = textureLod(shadow_map, vec3(uv + vec2(x, y) * frame.shadow_params.y, layer), 0).r;
            sum += p.z - frame.shadow_params.z <= depth ? 1.0 : 0.0;
            count += 1;
        }
    return sum / count;
}
int cascade_index(float depth) {
    int count = int(frame.cascade_params.x);
    int layer = 0;
    for (int i = 0; i < count - 1; ++i)
        if (depth > frame.cascade_splits[i])
            layer = i + 1;
    return layer;
}
float raster_shadow_visibility(vec3 n) {
    if (frame.shadow_params.x < 0.5 || draw.params.y < 0.5 || ((uint(draw.params.x) & 1u) != 0u))
        return 1;
    float depth = dot(world_position - frame.camera_position.xyz, frame.camera_forward.xyz);
    int count = int(frame.cascade_params.x);
    if (depth < 0 || depth > frame.cascade_splits[count - 1])
        return 1;
    int layer = cascade_index(depth);
    vec3 receiver = world_position + n * frame.shadow_params.w;
    float visibility = sample_shadow(layer, receiver);
    float near = layer == 0 ? frame.camera_position.w : frame.cascade_splits[layer - 1];
    float end = frame.cascade_splits[layer], width = (end - near) * frame.cascade_params.z;
    if (width > 0.00001 && depth > end - width) {
        float next = layer + 1 < count ? sample_shadow(layer + 1, receiver) : 1.0;
        visibility = mix(visibility, next, smoothstep(end - width, end, depth));
    }
    return visibility;
}
float shadow_visibility(vec3 n) {
    float raster = raster_shadow_visibility(n);
#ifdef AF_RAY_QUERY
    if (frame.sky_params.z < .5)
        return raster;
    if (frame.shadow_params.x < .5 || draw.params.y < .5 || ((uint(draw.params.x) & 1u) != 0u))
        return 1;
    vec3 l = -frame.direction_intensity.xyz;
    if (dot(n, l) <= 0)
        return raster;
    // Geometric-normal offset avoids normal-map self-intersections.
    vec3 geometric = normalize(cross(dFdx(world_position), dFdy(world_position)));
    if (dot(geometric, l) < 0)
        geometric = -geometric;
    float bias = max(.001, frame.shadow_params.w * .1);
    rayQueryEXT query;
    rayQueryInitializeEXT(query, scene_acceleration, gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT, 1,
                          world_position + geometric * bias, .001, l, 10000.0);
    while (rayQueryProceedEXT(query)) {
    }
    return rayQueryGetIntersectionTypeEXT(query, true) == gl_RayQueryCommittedIntersectionNoneEXT ? raster : 0.0;
#else
    return raster;
#endif
}
bool water_surface() {
    return (uint(draw.params.x) & (1u << 19u)) != 0u;
}
#include "water_ripples.glsl"
float water_f0() {
    float f = (draw.emission.w - 1.0) / (draw.emission.w + 1.0);
    return f * f;
}
vec3 water_transmission(vec3 n, vec3 v) {
    return exp(-draw.emission.rgb * draw.surface.z / max(abs(dot(n, v)), .15));
}
vec3 water_scatter(vec3 n, vec3 v, vec3 l, vec3 radiance) {
    return draw.tint.rgb * (vec3(1) - water_transmission(n, v)) * radiance * max(dot(n, l), 0.0) / 3.14159265359;
}
vec3 direct_light(vec3 base, vec3 n, vec3 v, vec3 l, vec3 radiance, float roughness, float metal) {
    float nl = max(dot(n, l), 0.0);
    if (draw.params.z < 0.5)
        return base * radiance * nl;
    const float pi = 3.14159265359;
    float rough = clamp(roughness, 0.04, 1.0);
    vec3 half_vector = v + l;
    if (dot(half_vector, half_vector) < 1e-10)
        return vec3(0);
    vec3 h = normalize(half_vector);
    float nv = max(dot(n, v), 0.0001), nh = max(dot(n, h), 0.0), vh = max(dot(v, h), 0.0);
    float alpha = rough * rough, alpha2 = alpha * alpha;
    float denominator = nh * nh * (alpha2 - 1.0) + 1.0;
    float distribution = alpha2 / (pi * denominator * denominator);
    float k = (rough + 1.0) * (rough + 1.0) / 8.0;
    float geometry = (nv / (nv * (1.0 - k) + k)) * (nl / (nl * (1.0 - k) + k));
    vec3 f0 = mix(vec3(water_surface() ? water_f0() : 0.04), base, metal);
    vec3 fresnel = f0 + (vec3(1) - f0) * pow(1.0 - vh, 5.0);
    vec3 specular = distribution * geometry * fresnel / max(4.0 * nv * nl, 0.0001);
    vec3 diffuse = (vec3(1) - fresnel) * (1.0 - metal) * base / pi;
    return (diffuse + specular) * radiance * nl;
}
int point_face(vec3 d) {
    vec3 a = abs(d);
    if (a.x >= a.y && a.x >= a.z)
        return d.x >= 0 ? 0 : 1;
    if (a.y >= a.z)
        return d.y >= 0 ? 2 : 3;
    return d.z >= 0 ? 4 : 5;
}
float local_depth_test(int layer, vec3 receiver, vec2 offset, float bias) {
    vec4 projected = frame.local_shadow_matrices[layer] * vec4(receiver, 1);
    if (projected.w <= 0)
        return 1;
    vec3 p = projected.xyz / projected.w;
    vec2 texcoord = p.xy * .5 + .5 + offset;
    if (p.z <= 0 || p.z >= 1 || any(lessThan(texcoord, vec2(0))) || any(greaterThan(texcoord, vec2(1))))
        return 1;
    return p.z - bias <= textureLod(local_shadow_map, vec3(texcoord, layer), 0).r ? 1.0 : 0.0;
}
float raster_local_visibility(LocalLight light, vec3 n) {
    if (draw.params.y < .5 || light.params.z < .5)
        return 1;
    int base = int(light.params.z) - 1;
    vec3 receiver = world_position + n * min(.01, light.position_range.w * .0005);
    float sum = 0;
    vec3 direction = receiver - light.position_range.xyz;
    vec3 ray = normalize(direction);
    vec3 t = normalize(cross(ray, abs(ray.y) < .99 ? vec3(0, 1, 0) : vec3(1, 0, 0))), b = cross(ray, t);
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x) {
            if (light.params.y > .5)
                sum += local_depth_test(base, receiver, vec2(x, y) * frame.light_count.y, light.params.w);
            else {
                vec3 point = receiver + (t * float(x) + b * float(y)) * length(direction) * 2.0 * frame.light_count.y;
                sum += local_depth_test(base + point_face(point - light.position_range.xyz), point, vec2(0),
                                        light.params.w);
            }
        }
    return sum / 9.0;
}
float local_visibility(LocalLight light, vec3 n, vec3 l, float distance_to_light) {
    float raster = raster_local_visibility(light, n);
#ifdef AF_RAY_QUERY
    if (frame.sky_params.z < .5)
        return raster;
    if (draw.params.y < .5 || light.params.z < .5 || ((uint(draw.params.x) & 1u) != 0u))
        return 1;
    if (dot(n, l) <= 0)
        return raster;
    vec3 geometric = normalize(cross(dFdx(world_position), dFdy(world_position)));
    if (dot(geometric, l) < 0)
        geometric = -geometric;
    float bias = max(.001, min(.01, light.position_range.w * .0005));
    if (distance_to_light <= bias)
        return raster;
    rayQueryEXT query;
    rayQueryInitializeEXT(query, scene_acceleration, gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT, 1,
                          world_position + geometric * bias, .001, l, distance_to_light - bias);
    while (rayQueryProceedEXT(query)) {
    }
    return rayQueryGetIntersectionTypeEXT(query, true) == gl_RayQueryCommittedIntersectionNoneEXT ? raster : 0.0;
#else
    return raster;
#endif
}
vec2 environment_uv(vec3 d) {
    return vec2((atan(d.z, d.x) - frame.render_params.w) / (2.0 * 3.14159265359) + 0.5,
                acos(clamp(d.y, -1.0, 1.0)) / 3.14159265359);
}
#ifdef AF_RAY_QUERY
#include "water_reflection.glsl"
#endif
vec3 environment_light(vec3 base, vec3 n, vec3 v, float roughness, float metal, out vec3 gi_contribution,
                       out bool has_gi) {
    gi_contribution = vec3(0);
    has_gi = false;
    if (frame.render_params.z <= 0
#ifdef AF_RAY_QUERY
        && !(water_surface() && frame.sky_params.w > .5) && !(frame.rtgi_params.x > .5)
#endif
    )
        return vec3(0);
    vec3 gi_radiance = vec3(0);
#ifdef AF_RAY_QUERY
    has_gi = !water_surface() && trace_gi_bounce(world_position, n, gi_radiance);
#endif
    vec3 diffuse = has_gi ? gi_radiance : textureLod(irradiance_map, environment_uv(n), 0).rgb * frame.render_params.z;
    if (draw.params.z < 0.5) {
        if (has_gi)
            gi_contribution = diffuse * base;
        return diffuse * base;
    }
    float rough = clamp(roughness, 0.04, 1.0), nv = max(dot(n, v), 0.0);
    vec3 f0 = mix(vec3(water_surface() ? water_f0() : 0.04), base, metal);
    vec3 fresnel = f0 + (max(vec3(1.0 - rough), f0) - f0) * pow(1.0 - nv, 5.0);
    vec3 direction = reflect(-v, n);
    vec3 radiance = textureLod(environment_map, environment_uv(direction), rough * 5.0).rgb * frame.render_params.z;
#ifdef AF_RAY_QUERY
    vec3 hit;
    if (water_surface() && trace_water_reflection(n, direction, rough, hit))
        radiance = mix(hit, radiance, smoothstep(.12, .6, rough));
#endif
    vec2 brdf = texture(brdf_lut, vec2(nv, rough)).rg;
    vec3 diffuse_brdf = (vec3(1) - fresnel) * (1.0 - metal) * base;
    if (has_gi)
        gi_contribution = diffuse_brdf * diffuse;
    return diffuse_brdf * diffuse + radiance * (f0 * brdf.x + brdf.y);
}
#include "water_fog.glsl"
void main() {
    out_gi = vec4(0);
    bool reactive = (uint(draw.params.x) & (1u << 19u)) != 0u || ((uint(draw.params.x) >> 1u) & 3u) == 2u;
    vec2 velocity = vec2(0);
    bool valid = motion_valid != 0u && previous_clip.w > 0 && current_clip.w > 0 && !reactive;
    if (valid)
        velocity = (previous_clip.xy / previous_clip.w - current_clip.xy / current_clip.w) * .5 *
                   (frame.water_viewport.zw - frame.water_viewport.xy);
    out_motion = vec4(velocity, valid ? min(previous_clip.w, 65000.0) : -1.0, min(max(current_clip.w, 0.0), 65000.0));
    vec3 geometric = cross(dFdx(world_position), dFdy(world_position));
    vec3 n = dot(world_normal, world_normal) > 1e-12
                 ? normalize(world_normal)
                 : (dot(geometric, geometric) > 1e-12 ? normalize(geometric) : vec3(0, 1, 0));
    vec3 px = dFdx(world_position), py = dFdy(world_position);
    vec2 ux = dFdx(uv), uy = dFdy(uv);
    float determinant = ux.x * uy.y - ux.y * uy.x;
    if (!water_surface() && draw.params.w > 0.5 && abs(determinant) > 1e-10) {
        vec3 tangent = (px * uy.y - py * ux.y) / determinant;
        vec3 bitangent = (py * ux.x - px * uy.x) / determinant;
        tangent -= n * dot(n, tangent);
        if (dot(tangent, tangent) > 1e-12) {
            tangent = normalize(tangent);
            float handedness = dot(cross(n, tangent), bitangent) < 0 ? -1.0 : 1.0;
            vec3 mapped = texture(normal_texture, uv).xyz * 2.0 - 1.0;
            mapped.xy *= draw.surface.z;
            if (dot(mapped, mapped) > 1e-12)
                n = normalize(mat3(tangent, cross(n, tangent) * handedness, n) * normalize(mapped));
        }
    }
    vec4 base = texture(base_texture, uv) * vertex_color;
    uint mode = (uint(draw.params.x) >> 1u) & 3u;
    uint cutoff = (uint(draw.params.x) >> 3u) & 65535u;
    if (mode == 1u && uint(round(clamp(base.a, 0, 1) * 65535.0)) < cutoff)
        discard;
    if (mode != 2u)
        base.a = 1;
    vec3 eye = frame.camera_position.xyz - world_position;
    vec3 v = dot(eye, eye) > 1e-12 ? normalize(eye) : n;
    bool water = water_surface();
    bool bulk_water = water && (uint(draw.params.x) & (1u << 18u)) != 0u;
    if (water && (uint(draw.params.x) & (1u << 21u)) != 0u) {
        bool inside = (uint(draw.params.x) & (1u << 23u)) != 0u;
        if (inside ? dot(n, v) >= 0 : dot(n, v) <= 0)
            discard;
    }
    if (water)
        n = water_ripple_normal(n);
    if (water) {
        if (dot(n, v) < 0)
            n = -n;
        float f = water_f0() + (1.0 - water_f0()) * pow(1.0 - max(dot(n, v), 0.0), 5.0);
        if ((uint(draw.params.x) & (1u << 20u)) != 0u) {
            out_color =
                vec4((uint(draw.params.x) & (1u << 22u)) != 0u ? vec3(0) : water_transmission(n, v) * (1.0 - f), 1);
            return;
        }
        base.rgb = vec3(0);
    }
    vec4 mr = texture(metallic_roughness_texture, uv);
    float roughness = draw.surface.x * mr.g, metal = water ? 0 : draw.surface.y * mr.b;
    vec3 ambient = base.rgb * frame.color_ambient.w * (draw.params.z > 0.5 ? 1.0 - metal : 1.0);
    float sun_visibility = shadow_visibility(n);
    vec3 output_rgb =
        ((uint(draw.params.x) & 1u) != 0u)
            ? base.rgb
            : ambient + direct_light(base.rgb, n, v, -frame.direction_intensity.xyz,
                                     frame.color_ambient.rgb * frame.direction_intensity.w, roughness, metal) *
                            sun_visibility;
    // The scattering term approximates the whole path through the water.
    // A hard shadow at the entry point cannot describe visibility along that
    // path. Applying it stamps a second silhouette over the shadowed pool bed.
    if (water)
        output_rgb +=
            draw.tint.rgb * (vec3(1) - water_transmission(n, v)) * frame.color_ambient.w +
            water_scatter(n, v, -frame.direction_intensity.xyz, frame.color_ambient.rgb * frame.direction_intensity.w) *
                (bulk_water ? 1.0 : sun_visibility);
    if (((uint(draw.params.x) & 1u) == 0u))
        for (int i = 0; i < int(frame.light_count.x); ++i) {
            LocalLight light = frame.lights[i];
            vec3 offset = light.position_range.xyz - world_position;
            float distance_squared = dot(offset, offset), distance_to_light = sqrt(distance_squared);
            if (distance_to_light >= light.position_range.w || distance_to_light < 0.00001)
                continue;
            vec3 l = offset / distance_to_light;
            float falloff = clamp(1.0 - pow(distance_to_light / light.position_range.w, 4.0), 0.0, 1.0);
            float attenuation = falloff * falloff / max(distance_squared, 0.01);
            if (light.params.y > 0.5) {
                float cone = dot(-l, light.direction_inner.xyz);
                float blend =
                    clamp((cone - light.params.x) / max(light.direction_inner.w - light.params.x, 0.00001), 0.0, 1.0);
                attenuation *= blend * blend * (3.0 - 2.0 * blend);
            }
            float visibility = local_visibility(light, n, l, distance_to_light);
            if (water)
                output_rgb +=
                    water_scatter(n, v, l, light.color_intensity.rgb * light.color_intensity.w * attenuation) *
                    (bulk_water ? 1.0 : visibility);
            output_rgb +=
                direct_light(base.rgb, n, v, l, light.color_intensity.rgb * light.color_intensity.w * attenuation,
                             roughness, metal) *
                visibility;
        }
    if (((uint(draw.params.x) & 1u) == 0u)) {
        vec3 gi = vec3(0);
        bool has_gi = false;
        output_rgb += environment_light(base.rgb, n, v, roughness, metal, gi, has_gi);
        vec3 gi_transmittance = underwater_transmittance(world_position);
        out_gi = vec4(gi * gi_transmittance, has_gi ? 1.0 : 0.0);
    }
    if (water && frame.render_params.z > 0)
        output_rgb += draw.tint.rgb * (vec3(1) - water_transmission(n, v)) *
                      textureLod(irradiance_map, environment_uv(n), 0).rgb * frame.render_params.z;
    vec3 transmitted_scene = vec3(0);
    if (water && (uint(draw.params.x) & (1u << 22u)) != 0u) {
        vec2 screen = gl_FragCoord.xy / frame.water_screen.xy;
        vec3 ray = refract(-v, n, 1.0 / max(draw.emission.w, 1.0001));
        vec4 projected = frame.view_projection * vec4(world_position + ray * draw.surface.z, 1);
        vec2 offset = vec2(0);
        if (projected.w > 0) {
            // Projection UVs cover the 3D viewport; snapshot UVs cover the whole window.
            vec2 viewport_uv = projected.xy / projected.w * .5 + .5;
            vec2 projected_screen = mix(frame.water_viewport.xy, frame.water_viewport.zw, viewport_uv);
            offset = (projected_screen - screen) * frame.water_screen.z;
        }
        vec2 pixel = .5 / frame.water_screen.xy;
        vec2 lo = frame.water_viewport.xy + pixel, hi = frame.water_viewport.zw - pixel;
        vec2 coordinate = clamp(screen + offset, min(lo, hi), max(lo, hi));
        if (texture(opaque_depth, coordinate).r + 0.0001 < gl_FragCoord.z)
            coordinate = screen;
        float f = water_f0() + (1.0 - water_f0()) * pow(1.0 - max(dot(n, v), 0.0), 5.0);
        transmitted_scene = texture(refracted_scene, coordinate).rgb * water_transmission(n, v) * (1.0 - f);
    }
    if (!water)
        output_rgb += draw.emission.rgb * draw.emission.w;
    float depth = dot(world_position - frame.camera_position.xyz, frame.camera_forward.xyz);
    if (frame.cascade_params.w > 0.5 && frame.shadow_params.x > 0.5 && depth >= 0 &&
        depth <= frame.cascade_splits[int(frame.cascade_params.x) - 1]) {
        const vec3 colors[4] =
            vec3[4](vec3(1, 0.25, 0.25), vec3(0.25, 1, 0.25), vec3(0.25, 0.5, 1), vec3(1, 0.85, 0.25));
        output_rgb *= mix(vec3(1), colors[cascade_index(depth)], 0.65);
    }
    output_rgb = underwater_color(output_rgb, world_position) + transmitted_scene;
    output_rgb = clamp(output_rgb, vec3(0), vec3(60000));
    out_color = vec4(output_rgb, base.a);
}
