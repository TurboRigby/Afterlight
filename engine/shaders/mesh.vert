#version 450
/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

layout(location=0) in vec3 in_position;
layout(location=1) in vec4 in_color;
layout(location=2) in vec3 in_normal;
layout(location=3) in vec2 in_uv;
layout(location=0) out vec4 vertex_color;
layout(location=1) out vec3 world_position;
layout(location=2) out vec3 world_normal;
layout(location=3) out vec2 uv;
struct LocalLight {vec4 position_range;vec4 color_intensity;vec4 direction_inner;vec4 params;};
layout(set=0,binding=0) uniform Frame {
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
    mat4 inverse_view_projection;vec4 sky_params;
} frame;
layout(push_constant) uniform Draw {mat4 model;vec4 tint;vec4 params;vec4 surface;vec4 emission;} draw;
void main(){
    vec4 world=draw.model*vec4(in_position,1);
    gl_Position=frame.view_projection*world;
    world_position=world.xyz;uv=in_uv;vertex_color=in_color*draw.tint;
    mat3 basis=mat3(draw.model);
    world_normal=abs(determinant(basis))>1e-12?transpose(inverse(basis))*in_normal:vec3(0);
}
