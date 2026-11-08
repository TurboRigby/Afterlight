/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef AF_EDITOR_GUI_H
#define AF_EDITOR_GUI_H
#include "nuklear_config.h"
#include <afterlight/afterlight.h>

typedef struct ed_gui {
    struct nk_context context;
    struct nk_font_atlas atlas;
    struct nk_draw_null_texture null;
    struct nk_buffer commands, vertices, indices;
    af_overlay_command *draws;
    size_t draw_capacity;
    uint32_t draw_count;
    bool initialized;
    bool text_input;
} ed_gui;
bool ed_gui_init(ed_gui *gui, af_engine *engine, const char *font_path);
void ed_gui_destroy(ed_gui *gui);
bool ed_gui_set_text_input(ed_gui *gui, af_engine *engine, bool enabled);
void ed_gui_unfocus(ed_gui *gui);
void ed_gui_input(ed_gui *gui, af_engine *engine);
bool ed_gui_render(ed_gui *gui, af_engine *engine, float width, float height);
#endif
