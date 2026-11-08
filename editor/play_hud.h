/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef ED_PLAY_HUD_H
#define ED_PLAY_HUD_H
#include "gui.h"
void ed_play_hud(struct nk_context *context, af_engine *engine, af_rect viewport, bool paused, bool free_camera,
                 bool editor, bool player);
#endif
