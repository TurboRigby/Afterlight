#version 450
/* SPDX-License-Identifier: MPL-2.0 */
layout(location=0) in vec2 uv;layout(location=1) in float alpha;
layout(set=1,binding=0) uniform sampler2D base_texture;
layout(push_constant) uniform Shadow {mat4 mvp;uvec4 skin_params;} shadow;
void main(){if(shadow.skin_params.y==1u&&texture(base_texture,uv).a*alpha<uintBitsToFloat(shadow.skin_params.w))discard;}
