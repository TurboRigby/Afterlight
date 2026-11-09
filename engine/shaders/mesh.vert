#version 450
/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec4 in_color;
layout(location = 2) in vec3 in_normal;
layout(location = 3) in vec2 in_uv;
layout(location = 4) in uvec4 in_joints;
layout(location = 5) in vec4 in_weights;
layout(set = 0, binding = 6, std430) readonly buffer SkinMatrices {
    mat4 bones[];
}
skin;
layout(location = 0) out vec4 vertex_color;
layout(location = 1) out vec3 world_position;
layout(location = 2) out vec3 world_normal;
layout(location = 3) out vec2 uv;
layout(location = 4) out vec4 previous_clip;
layout(location = 5) out vec4 current_clip;
layout(location = 6) flat out uint motion_valid;
struct MotionRecord {
    mat4 model;
    uvec4 info;
};
layout(set = 0, binding = 9, std430) readonly buffer MotionRecords {
    MotionRecord objects[];
}
motion;
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
    vec4 ray_params;
    vec4 rtgi_flags;
}
frame;
layout(push_constant) uniform Draw {
    mat4 model;
    vec4 tint;
    vec4 params;
    vec4 surface;
    vec4 emission;
}
draw;
void main() {
    mat4 deformation = mat4(1);
    uint base = (uint(draw.params.x) & (1u << 19u)) != 0u ? 0u : uint(draw.surface.w);
    if (base > 0 && dot(in_weights, vec4(1)) > 0)
        deformation = skin.bones[base + in_joints.x] * in_weights.x + skin.bones[base + in_joints.y] * in_weights.y +
                      skin.bones[base + in_joints.z] * in_weights.z + skin.bones[base + in_joints.w] * in_weights.w;
    vec4 world = draw.model * deformation * vec4(in_position, 1);
    gl_Position = frame.view_projection * world;
    current_clip = frame.unjittered_view_projection * world;
    previous_clip = current_clip;
    motion_valid = 0;
    if (frame.sky_params.y > 0.5) {
        MotionRecord old = motion.objects[gl_InstanceIndex];
        motion_valid = old.info.y;
        mat4 old_skin = mat4(1);
        if (base > 0 && old.info.x > 0 && dot(in_weights, vec4(1)) > 0) {
            uint b = old.info.x;
            old_skin = skin.bones[b + in_joints.x] * in_weights.x + skin.bones[b + in_joints.y] * in_weights.y +
                       skin.bones[b + in_joints.z] * in_weights.z + skin.bones[b + in_joints.w] * in_weights.w;
        }
        previous_clip = frame.previous_view_projection * old.model * old_skin * vec4(in_position, 1);
    }
    world_position = world.xyz;
    uv = in_uv;
    vertex_color = in_color * draw.tint;
    mat3 basis = mat3(draw.model * deformation);
    world_normal = abs(determinant(basis)) > 1e-12 ? transpose(inverse(basis)) * in_normal : vec3(0);
}
