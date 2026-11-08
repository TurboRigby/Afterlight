/* SPDX-License-Identifier: MPL-2.0 */
struct ReflectionSurface {
    vec4 color;
    vec4 emission_unlit;
    uvec4 addresses;
};
layout(std430, set = 0, binding = 11) readonly buffer ReflectionSurfaces {
    ReflectionSurface surfaces[];
}
reflection;
layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer MeshWords {
    uint words[];
};
vec3 vertex_attribute(MeshWords vertices, uint vertex, uint offset) {
    uint start = vertex * 18u + offset;
    return uintBitsToFloat(uvec3(vertices.words[start], vertices.words[start + 1], vertices.words[start + 2]));
}
vec3 shade_reflection_hit(rayQueryEXT query, vec3 hit_pos, vec3 direction) {
    ReflectionSurface surface = reflection.surfaces[rayQueryGetIntersectionInstanceCustomIndexEXT(query, true)];
    MeshWords vertices = MeshWords(surface.addresses.xy);
    MeshWords indices = MeshWords(surface.addresses.zw);
    uint triangle = 3u * uint(rayQueryGetIntersectionPrimitiveIndexEXT(query, true));
    uvec3 index = uvec3(indices.words[triangle], indices.words[triangle + 1], indices.words[triangle + 2]);
    vec2 uv_hit = rayQueryGetIntersectionBarycentricsEXT(query, true);
    vec3 weights = vec3(1.0 - uv_hit.x - uv_hit.y, uv_hit);
    vec3 base = surface.color.rgb * (vertex_attribute(vertices, index.x, 3) * weights.x +
                                     vertex_attribute(vertices, index.y, 3) * weights.y +
                                     vertex_attribute(vertices, index.z, 3) * weights.z);
    if (surface.emission_unlit.w > .5)
        return base + surface.emission_unlit.rgb;

    vec3 normal = vertex_attribute(vertices, index.x, 7) * weights.x +
                  vertex_attribute(vertices, index.y, 7) * weights.y +
                  vertex_attribute(vertices, index.z, 7) * weights.z;
    if (dot(normal, normal) < 1e-12) {
        vec3 a = vertex_attribute(vertices, index.x, 0);
        normal = cross(vertex_attribute(vertices, index.y, 0) - a, vertex_attribute(vertices, index.z, 0) - a);
    }
    normal = transpose(mat3(rayQueryGetIntersectionWorldToObjectEXT(query, true))) * normal;
    normal = dot(normal, normal) > 1e-12 ? normalize(normal) : -direction;
    if (dot(normal, direction) > 0.0)
        normal = -normal;
    vec3 sun =
        frame.color_ambient.rgb * frame.direction_intensity.w * max(dot(normal, -frame.direction_intensity.xyz), 0.0);
    vec3 sky = textureLod(irradiance_map, environment_uv(normal), 0).rgb * frame.render_params.z;

    vec3 local_light = vec3(0.0);
    for (int i = 0; i < int(frame.light_count.x); ++i) {
        LocalLight light = frame.lights[i];
        vec3 offset = light.position_range.xyz - hit_pos;
        float dist_sq = dot(offset, offset);
        float range = max(light.position_range.w, 0.001);
        if (dist_sq >= range * range || dist_sq < 1e-8)
            continue;
        float dist = sqrt(dist_sq);
        vec3 l = offset / dist;
        float n_dot_l = max(dot(normal, l), 0.0);
        if (n_dot_l <= 0.0)
            continue;
        float falloff = clamp(1.0 - pow(dist / range, 4.0), 0.0, 1.0);
        float attenuation = falloff * falloff / max(dist_sq, 0.01);
        if (light.params.y > 0.5) {
            float cone = dot(-l, normalize(light.direction_inner.xyz));
            float blend = clamp((cone - light.params.x) /
                                max(light.direction_inner.w - light.params.x, 1e-5), 0.0, 1.0);
            attenuation *= blend * blend * (3.0 - 2.0 * blend);
        }
        local_light += light.color_intensity.rgb * (light.color_intensity.w * attenuation * n_dot_l);
    }

    return base * (frame.color_ambient.w + sun + sky + local_light) + surface.emission_unlit.rgb;
}

