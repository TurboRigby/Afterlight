#version 450
/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

layout(location = 0) in vec2 in_position;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec4 in_color;
layout(location = 0) out vec2 uv;
layout(location = 1) out vec4 color;
layout(push_constant) uniform Screen { vec2 size; } screen;
void main() {
    gl_Position = vec4(in_position / screen.size * 2.0 - 1.0, 0.0, 1.0);
    uv = in_uv;
    color = in_color;
}
