/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "gui.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static void copy_clipboard(nk_handle handle, const char *text, int length) {
    if (length <= 0)
        return;
    char *copy = malloc((size_t)length + 1);
    if (!copy)
        return;
    memcpy(copy, text, (size_t)length);
    copy[length] = 0;
    (void)af_engine_set_clipboard_text(handle.ptr, copy);
    free(copy);
}
static void paste_clipboard(nk_handle handle, struct nk_text_edit *edit) {
    const char *text = af_engine_get_clipboard_text(handle.ptr);
    size_t length = strlen(text);
    if (length < 1048576)
        nk_textedit_paste(edit, text, (int)length);
}
bool ed_gui_init(ed_gui *gui, af_engine *engine, const char *font_path) {
    memset(gui, 0, sizeof(*gui));
    nk_font_atlas_init_default(&gui->atlas);
    nk_font_atlas_begin(&gui->atlas);
    static const nk_rune ranges[] = {0x20, 0xff, 0x400, 0x52f, 0x2000, 0x206f, 0};
    struct nk_font_config config = nk_font_config(17);
    config.oversample_h = 3;
    config.oversample_v = 3;
    config.range = ranges;
    struct nk_font *font = nk_font_atlas_add_from_file(&gui->atlas, font_path, 17, &config);
    if (!font) {
        nk_font_atlas_clear(&gui->atlas);
        return false;
    }
    int width = 0, height = 0;
    const void *pixels = nk_font_atlas_bake(&gui->atlas, &width, &height, NK_FONT_ATLAS_RGBA32);
    if (!pixels || width <= 0 || height <= 0 ||
        !af_engine_upload_overlay_atlas(engine, pixels, (uint32_t)width, (uint32_t)height)) {
        nk_font_atlas_clear(&gui->atlas);
        return false;
    }
    nk_font_atlas_end(&gui->atlas, nk_handle_id(1), &gui->null);
    if (!nk_init_default(&gui->context, &font->handle)) {
        nk_font_atlas_clear(&gui->atlas);
        return false;
    }
    gui->context.clip.copy = copy_clipboard;
    gui->context.clip.paste = paste_clipboard;
    gui->context.clip.userdata = nk_handle_ptr(engine);
    nk_buffer_init_default(&gui->commands);
    nk_buffer_init_default(&gui->vertices);
    nk_buffer_init_default(&gui->indices);
    gui->initialized = true;
    struct nk_color colors[NK_COLOR_COUNT];
    for (int i = 0; i < NK_COLOR_COUNT; ++i)
        colors[i] = nk_rgb(50, 47, 58);
    colors[NK_COLOR_TEXT] = nk_rgb(226, 223, 232);
    colors[NK_COLOR_WINDOW] = nk_rgb(35, 33, 42);
    colors[NK_COLOR_HEADER] = nk_rgb(43, 40, 51);
    colors[NK_COLOR_BORDER] = nk_rgb(61, 57, 70);
    colors[NK_COLOR_BUTTON] = nk_rgb(57, 53, 66);
    colors[NK_COLOR_BUTTON_HOVER] = nk_rgb(73, 66, 82);
    colors[NK_COLOR_BUTTON_ACTIVE] = nk_rgb(105, 83, 55);
    colors[NK_COLOR_TOGGLE] = nk_rgb(64, 60, 73);
    colors[NK_COLOR_TOGGLE_HOVER] = nk_rgb(89, 78, 94);
    colors[NK_COLOR_TOGGLE_CURSOR] = nk_rgb(237, 179, 90);
    colors[NK_COLOR_SELECT] = nk_rgb(35, 33, 42);
    colors[NK_COLOR_SELECT_ACTIVE] = nk_rgb(101, 76, 46);
    colors[NK_COLOR_SLIDER] = nk_rgb(61, 57, 70);
    colors[NK_COLOR_SLIDER_CURSOR] = nk_rgb(200, 151, 83);
    colors[NK_COLOR_SLIDER_CURSOR_HOVER] = nk_rgb(237, 179, 90);
    colors[NK_COLOR_SLIDER_CURSOR_ACTIVE] = nk_rgb(255, 196, 105);
    colors[NK_COLOR_PROPERTY] = nk_rgb(27, 26, 34);
    colors[NK_COLOR_EDIT] = nk_rgb(27, 26, 34);
    colors[NK_COLOR_EDIT_CURSOR] = nk_rgb(237, 179, 90);
    colors[NK_COLOR_COMBO] = nk_rgb(49, 45, 57);
    colors[NK_COLOR_SCROLLBAR] = nk_rgb(29, 27, 35);
    colors[NK_COLOR_SCROLLBAR_CURSOR] = nk_rgb(75, 68, 87);
    colors[NK_COLOR_SCROLLBAR_CURSOR_HOVER] = nk_rgb(102, 91, 112);
    colors[NK_COLOR_SCROLLBAR_CURSOR_ACTIVE] = nk_rgb(131, 109, 133);
    nk_style_from_table(&gui->context, colors);
    struct nk_style *style = &gui->context.style;
    style->window.padding = nk_vec2(16, 14);
    style->window.spacing = nk_vec2(8, 9);
    style->window.border = 1;
    style->button.rounding = 3;
    style->button.padding = nk_vec2(10, 7);
    style->edit.rounding = 3;
    style->edit.padding = nk_vec2(8, 6);
    style->property.rounding = 3;
    style->property.padding = nk_vec2(2, 4);
    style->selectable.rounding = 3;
    style->selectable.padding = nk_vec2(8, 5);
    style->window.group_padding = nk_vec2(0, 0);
    return af_engine_set_text_input(engine, false);
}
void ed_gui_destroy(ed_gui *gui) {
    if (!gui->initialized)
        return;
    nk_buffer_free(&gui->commands);
    nk_buffer_free(&gui->vertices);
    nk_buffer_free(&gui->indices);
    nk_free(&gui->context);
    nk_font_atlas_clear(&gui->atlas);
    free(gui->draws);
    memset(gui, 0, sizeof(*gui));
}
void ed_gui_input(ed_gui *gui, af_engine *engine) {
    struct nk_context *context = &gui->context;
    nk_input_begin(context);
    af_vec2 mouse = af_engine_mouse_position(engine), scroll = af_engine_mouse_scroll(engine);
    nk_input_motion(context, (int)mouse.x, (int)mouse.y);
    const enum nk_buttons buttons[] = {NK_BUTTON_LEFT, NK_BUTTON_RIGHT, NK_BUTTON_MIDDLE};
    for (int i = 0; i < AF_MOUSE_BUTTON_COUNT; ++i) {
        bool down = af_engine_mouse_down(engine, (af_mouse_button)i);
        /* Preserve a press for one UI frame even when both events arrive together. */
        nk_input_button(context, buttons[i], (int)mouse.x, (int)mouse.y,
                        down || af_engine_mouse_pressed(engine, (af_mouse_button)i));
    }
    nk_input_scroll(context, nk_vec2(scroll.x, scroll.y));
    bool control = af_engine_key_down(engine, AF_KEY_LEFT_CONTROL) || af_engine_key_down(engine, AF_KEY_LEFT_SUPER) ||
                   af_engine_key_pressed(engine, AF_KEY_LEFT_CONTROL) ||
                   af_engine_key_pressed(engine, AF_KEY_LEFT_SUPER);
    const struct {
        enum nk_keys nk;
        af_key key;
    } keys[] = {{NK_KEY_SHIFT, AF_KEY_LEFT_SHIFT},    {NK_KEY_DEL, AF_KEY_DELETE},
                {NK_KEY_ENTER, AF_KEY_ENTER},         {NK_KEY_TAB, AF_KEY_TAB},
                {NK_KEY_BACKSPACE, AF_KEY_BACKSPACE}, {NK_KEY_LEFT, AF_KEY_LEFT},
                {NK_KEY_RIGHT, AF_KEY_RIGHT},         {NK_KEY_UP, AF_KEY_UP},
                {NK_KEY_DOWN, AF_KEY_DOWN},           {NK_KEY_TEXT_START, AF_KEY_HOME},
                {NK_KEY_TEXT_END, AF_KEY_END}};
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
        bool down = af_engine_key_down(engine, keys[i].key);
        if (af_engine_key_repeated(engine, keys[i].key))
            nk_input_key(context, keys[i].nk, nk_false);
        if ((af_engine_key_pressed(engine, keys[i].key) || af_engine_key_repeated(engine, keys[i].key)) && !down)
            nk_input_key(context, keys[i].nk, nk_true);
        nk_input_key(context, keys[i].nk, down);
    }
    nk_input_key(context, NK_KEY_COPY,
                 control && (af_engine_key_down(engine, AF_KEY_C) || af_engine_key_pressed(engine, AF_KEY_C)));
    nk_input_key(context, NK_KEY_PASTE,
                 control && (af_engine_key_down(engine, AF_KEY_V) || af_engine_key_pressed(engine, AF_KEY_V)));
    nk_input_key(context, NK_KEY_CUT,
                 control && (af_engine_key_down(engine, AF_KEY_X) || af_engine_key_pressed(engine, AF_KEY_X)));
    nk_input_key(context, NK_KEY_TEXT_SELECT_ALL,
                 control && (af_engine_key_down(engine, AF_KEY_A) || af_engine_key_pressed(engine, AF_KEY_A)));
    nk_input_key(context, NK_KEY_TEXT_UNDO,
                 control && (af_engine_key_down(engine, AF_KEY_Z) || af_engine_key_pressed(engine, AF_KEY_Z)));
    nk_input_key(context, NK_KEY_TEXT_REDO,
                 control && (af_engine_key_down(engine, AF_KEY_Y) || af_engine_key_pressed(engine, AF_KEY_Y)));
    if (!control) {
        const char *text = af_engine_text_input(engine);
        while (*text) {
            nk_rune rune;
            int size = nk_utf_decode(text, &rune, NK_UTF_SIZE);
            if (size <= 0)
                break;
            nk_input_unicode(context, rune);
            text += size;
        }
    }
    nk_input_end(context);
}
bool ed_gui_render(ed_gui *gui, af_engine *engine, float width, float height) {
    nk_buffer_clear(&gui->commands);
    nk_buffer_clear(&gui->vertices);
    nk_buffer_clear(&gui->indices);
    static const struct nk_draw_vertex_layout_element layout[] = {
        {NK_VERTEX_POSITION, NK_FORMAT_FLOAT, offsetof(af_overlay_vertex, position)},
        {NK_VERTEX_TEXCOORD, NK_FORMAT_FLOAT, offsetof(af_overlay_vertex, uv)},
        {NK_VERTEX_COLOR, NK_FORMAT_R8G8B8A8, offsetof(af_overlay_vertex, color)},
        {NK_VERTEX_LAYOUT_END}};
    struct nk_convert_config config = {0};
    config.vertex_layout = layout;
    config.vertex_size = sizeof(af_overlay_vertex);
    config.vertex_alignment = _Alignof(af_overlay_vertex);
    config.tex_null = gui->null;
    config.circle_segment_count = 22;
    config.curve_segment_count = 22;
    config.arc_segment_count = 22;
    config.global_alpha = 1;
    config.shape_AA = NK_ANTI_ALIASING_ON;
    config.line_AA = NK_ANTI_ALIASING_ON;
    if (nk_convert(&gui->context, &gui->commands, &gui->vertices, &gui->indices, &config) != NK_CONVERT_SUCCESS)
        return false;
    size_t count = 0;
    const struct nk_draw_command *command;
    nk_draw_foreach(command, &gui->context, &gui->commands)++ count;
    if (count > gui->draw_capacity) {
        af_overlay_command *draws = realloc(gui->draws, count * sizeof(*draws));
        if (!draws)
            return false;
        gui->draws = draws;
        gui->draw_capacity = count;
    }
    uint32_t first = 0, index = 0;
    nk_draw_foreach(command, &gui->context, &gui->commands) {
        gui->draws[index++] = (af_overlay_command){
            {command->clip_rect.x, command->clip_rect.y, command->clip_rect.w, command->clip_rect.h},
            first,
            command->elem_count,
            (uint32_t)command->texture.id};
        first += command->elem_count;
    }
    gui->draw_count = index;
    af_overlay_frame frame = {.vertices = nk_buffer_memory_const(&gui->vertices),
                              .indices = nk_buffer_memory_const(&gui->indices),
                              .commands = gui->draws,
                              .vertex_count = (uint32_t)(gui->vertices.allocated / sizeof(af_overlay_vertex)),
                              .index_count = (uint32_t)(gui->indices.allocated / sizeof(uint32_t)),
                              .command_count = index,
                              .width = width,
                              .height = height};
    bool result = af_engine_set_overlay(engine, &frame);
    nk_clear(&gui->context);
    return result;
}

void ed_gui_unfocus(ed_gui *gui) {
    struct nk_window *window = nk_window_find(&gui->context, "Inspector");
    if (window) {
        window->edit.active = nk_false;
        window->edit.name = 0;
        window->property.active = 0;
        window->property.state = 0;
    }
}

bool ed_gui_set_text_input(ed_gui *gui, af_engine *engine, bool enabled) {
    if (gui->text_input == enabled)
        return true;
    if (!af_engine_set_text_input(engine, enabled))
        return false;
    gui->text_input = enabled;
    return true;
}
