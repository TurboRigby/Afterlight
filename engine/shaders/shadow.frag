#version 450
/* SPDX-License-Identifier: MPL-2.0 */
layout(location = 0) in vec2 uv;
layout(location = 1) in float alpha;
layout(set = 1, binding = 0) uniform sampler2D base_texture;
layout(push_constant) uniform Shadow {
    mat4 mvp;
    uvec4 skin_params;
}
shadow;
void main() {
    uint material = shadow.skin_params.y;
    bool front = gl_FrontFacing != ((material & (1u << 11u)) != 0u);
    if (!front && (material & (1u << 9u)) != 0u)
        discard;
    if ((material & 0xffu) == 1u &&
        uint(round(clamp(texture(base_texture, uv).a * alpha, 0, 1) * 65535.0)) < shadow.skin_params.w)
        discard;
}
