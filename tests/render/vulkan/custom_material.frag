#version 450
/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

layout(location = 0) in vec4 vertex_color;
layout(location = 1) in vec3 world_position;
layout(location = 2) in vec3 world_normal;
layout(location = 3) in vec2 uv;
layout(location = 4) in vec4 previous_clip;
layout(location = 5) in vec4 current_clip;
layout(location = 6) flat in uint motion_valid;

layout(location = 0) out vec4 out_color;
layout(location = 1) out vec4 out_motion;
layout(location = 2) out vec4 out_gi;

void main() {
    out_color = vec4(1.0, 0.0, 1.0, 1.0);
    out_motion = vec4(0.0, 0.0, 0.0, 0.0);
    out_gi = vec4(0.0, 0.0, 0.0, 0.0);
}
