/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef AF_EDITOR_PANELS_H
#define AF_EDITOR_PANELS_H

#include "editor_state.h"

#ifdef __cplusplus
extern "C" {
#endif

void toolbar(editor *app, float width);
bool post_processing_controls(struct nk_context *ctx, af_render_settings *render);
void environment_panel(editor *app, float width, float height);
void post_processing_panel(editor *app, float width, float height);
void performance_panel(editor *app, float width, float height);
void asset_browser_panel(editor *app, float width, float height);
void play_hud(editor *app);
void animation_panel(editor *app, float width, float height);
void input_panel(editor *app);
void prefab_panel(editor *app);
void draw_splitters(editor *app, float width, float height);

#ifdef __cplusplus
}
#endif

#endif /* AF_EDITOR_PANELS_H */
