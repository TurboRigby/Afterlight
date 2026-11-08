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
vec3 shade_reflection_hit(rayQueryEXT query, vec3 direction) {
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
    return base * (frame.color_ambient.w + sun + sky) + surface.emission_unlit.rgb;
}

bool trace_water_reflection(vec3 n, vec3 direction, float roughness, out vec3 radiance) {
    // Trace the upper surface of pools and rivers. Droplets retain environment lighting.
    if (frame.sky_params.w < .5 || (uint(draw.params.x) & (1u << 18u)) == 0u || n.y <= .15 || direction.y <= 0.0 ||
        roughness >= .6)
        return false;
    rayQueryEXT query;
    rayQueryInitializeEXT(query, scene_acceleration, gl_RayFlagsOpaqueEXT, 2, world_position + vec3(0, .003, 0), .002,
                          direction, 150.0);
    while (rayQueryProceedEXT(query)) {
    }
    if (rayQueryGetIntersectionTypeEXT(query, true) == gl_RayQueryCommittedIntersectionNoneEXT)
        return false;
    radiance = shade_reflection_hit(query, direction);
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

bool trace_gi_bounce(vec3 pos, vec3 normal, out vec3 gi_radiance) {
    if (frame.rtgi_params.x < 0.5)
        return false;
    uint samples = clamp(uint(frame.rtgi_params.w + 0.5), 1u, 4u);
    float max_dist = max(1.0, frame.rtgi_params.z);

    vec3 d1 = dFdx(pos);
    vec3 d2 = dFdy(pos);
    vec3 geometric = cross(d1, d2);
    geometric = dot(geometric, geometric) > 1e-12 ? normalize(geometric) : normal;
    if (dot(geometric, normal) < 0.0)
        geometric = -geometric;

    float bias = max(0.003, frame.shadow_params.w * 0.15);
    vec3 origin = pos + geometric * bias;

    vec3 b1, b2;
    create_orthonormal_basis(normal, b1, b2);

    vec2 seed = fract(gl_FragCoord.xy * vec2(0.754877666, 0.569840291));

    vec3 accum = vec3(0.0);
    for (uint s = 0u; s < samples; ++s) {
        vec2 u = sample_hammersley(s, samples, seed);
        vec3 dir = sample_cosine_hemisphere(u, normal, b1, b2);

        rayQueryEXT query;
        rayQueryInitializeEXT(query, scene_acceleration, gl_RayFlagsOpaqueEXT, 4, origin, 0.002, dir, max_dist);
        while (rayQueryProceedEXT(query)) {
        }
        if (rayQueryGetIntersectionTypeEXT(query, true) != gl_RayQueryCommittedIntersectionNoneEXT) {
            accum += shade_reflection_hit(query, dir);
        } else {
            accum += textureLod(irradiance_map, environment_uv(dir), 0).rgb * frame.render_params.z;
        }
    }
    gi_radiance = (accum / float(samples)) * frame.rtgi_params.y;
    return true;
}
