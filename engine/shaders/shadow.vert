#version 450
/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec4 in_color;
layout(location = 3) in vec2 in_uv;
layout(location = 0) out vec2 uv;
layout(location = 1) out float alpha;
layout(location = 4) in uvec4 in_joints;
layout(location = 5) in vec4 in_weights;
layout(set = 0, binding = 6, std430) readonly buffer SkinMatrices {
    mat4 bones[];
}
skin;
layout(push_constant) uniform Shadow {
    mat4 mvp;
    uvec4 skin_params;
}
shadow;
void main() {
    mat4 deformation = mat4(1);
    uint base = shadow.skin_params.x;
    if (base > 0 && dot(in_weights, vec4(1)) > 0)
        deformation = skin.bones[base + in_joints.x] * in_weights.x + skin.bones[base + in_joints.y] * in_weights.y +
                      skin.bones[base + in_joints.z] * in_weights.z + skin.bones[base + in_joints.w] * in_weights.w;
    uv = in_uv;
    alpha = in_color.a * uintBitsToFloat(shadow.skin_params.z);
    gl_Position = shadow.mvp * deformation * vec4(in_position, 1);
}
