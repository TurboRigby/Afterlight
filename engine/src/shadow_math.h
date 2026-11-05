/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef AF_SHADOW_MATH_H
#define AF_SHADOW_MATH_H
#include <afterlight/afterlight.h>
#define AF_SHADOW_MAX_CASCADES 4
typedef struct af_shadow_bounds { af_vec3 corners[8]; } af_shadow_bounds;
typedef struct af_shadow_view {
    af_mat4 matrices[AF_SHADOW_MAX_CASCADES];
    float splits[AF_SHADOW_MAX_CASCADES];
    float texel_sizes[AF_SHADOW_MAX_CASCADES];
    uint32_t count;
} af_shadow_view;
af_shadow_bounds af_shadow_transform_bounds(af_mat4 model,af_vec3 minimum,af_vec3 maximum);
bool af_shadow_intersects(af_mat4 matrix,const af_shadow_bounds *bounds);
bool af_shadow_build(const af_camera *camera,float aspect,af_vec3 direction,af_shadow_settings settings,
    const af_shadow_bounds *casters,size_t caster_count,af_shadow_view *out);
#endif