bool trace_water_reflection(vec3 n, vec3 direction, float roughness, out vec3 radiance) {
    // Trace the upper surface of pools and rivers. Droplets retain environment lighting.
    if (frame.sky_params.w < .5 || (uint(draw.params.x) & (1u << 18u)) == 0u || n.y <= .15 || direction.y <= 0.0 ||
        roughness >= .6)
        return false;
    rayQueryEXT query;
    vec3 origin = world_position + vec3(0, .003, 0);
    rayQueryInitializeEXT(query, scene_acceleration, gl_RayFlagsOpaqueEXT, 2, origin, .002,
                          direction, 150.0);
    while (rayQueryProceedEXT(query)) {
    }
    if (rayQueryGetIntersectionTypeEXT(query, true) == gl_RayQueryCommittedIntersectionNoneEXT)
        return false;
    float hit_t = rayQueryGetIntersectionTEXT(query, true);
    vec3 hit_pos = origin + direction * hit_t;
    radiance = shade_reflection_hit(query, hit_pos, direction);
    return true;
}

void create_orthonormal_basis(vec3 n, out vec3 b1, out vec3 b2) {
    if (abs(n.z) > 0.70710678) {
        float a = n.y * n.y + n.z * n.z;
        float k = 1.0 / sqrt(max(1e-12, a));
        b1 = vec3(0.0, -n.z * k, n.y * k);
        b2 = vec3(a * k, -n.x * b1.z, n.x * b1.y);
    } else {
        float a = n.x * n.x + n.y * n.y;
        float k = 1.0 / sqrt(max(1e-12, a));
        b1 = vec3(-n.y * k, n.x * k, 0.0);
        b2 = vec3(-n.z * b1.y, n.z * b1.x, a * k);
    }
}

vec2 sample_hammersley(uint i, uint N, vec2 seed) {
    uint bits = i;
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    float r = float(bits) * 2.3283064365386963e-10;
    return fract(vec2(float(i) / float(N), r) + seed);
}

vec3 sample_cosine_hemisphere(vec2 u, vec3 n, vec3 b1, vec3 b2) {
    float phi = 6.28318530718 * u.x;
    float cos_theta = sqrt(clamp(1.0 - u.y, 0.0, 1.0));
    float sin_theta = sqrt(clamp(u.y, 0.0, 1.0));
    return normalize(b1 * (cos(phi) * sin_theta) + b2 * (sin(phi) * sin_theta) + n * cos_theta);
}

vec2 hash22(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.xx + p3.yz) * p3.zy);
}

vec3 sample_ggx(vec2 u, vec3 n, vec3 b1, vec3 b2, float roughness) {
    float alpha = max(roughness * roughness, 0.002);
    float alpha2 = alpha * alpha;
    float phi = 6.28318530718 * u.x;
    float cos_theta = sqrt(clamp((1.0 - u.y) / (1.0 + (alpha2 - 1.0) * u.y), 0.0, 1.0));
    float sin_theta = sqrt(max(1.0 - cos_theta * cos_theta, 0.0));
    return normalize(b1 * (cos(phi) * sin_theta) + b2 * (sin(phi) * sin_theta) + n * cos_theta);
}

vec2 ray_effect_seed() {
    // Stable without TAA (no frame-to-frame sparkle); subpixel TAA jitter naturally changes the seed.
    return hash22(gl_FragCoord.xy);
}

bool trace_glossy_reflection(vec3 pos, vec3 n, vec3 v, float roughness, out vec3 radiance) {
    if (frame.ray_params.x < 0.5 || roughness >= 0.75)
        return false;

    float rough = clamp(roughness, 0.02, 0.75);
    vec3 b1, b2;
    create_orthonormal_basis(n, b1, b2);
    vec3 h = sample_ggx(ray_effect_seed(), n, b1, b2, rough);
    vec3 direction = reflect(-v, h);
    if (dot(direction, n) <= 0.0)
        return false;

    float bias = max(0.005, frame.shadow_params.w * 0.15);
    vec3 origin = pos + n * bias;
    rayQueryEXT query;
    rayQueryInitializeEXT(query, scene_acceleration, gl_RayFlagsOpaqueEXT, 2, origin, 0.003, direction, 150.0);
    while (rayQueryProceedEXT(query)) {
    }
    if (rayQueryGetIntersectionTypeEXT(query, true) == gl_RayQueryCommittedIntersectionNoneEXT) {
        radiance = textureLod(environment_map, environment_uv(direction), rough * 5.0).rgb * frame.render_params.z;
        return true;
    }

    float hit_t = rayQueryGetIntersectionTEXT(query, true);
    vec3 hit_pos = origin + direction * hit_t;
    radiance = shade_reflection_hit(query, hit_pos, direction);
    return true;
}

