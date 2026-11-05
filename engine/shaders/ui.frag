#version 450
/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

layout(set = 0, binding = 0) uniform sampler2D atlas;
layout(location = 0) in vec2 uv;
layout(location = 1) in vec4 color;
layout(location = 0) out vec4 out_color;
void main() {
    vec4 sample_color = texture(atlas, uv) * color;
    vec3 linear_color = mix(sample_color.rgb / 12.92,
        pow((sample_color.rgb + 0.055) / 1.055, vec3(2.4)),
        greaterThan(sample_color.rgb, vec3(0.04045)));
    out_color = vec4(linear_color, sample_color.a);
}
