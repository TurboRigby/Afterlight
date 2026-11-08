/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef ED_COLLIDER_VIEW_H
#define ED_COLLIDER_VIEW_H
#include <afterlight/collision.h>
#include <afterlight/joint.h>
typedef struct ed_line3 {
    af_vec3 a, b;
} ed_line3;
#define ED_COLLIDER_MAX_LINES 768
size_t ed_joint_lines(const af_scene *, af_joint, ed_line3 *, size_t);
size_t ed_collider_lines(af_world_collider collider, ed_line3 *out, size_t capacity);
#endif