float dielectric_fresnel(float cos_i, float eta_i, float eta_t, out bool total_internal_reflection) {
    cos_i = clamp(abs(cos_i), 0.0, 1.0);
    float sin_i = sqrt(max(1.0 - cos_i * cos_i, 0.0));
    float sin_t = (eta_i / eta_t) * sin_i;
    total_internal_reflection = sin_t >= 1.0;
    if (total_internal_reflection)
        return 1.0;
    float cos_t = sqrt(max(1.0 - sin_t * sin_t, 0.0));
    float rs_denom = eta_i * cos_i + eta_t * cos_t;
    float rp_denom = eta_t * cos_i + eta_i * cos_t;
    float rs = abs(rs_denom) > 1e-7 ? (eta_i * cos_i - eta_t * cos_t) / rs_denom : 1.0;
    float rp = abs(rp_denom) > 1e-7 ? (eta_t * cos_i - eta_i * cos_t) / rp_denom : 1.0;
    return clamp(0.5 * (rs * rs + rp * rp), 0.0, 1.0);
}

bool trace_refraction(vec3 pos, vec3 n, vec3 v, float roughness, float ior,
                      out vec3 transmitted_radiance, out float fresnel) {
    transmitted_radiance = vec3(0.0);
    if (frame.ray_params.x < 0.5)
        return false;

    float material_ior = clamp(ior, 1.0, 3.0);
    bool entering = dot(n, v) >= 0.0;
    vec3 interface_normal = entering ? normalize(n) : -normalize(n);
    float eta_i = entering ? 1.0 : material_ior;
    float eta_t = entering ? material_ior : 1.0;
    float eta = eta_i / eta_t;
    bool tir = false;
    fresnel = dielectric_fresnel(dot(interface_normal, v), eta_i, eta_t, tir);
    if (tir) {
        transmitted_radiance = vec3(0.0);
        return true;
    }

    vec3 direction = refract(-v, interface_normal, eta);
    if (dot(direction, direction) < 1e-8) {
        fresnel = 1.0;
        return true;
    }
    direction = normalize(direction);
    vec3 origin = pos - interface_normal * 0.005;
    rayQueryEXT query;
    rayQueryInitializeEXT(query, scene_acceleration, gl_RayFlagsOpaqueEXT, 2, origin, 0.003, direction, 150.0);
    while (rayQueryProceedEXT(query)) {
    }
    if (rayQueryGetIntersectionTypeEXT(query, true) == gl_RayQueryCommittedIntersectionNoneEXT) {
        transmitted_radiance = textureLod(environment_map, environment_uv(direction), roughness * 5.0).rgb *
                               frame.render_params.z;
        return true;
    }

    float hit_t = rayQueryGetIntersectionTEXT(query, true);
    vec3 hit_pos = origin + direction * hit_t;
    vec3 hit_radiance = shade_reflection_hit(query, hit_pos, direction);
    if (roughness > 0.08) {
        vec3 environment_blur = textureLod(environment_map, environment_uv(direction), roughness * 5.0).rgb *
                                frame.render_params.z;
        transmitted_radiance = mix(hit_radiance, environment_blur, smoothstep(0.08, 0.75, roughness));
    } else {
        transmitted_radiance = hit_radiance;
    }
    return true;
}

