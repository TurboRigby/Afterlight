#version 450
/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

layout(location=0) in vec3 in_position;
layout(push_constant) uniform Shadow {mat4 mvp;} shadow;
void main(){gl_Position=shadow.mvp*vec4(in_position,1);}
