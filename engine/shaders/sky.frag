#version 450
#extension GL_GOOGLE_include_directive : require
// SPDX-License-Identifier: MPL-2.0
layout(location=0) in vec2 uv;
layout(location=0) out vec4 out_color;
struct LocalLight {vec4 position_range;vec4 color_intensity;vec4 direction_inner;vec4 params;};
layout(set=0,binding=0) uniform Frame {
    mat4 view_projection;
    vec4 direction_intensity;
    vec4 color_ambient;
    mat4 shadow_matrices[4];
    vec4 cascade_splits,camera_position,camera_forward,shadow_params,cascade_params,render_params;
    LocalLight lights[16];vec4 light_count;mat4 local_shadow_matrices[24];
    mat4 inverse_view_projection;vec4 sky_params;
    vec4 water_absorption,water_scattering,water_bounds,water_axes,water_screen,water_viewport;
} frame;
layout(set=0,binding=5) uniform sampler2D sky_map;
#include "water_fog.glsl"
void main(){
    vec4 p=frame.inverse_view_projection*vec4(uv*2-1,1,1);
    vec3 direction=normalize(p.xyz/p.w-frame.camera_position.xyz);
    vec2 coordinate=vec2((atan(direction.z,direction.x)-frame.render_params.w)/(2*3.14159265359)+.5,acos(clamp(direction.y,-1,1))/3.14159265359);
    vec3 color=textureLod(sky_map,coordinate,0).rgb*frame.sky_params.x;out_color=vec4(clamp(underwater_color(color,frame.camera_position.xyz+direction*100),vec3(0),vec3(60000)),1);
}