bool trace_rtao(vec3 pos, vec3 geometric_normal, out float ao) {
    if (frame.ray_params.x < 0.5) {
        ao = 1.0;
        return false;
    }

    vec3 geometric = dot(geometric_normal, geometric_normal) > 1e-12
                         ? normalize(geometric_normal)
                         : vec3(0.0, 1.0, 0.0);
    vec3 b1, b2;
    create_orthonormal_basis(geometric, b1, b2);
    float bias = max(0.008, frame.shadow_params.w * 0.15);
    vec3 origin = pos + geometric * bias;

    const uint samples = 4u;
    const float radius = 2.0;
    vec2 seed = hash22(gl_FragCoord.xy + vec2(13.0, 37.0));
    float total_occlusion = 0.0;
    for (uint s = 0u; s < samples; ++s) {
        vec2 u = sample_hammersley(s, samples, seed);
        vec3 direction = sample_cosine_hemisphere(u, geometric, b1, b2);
        rayQueryEXT query;
        rayQueryInitializeEXT(query, scene_acceleration,
                              gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT,
                              2, origin, 0.005, direction, radius);
        while (rayQueryProceedEXT(query)) {
        }
        if (rayQueryGetIntersectionTypeEXT(query, true) != gl_RayQueryCommittedIntersectionNoneEXT) {
            float hit_t = rayQueryGetIntersectionTEXT(query, true);
            float occlusion = clamp(1.0 - hit_t / radius, 0.0, 1.0);
            total_occlusion += occlusion * occlusion;
        }
    }
    ao = clamp(1.0 - total_occlusion / float(samples), 0.0, 1.0);
    return true;
}

float compute_gi_falloff(float d, float max_dist) {
    float norm = clamp(d / max_dist, 0.0, 1.0);
    float window = clamp(1.0 - norm * norm, 0.0, 1.0);
    float dist_sq = d * d;
    return (1.0 / (1.0 + dist_sq * 0.25)) * window;
}

vec3 shade_gi_hit(rayQueryEXT query, vec3 hit_pos, vec3 direction, float max_dist, float hit_t, out vec3 hit_base,
                  out vec3 hit_normal, out bool can_bounce, bool include_environment) {
    ReflectionSurface surface = reflection.surfaces[rayQueryGetIntersectionInstanceCustomIndexEXT(query, true)];
    MeshWords vertices = MeshWords(surface.addresses.xy);
    MeshWords indices = MeshWords(surface.addresses.zw);
    uint triangle = 3u * uint(rayQueryGetIntersectionPrimitiveIndexEXT(query, true));
    uvec3 index = uvec3(indices.words[triangle], indices.words[triangle + 1], indices.words[triangle + 2]);
    vec2 uv_hit = rayQueryGetIntersectionBarycentricsEXT(query, true);
    vec3 weights = vec3(1.0 - uv_hit.x - uv_hit.y, uv_hit);
    vec3 base = surface.color.rgb * (vertex_attribute(vertices, index.x, 3) * weights.x +
                                     vertex_attribute(vertices, index.y, 3) * weights.y +
                                     vertex_attribute(vertices, index.z, 3) * weights.z);
    hit_base = base;
    hit_normal = -direction;
    can_bounce = false;
    if (surface.emission_unlit.w > .5)
        return base + surface.emission_unlit.rgb;

    vec3 normal = vertex_attribute(vertices, index.x, 7) * weights.x +
                  vertex_attribute(vertices, index.y, 7) * weights.y +
                  vertex_attribute(vertices, index.z, 7) * weights.z;
    if (dot(normal, normal) < 1e-12) {
        vec3 a = vertex_attribute(vertices, index.x, 0);
        normal = cross(vertex_attribute(vertices, index.y, 0) - a, vertex_attribute(vertices, index.z, 0) - a);
    }
    normal = transpose(mat3(rayQueryGetIntersectionWorldToObjectEXT(query, true))) * normal;
    normal = dot(normal, normal) > 1e-12 ? normalize(normal) : -direction;
    hit_normal = normal;

    // Discard backface hits
    float cos_emitter = dot(normal, -direction);
    if (cos_emitter <= 0.0)
        return vec3(0.0);
    can_bounce = true;

    float falloff = compute_gi_falloff(hit_t, max_dist);
    vec3 emissive = surface.emission_unlit.rgb;

    vec3 sun_dir = -frame.direction_intensity.xyz;
    float n_dot_l = max(dot(normal, sun_dir), 0.0);
    vec3 sun = vec3(0.0);
    if (n_dot_l > 0.0 && frame.direction_intensity.w > 0.0) {
        // GI instances carry mask 4 even when the main shadow effect is disabled.
        float visibility = trace_sun_visibility(hit_pos, normal, sun_dir, 0.005, 4u);
        sun = frame.color_ambient.rgb * frame.direction_intensity.w * n_dot_l * visibility;
    }
    vec3 sky = include_environment ? textureLod(irradiance_map, environment_uv(normal), 0).rgb * frame.render_params.z : vec3(0.0);
    vec3 reflected = base * (frame.color_ambient.w + sun + sky);
    return (reflected + emissive) * (cos_emitter * falloff);
}

