/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ED_LIGHT_GIZMO_H
#define ED_LIGHT_GIZMO_H
#include "gizmo.h"
#include "collider_view.h"
#define ED_LIGHT_MAX_LINES 100
size_t ed_light_lines(af_local_light light, ed_line3 *out, size_t capacity);
int ed_light_pick(const af_scene *scene, const af_camera *camera, af_rect viewport, af_vec2 mouse);
af_transform ed_light_transform(af_local_light light);
af_local_light ed_light_apply(af_local_light light, af_transform transform, ed_gizmo_mode mode, int axis);
#endif
