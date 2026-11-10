/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef AF_EDITOR_LANDSCAPE_EDITOR_H
#define AF_EDITOR_LANDSCAPE_EDITOR_H

#include "core/editor_state.h"

#ifdef __cplusplus
extern "C" {
#endif

void landscape_panel(editor *app, float height);
void draw_landscape_brush(editor *app);
void landscape_update_stroke(editor *app, double delta, af_vec2 mouse, bool shift);

#ifdef __cplusplus
}
#endif

#endif /* AF_EDITOR_LANDSCAPE_EDITOR_H */
