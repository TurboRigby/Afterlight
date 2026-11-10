/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef ED_GIZMO_H
#define ED_GIZMO_H
#include <afterlight/afterlight.h>
#define ED_GIZMO_SEGMENTS 64
typedef enum ed_gizmo_mode { ED_GIZMO_MOVE, ED_GIZMO_ROTATE, ED_GIZMO_SCALE } ed_gizmo_mode;
typedef struct ed_gizmo {
    ed_gizmo_mode mode;
    bool snap, dragging, projected;
    int axis;
    af_object_id object;
    af_transform initial;
    af_vec2 origin, ends[3], rings[3][ED_GIZMO_SEGMENTS + 1], start, direction;
    float units, axis_units[3], angle, accumulated;
    af_camera camera;
    af_rect viewport;
    af_mat4 parent_inverse;
    af_vec3 center, plane_normals[3], local_axes[3], previous_vector;
    bool plane_valid;
} ed_gizmo;
bool ed_project(const af_camera *camera, af_rect viewport, af_vec3 point, af_vec2 *screen);
void ed_gizmo_project(ed_gizmo *g, const af_camera *camera, af_rect viewport, af_vec3 center, af_mat4 parent,
                      af_vec3 rotation);
int ed_gizmo_hit(const ed_gizmo *g, af_vec2 point);
void ed_gizmo_begin(ed_gizmo *g, int axis, af_object_id id, af_transform initial, af_vec2 mouse);
af_transform ed_gizmo_drag(ed_gizmo *g, af_vec2 mouse);
#endif
