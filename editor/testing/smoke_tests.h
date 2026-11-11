/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef AF_EDITOR_SMOKE_TESTS_H
#define AF_EDITOR_SMOKE_TESTS_H

#include "core/editor_state.h"

#ifdef __cplusplus
extern "C" {
#endif

void smoke_click(editor *app, float x, float y);
void smoke_button(editor *app, Uint8 button, bool down);
void smoke_key(editor *app, SDL_Scancode key);
void animation_tools_smoke(editor *app);
void animation_authoring_smoke(editor *app);
void light_tools_smoke(editor *app);
void play_controls_smoke(editor *app);
void smoke(editor *app);

#ifdef __cplusplus
}
#endif

#endif /* AF_EDITOR_SMOKE_TESTS_H */