bool trace_gi_bounce(vec3 pos, vec3 normal, out vec3 gi_radiance) {
    if (frame.rtgi_params.x < 0.5)
        return false;
    uint samples = clamp(uint(frame.rtgi_params.w + 0.5), 1u, 4u);
    float max_dist = max(1.0, frame.rtgi_params.z);
    uint max_bounces = clamp(uint(frame.ray_params.w + 0.5), 1u, 4u);

    vec3 d1 = dFdx(pos);
    vec3 d2 = dFdy(pos);
    vec3 geometric = cross(d1, d2);
    geometric = dot(geometric, geometric) > 1e-12 ? normalize(geometric) : normal;
    if (dot(geometric, normal) < 0.0)
        geometric = -geometric;

    // Use the geometric normal for hemisphere sampling so rays never point into the surface geometry
    vec3 b1, b2;
    create_orthonormal_basis(geometric, b1, b2);
    vec3 initial_b1 = b1, initial_b2 = b2;

    float bias = max(0.005, frame.shadow_params.w * 0.15);
    vec3 origin = pos + geometric * bias;

    float frame_time = frame.wind_time.w > 0.0 ? frame.wind_time.w : frame.wind_time.z;
    float time_jitter = fract(frame_time * 0.6180339887);
    vec2 seed = hash22(gl_FragCoord.xy + vec2(time_jitter * 1000.0, time_jitter * 7919.0));

    vec3 sky_ambient = textureLod(irradiance_map, environment_uv(geometric), 0).rgb * frame.render_params.z;
    float sky_visibility = 0.0;
    vec3 bounce_accum = vec3(0.0);

    for (uint s = 0u; s < samples; ++s) {
        vec2 u = sample_hammersley(s, samples, seed);
        vec3 dir = sample_cosine_hemisphere(u, geometric, initial_b1, initial_b2);
        vec3 ray_origin = origin;
        vec3 throughput = vec3(1.0);
        for (uint bounce = 0u; bounce < max_bounces; ++bounce) {
            rayQueryEXT query;
            rayQueryInitializeEXT(query, scene_acceleration, gl_RayFlagsOpaqueEXT, 4, ray_origin, 0.003, dir, max_dist);
            while (rayQueryProceedEXT(query)) {
            }
            if (rayQueryGetIntersectionTypeEXT(query, true) == gl_RayQueryCommittedIntersectionNoneEXT) {
                if (bounce == 0u)
                    sky_visibility += 1.0;
                else {
                    vec3 sky = textureLod(irradiance_map, environment_uv(dir), 0).rgb * frame.render_params.z;
                    bounce_accum += throughput * clamp(sky, vec3(0.0), vec3(3.0));
                }
                break;
            }
            float hit_t = rayQueryGetIntersectionTEXT(query, true);
            vec3 hit_pos = ray_origin + dir * hit_t;
            vec3 hit_base, hit_normal;
            bool can_bounce;
            bool terminal = bounce + 1u >= max_bounces;
            vec3 hit_rad = shade_gi_hit(query, hit_pos, dir, max_dist, hit_t, hit_base, hit_normal, can_bounce,
                                        terminal);
            bounce_accum += throughput * clamp(hit_rad, vec3(0.0), vec3(3.0));
            if (!can_bounce || terminal)
                break;
            throughput *= clamp(hit_base, vec3(0.0), vec3(1.0));
            if (max(throughput.r, max(throughput.g, throughput.b)) < 0.01)
                break;
            vec3 bounce_b1, bounce_b2;
            create_orthonormal_basis(hit_normal, bounce_b1, bounce_b2);
            vec2 bounce_seed = hash22(seed + vec2(17.0 * float(bounce + 1u), 29.0 * float(bounce + 1u)));
            vec2 bounce_u = sample_hammersley(s, samples, bounce_seed);
            dir = sample_cosine_hemisphere(bounce_u, hit_normal, bounce_b1, bounce_b2);
            ray_origin = hit_pos + hit_normal * bias;
        }
    }
    vec3 indirect = (bounce_accum / float(samples)) * frame.rtgi_params.y;
    vec3 ambient = sky_ambient * (sky_visibility / float(samples));
    gi_radiance = ambient + indirect;
    return true;
}
