/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef AF_EDITOR_VIEWPORT_OVERLAYS_H
#define AF_EDITOR_VIEWPORT_OVERLAYS_H

#include "editor_state.h"

#ifdef __cplusplus
extern "C" {
#endif

void draw_colliders(editor *app);
void draw_skeleton(editor *app);
void draw_physics(editor *app);
void draw_lights(editor *app);
void draw_gizmo(editor *app);

#ifdef __cplusplus
}
#endif

#endif /* AF_EDITOR_VIEWPORT_OVERLAYS_H */
