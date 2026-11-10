/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef ED_BLUEPRINT_EDITOR_H
#define ED_BLUEPRINT_EDITOR_H

#include <afterlight/afterlight.h>
#include <afterlight/blueprint.h>
#include "gui.h"

typedef struct ed_blueprint_state {
    size_t active_graph_index;
    bool (*record_edit)(void *);
    void *edit_context;
    float pan_x, pan_y;
    float zoom;
    uint32_t selected_node_id;

    /* Node dragging */
    bool dragging_node, drag_recorded;
    uint32_t drag_node_id;
    float drag_node_offset_x;
    float drag_node_offset_y;

    /* Wire dragging */
    bool dragging_wire;
    uint32_t wire_from_node;
    uint32_t wire_from_pin;
    float wire_mouse_x;
    float wire_mouse_y;

    /* Canvas panning */
    bool panning;
    float pan_start_mouse_x;
    float pan_start_mouse_y;
    float pan_start_pan_x;
    float pan_start_pan_y;

    /* Context menu / Quick add */
    bool palette_open;
    float palette_x;
    float palette_y;
    char search_filter[32];

    /* Variable creation */
    char new_var_name[32];
    int new_var_type;
    float new_var_float;

    /* Graph creation */
    char new_graph_name[32];
    bool initialized;
} ed_blueprint_state;

void ed_blueprint_state_init(ed_blueprint_state *state);
void ed_blueprint_sidebar_panel(ed_blueprint_state *state, struct nk_context *ctx, af_scene *scene, float height,
                                bool is_playing);
bool ed_blueprint_attach_interactable(ed_blueprint_state *, af_scene *, af_object_id);
void ed_blueprint_canvas_panel(ed_blueprint_state *state, struct nk_context *ctx, af_scene *scene,
                               af_rect viewport_rect, bool is_playing);

#endif /* ED_BLUEPRINT_EDITOR_H */
