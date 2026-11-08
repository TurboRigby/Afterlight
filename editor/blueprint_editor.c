/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "blueprint_editor.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void ed_blueprint_state_init(ed_blueprint_state *state) {
    if (!state)
        return;
    memset(state, 0, sizeof(*state));
    state->pan_x = 60.0f;
    state->pan_y = 60.0f;
    state->zoom = 1.0f;
    snprintf(state->new_graph_name, sizeof(state->new_graph_name), "GameplayScript");
    snprintf(state->new_var_name, sizeof(state->new_var_name), "Speed");
    state->new_var_float = 5.0f;
    state->initialized = true;
}

static const char *pin_type_to_str(af_bp_pin_type type) {
    switch (type) {
    case AF_BP_PIN_EXEC:
        return "Exec";
    case AF_BP_PIN_FLOAT:
        return "Float";
    case AF_BP_PIN_BOOL:
        return "Bool";
    case AF_BP_PIN_VEC3:
        return "Vec3";
    case AF_BP_PIN_OBJECT:
        return "Object";
    case AF_BP_PIN_STRING:
        return "String";
    default:
        return "Unknown";
    }
}

static struct nk_color pin_color(af_bp_pin_type type) {
    switch (type) {
    case AF_BP_PIN_EXEC:
        return nk_rgb(255, 255, 255);
    case AF_BP_PIN_FLOAT:
        return nk_rgb(80, 210, 255);
    case AF_BP_PIN_BOOL:
        return nk_rgb(90, 220, 100);
    case AF_BP_PIN_VEC3:
        return nk_rgb(255, 215, 60);
    case AF_BP_PIN_OBJECT:
        return nk_rgb(180, 130, 255);
    case AF_BP_PIN_STRING:
        return nk_rgb(255, 155, 75);
    default:
        return nk_rgb(200, 200, 200);
    }
}

static struct nk_color node_header_color(af_bp_node_type type) {
    if (type <= AF_BP_NODE_EVENT_CUSTOM)
        return nk_rgb(180, 45, 45); /* Crimson for Events */
    if (type <= AF_BP_NODE_FOR_LOOP)
        return nk_rgb(120, 45, 160); /* Violet for Flow Control */
    if (type <= AF_BP_NODE_RANDOM_FLOAT)
        return nk_rgb(40, 135, 75); /* Forest Green for Math */
    if (type <= AF_BP_NODE_SET_VAR)
        return nk_rgb(50, 120, 130); /* Slate/Teal for Variables */
    return nk_rgb(45, 95, 180);      /* Royal Blue for Actions */
}

static void draw_pin_icon(struct nk_command_buffer *canvas, float cx, float cy, float radius, af_bp_pin_type type,
                          bool connected) {
    struct nk_color col = pin_color(type);
    if (type == AF_BP_PIN_EXEC) {
        /* Arrow shape for Exec pin */
        float s = radius * 1.2f;
        if (connected) {
            nk_fill_polygon(canvas, (float[]){cx - s * 0.5f, cy - s, cx + s * 0.8f, cy, cx - s * 0.5f, cy + s}, 3, col);
        } else {
            nk_stroke_polygon(canvas, (float[]){cx - s * 0.5f, cy - s, cx + s * 0.8f, cy, cx - s * 0.5f, cy + s}, 3,
                              2.0f, col);
        }
    } else {
        /* Circle icon for data pin */
        struct nk_rect r = nk_rect(cx - radius, cy - radius, radius * 2.0f, radius * 2.0f);
        if (connected) {
            nk_fill_circle(canvas, r, col);
        } else {
            nk_stroke_circle(canvas, r, 2.0f, col);
        }
    }
}

static void get_pin_pos(const af_bp_node *node, uint32_t pin_id, float pan_x, float pan_y, float zoom, float base_x,
                        float base_y, float *out_x, float *out_y) {
    float nx = base_x + (node->x + pan_x) * zoom;
    float ny = base_y + (node->y + pan_y) * zoom;
    float nw = node->width * zoom;

    uint32_t in_idx = 0;
    uint32_t out_idx = 0;
    for (uint32_t i = 0; i < node->pin_count; ++i) {
        const af_bp_pin *p = &node->pins[i];
        if (p->dir == AF_BP_PIN_INPUT) {
            if (p->id == pin_id) {
                *out_x = nx + 10.0f * zoom;
                *out_y = ny + (36.0f + (float)in_idx * 22.0f) * zoom;
                return;
            }
            in_idx++;
        } else {
            if (p->id == pin_id) {
                *out_x = nx + nw - 10.0f * zoom;
                *out_y = ny + (36.0f + (float)out_idx * 22.0f) * zoom;
                return;
            }
            out_idx++;
        }
    }
    *out_x = nx;
    *out_y = ny;
}

void ed_blueprint_sidebar_panel(ed_blueprint_state *state, struct nk_context *ctx, af_scene *scene, float height) {
    if (!state || !ctx || !scene)
        return;

    size_t bp_count = af_scene_blueprint_count(scene);
    nk_layout_row_dynamic(ctx, 24, 1);
    nk_label(ctx, "Blueprint Scripting", NK_TEXT_LEFT);

    /* Graph Selector */
    nk_layout_row_dynamic(ctx, 26, 1);
    if (bp_count == 0) {
        nk_label(ctx, "No Blueprints in scene.", NK_TEXT_LEFT);
        nk_layout_row_dynamic(ctx, 28, 1);
        nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, state->new_graph_name, sizeof(state->new_graph_name),
                                       nk_filter_default);
        if (nk_button_label(ctx, "+ Create Graph")) {
            af_blueprint *bp = af_scene_add_blueprint(scene, state->new_graph_name);
            if (bp) {
                /* Add a default BeginPlay and Tick */
                af_blueprint_add_node(bp, AF_BP_NODE_EVENT_START, 50, 50);
                af_blueprint_add_node(bp, AF_BP_NODE_EVENT_TICK, 50, 180);
                state->active_graph_index = 0;
            }
        }
        return;
    }

    if (state->active_graph_index >= bp_count)
        state->active_graph_index = 0;

    af_blueprint *active_bp = af_scene_get_blueprint(scene, state->active_graph_index);
    if (!active_bp)
        return;

    if (nk_combo_begin_label(ctx, active_bp->name, nk_vec2(220, 200))) {
        nk_layout_row_dynamic(ctx, 24, 1);
        for (size_t i = 0; i < bp_count; ++i) {
            const af_blueprint *b = af_scene_get_blueprint_const(scene, i);
            if (nk_combo_item_label(ctx, b->name, NK_TEXT_LEFT)) {
                state->active_graph_index = i;
            }
        }
        nk_end(ctx);
    }

    /* Graph Settings */
    nk_layout_row_dynamic(ctx, 24, 2);
    nk_bool enabled = active_bp->enabled;
    if (nk_checkbox_label(ctx, "Enabled", &enabled)) {
        active_bp->enabled = enabled != 0;
    }
    if (nk_button_label(ctx, "+ New")) {
        char gname[64];
        snprintf(gname, sizeof(gname), "Graph_%zu", bp_count + 1);
        af_blueprint *nb = af_scene_add_blueprint(scene, gname);
        if (nb) {
            af_blueprint_add_node(nb, AF_BP_NODE_EVENT_START, 50, 50);
            af_blueprint_add_node(nb, AF_BP_NODE_EVENT_TICK, 50, 180);
            state->active_graph_index = bp_count;
        }
    }

    /* Bound Object / Actor dropdown */
    nk_layout_row_dynamic(ctx, 24, 1);
    nk_label(ctx, "Target Actor:", NK_TEXT_LEFT);
    const af_scene_object *target_obj = af_scene_get_object(scene, active_bp->bound_object);
    const char *target_name = target_obj ? target_obj->name : "[ Scene Global ]";
    if (nk_combo_begin_label(ctx, target_name, nk_vec2(220, 220))) {
        nk_layout_row_dynamic(ctx, 24, 1);
        if (nk_combo_item_label(ctx, "[ Scene Global ]", NK_TEXT_LEFT)) {
            active_bp->bound_object = 0;
        }
        for (size_t i = 0; i < af_scene_object_count(scene); ++i) {
            const af_scene_object *o = af_scene_object_at(scene, i);
            if (nk_combo_item_label(ctx, o->name, NK_TEXT_LEFT)) {
                active_bp->bound_object = o->id;
            }
        }
        nk_end(ctx);
    }

    /* Sub-panels inside scroll group */
    nk_layout_row_dynamic(ctx, fmaxf(100.0f, height - 260.0f), 1);
    if (nk_group_begin(ctx, "BP Controls", NK_WINDOW_NO_SCROLLBAR)) {
        /* Variables */
        nk_layout_row_dynamic(ctx, 22, 1);
        nk_label(ctx, "--- Variables ---", NK_TEXT_LEFT);
        for (size_t i = 0; i < active_bp->var_count; ++i) {
            af_bp_variable *v = &active_bp->variables[i];
            nk_layout_row_dynamic(ctx, 24, 2);
            nk_label(ctx, v->name, NK_TEXT_LEFT);
            if (v->type == AF_BP_PIN_FLOAT) {
                nk_property_float(ctx, "", -1000.0f, &v->val.f, 1000.0f, 0.5f, 0.1f);
            } else if (v->type == AF_BP_PIN_BOOL) {
                nk_bool b = v->val.b;
                if (nk_checkbox_label(ctx, "", &b))
                    v->val.b = b != 0;
            } else {
                nk_label(ctx, pin_type_to_str(v->type), NK_TEXT_RIGHT);
            }
        }

        nk_layout_row_dynamic(ctx, 24, 2);
        nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, state->new_var_name, sizeof(state->new_var_name),
                                       nk_filter_default);
        if (nk_button_label(ctx, "+ Add Var")) {
            af_bp_value val = {.type = AF_BP_PIN_FLOAT, .f = state->new_var_float};
            af_blueprint_add_variable(active_bp, state->new_var_name, AF_BP_PIN_FLOAT, val);
        }

        /* Node Palette */
        nk_layout_row_dynamic(ctx, 22, 1);
        nk_label(ctx, "--- Add Nodes ---", NK_TEXT_LEFT);

        nk_layout_row_dynamic(ctx, 22, 1);
        nk_label(ctx, "Events:", NK_TEXT_LEFT);
        nk_layout_row_dynamic(ctx, 24, 2);
        if (nk_button_label(ctx, "+ BeginPlay"))
            af_blueprint_add_node(active_bp, AF_BP_NODE_EVENT_START, -state->pan_x + 100, -state->pan_y + 100);
        if (nk_button_label(ctx, "+ Tick"))
            af_blueprint_add_node(active_bp, AF_BP_NODE_EVENT_TICK, -state->pan_x + 100, -state->pan_y + 160);
        if (nk_button_label(ctx, "+ Key Input"))
            af_blueprint_add_node(active_bp, AF_BP_NODE_EVENT_KEY, -state->pan_x + 100, -state->pan_y + 220);
        if (nk_button_label(ctx, "+ On Hit"))
            af_blueprint_add_node(active_bp, AF_BP_NODE_EVENT_COLLISION, -state->pan_x + 100, -state->pan_y + 280);

        nk_layout_row_dynamic(ctx, 22, 1);
        nk_label(ctx, "Actions:", NK_TEXT_LEFT);
        nk_layout_row_dynamic(ctx, 24, 2);
        if (nk_button_label(ctx, "+ Set Location"))
            af_blueprint_add_node(active_bp, AF_BP_NODE_SET_POSITION, -state->pan_x + 300, -state->pan_y + 100);
        if (nk_button_label(ctx, "+ Add Offset"))
            af_blueprint_add_node(active_bp, AF_BP_NODE_TRANSLATE, -state->pan_x + 300, -state->pan_y + 160);
        if (nk_button_label(ctx, "+ Rotate"))
            af_blueprint_add_node(active_bp, AF_BP_NODE_ROTATE, -state->pan_x + 300, -state->pan_y + 220);
        if (nk_button_label(ctx, "+ Set Color"))
            af_blueprint_add_node(active_bp, AF_BP_NODE_SET_COLOR, -state->pan_x + 300, -state->pan_y + 280);
        if (nk_button_label(ctx, "+ Add Force"))
            af_blueprint_add_node(active_bp, AF_BP_NODE_ADD_FORCE, -state->pan_x + 300, -state->pan_y + 340);
        if (nk_button_label(ctx, "+ Print"))
            af_blueprint_add_node(active_bp, AF_BP_NODE_PRINT, -state->pan_x + 300, -state->pan_y + 400);

        nk_layout_row_dynamic(ctx, 22, 1);
        nk_label(ctx, "Flow Control:", NK_TEXT_LEFT);
        nk_layout_row_dynamic(ctx, 24, 2);
        if (nk_button_label(ctx, "+ Branch"))
            af_blueprint_add_node(active_bp, AF_BP_NODE_BRANCH, -state->pan_x + 250, -state->pan_y + 100);
        if (nk_button_label(ctx, "+ Sequence"))
            af_blueprint_add_node(active_bp, AF_BP_NODE_SEQUENCE, -state->pan_x + 250, -state->pan_y + 160);
        if (nk_button_label(ctx, "+ Gate"))
            af_blueprint_add_node(active_bp, AF_BP_NODE_GATE, -state->pan_x + 250, -state->pan_y + 220);
        if (nk_button_label(ctx, "+ Delay"))
            af_blueprint_add_node(active_bp, AF_BP_NODE_DELAY, -state->pan_x + 250, -state->pan_y + 280);

        nk_layout_row_dynamic(ctx, 22, 1);
        nk_label(ctx, "Math & Logic:", NK_TEXT_LEFT);
        nk_layout_row_dynamic(ctx, 24, 2);
        if (nk_button_label(ctx, "+ Add"))
            af_blueprint_add_node(active_bp, AF_BP_NODE_ADD, -state->pan_x + 200, -state->pan_y + 100);
        if (nk_button_label(ctx, "+ Multiply"))
            af_blueprint_add_node(active_bp, AF_BP_NODE_MUL, -state->pan_x + 200, -state->pan_y + 160);
        if (nk_button_label(ctx, "+ Lerp"))
            af_blueprint_add_node(active_bp, AF_BP_NODE_LERP, -state->pan_x + 200, -state->pan_y + 220);
        if (nk_button_label(ctx, "+ Sin"))
            af_blueprint_add_node(active_bp, AF_BP_NODE_SIN, -state->pan_x + 200, -state->pan_y + 280);
        if (nk_button_label(ctx, "+ Make Vector"))
            af_blueprint_add_node(active_bp, AF_BP_NODE_VEC3_MAKE, -state->pan_x + 200, -state->pan_y + 340);
        if (nk_button_label(ctx, "+ Equal (==)"))
            af_blueprint_add_node(active_bp, AF_BP_NODE_COMPARE_EQUAL, -state->pan_x + 200, -state->pan_y + 400);

        nk_layout_row_dynamic(ctx, 22, 1);
        nk_label(ctx, "Actions:", NK_TEXT_LEFT);
        nk_layout_row_dynamic(ctx, 26, 1);
        if (state->selected_node_id && nk_button_label(ctx, "Delete Selected Node")) {
            af_blueprint_remove_node(active_bp, state->selected_node_id);
            state->selected_node_id = 0;
        }
        if (bp_count > 1 && nk_button_label(ctx, "Delete Active Graph")) {
            af_scene_remove_blueprint(scene, state->active_graph_index);
            state->active_graph_index = 0;
        }

        nk_group_end(ctx);
    }
}

void ed_blueprint_canvas_panel(ed_blueprint_state *state, struct nk_context *ctx, af_scene *scene,
                               af_rect viewport_rect, bool is_playing) {
    if (!state || !ctx || !scene)
        return;

    size_t bp_count = af_scene_blueprint_count(scene);
    if (bp_count == 0)
        return;

    if (state->active_graph_index >= bp_count)
        state->active_graph_index = 0;

    af_blueprint *bp = af_scene_get_blueprint(scene, state->active_graph_index);
    if (!bp)
        return;

    struct nk_rect bounds = nk_rect(viewport_rect.x, viewport_rect.y, viewport_rect.width, viewport_rect.height);

    nk_style_push_style_item(ctx, &ctx->style.window.fixed_background, nk_style_item_color(nk_rgba(24, 26, 32, 255)));

    if (!nk_begin(ctx, "Blueprint Canvas", bounds,
                  NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_BACKGROUND | NK_WINDOW_NO_INPUT)) {
        nk_style_pop_style_item(ctx);
        return;
    }

    struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);
    float base_x = bounds.x;
    float base_y = bounds.y;
    float zoom = state->zoom > 0.1f ? state->zoom : 1.0f;
    float pan_x = state->pan_x;
    float pan_y = state->pan_y;

    /* Mouse input & interaction */
    float mx = ctx->input.mouse.pos.x;
    float my = ctx->input.mouse.pos.y;
    bool in_viewport = (mx >= bounds.x && mx <= bounds.x + bounds.w && my >= bounds.y && my <= bounds.y + bounds.h);
    bool lmb_down = ctx->input.mouse.buttons[NK_BUTTON_LEFT].down != 0;
    bool lmb_click = ctx->input.mouse.buttons[NK_BUTTON_LEFT].clicked != 0;
    bool rmb_down = ctx->input.mouse.buttons[NK_BUTTON_RIGHT].down != 0;
    bool mmb_down = ctx->input.mouse.buttons[NK_BUTTON_MIDDLE].down != 0;
    float dx = ctx->input.mouse.delta.x;
    float dy = ctx->input.mouse.delta.y;

    /* 1. Canvas Panning */
    if (in_viewport && (rmb_down || mmb_down)) {
        state->pan_x += dx / zoom;
        state->pan_y += dy / zoom;
    }

    /* 2. Grid lines */
    float grid_step = 40.0f * zoom;
    float ox = fmodf((base_x + pan_x * zoom), grid_step);
    float oy = fmodf((base_y + pan_y * zoom), grid_step);
    struct nk_color grid_col = nk_rgba(35, 38, 48, 255);
    for (float x = ox; x < bounds.w; x += grid_step) {
        nk_stroke_line(canvas, base_x + x, base_y, base_x + x, base_y + bounds.h, 1.0f, grid_col);
    }
    for (float y = oy; y < bounds.h; y += grid_step) {
        nk_stroke_line(canvas, base_x, base_y + y, base_x + bounds.w, base_y + y, 1.0f, grid_col);
    }

    /* 3. Render Links / Wires */
    for (size_t i = 0; i < bp->link_count; ++i) {
        const af_bp_link *l = &bp->links[i];
        const af_bp_node *fn = af_blueprint_get_node_const(bp, l->from_node_id);
        const af_bp_node *tn = af_blueprint_get_node_const(bp, l->to_node_id);
        if (!fn || !tn)
            continue;

        float p1x, p1y, p2x, p2y;
        get_pin_pos(fn, l->from_pin_id, pan_x, pan_y, zoom, base_x, base_y, &p1x, &p1y);
        get_pin_pos(tn, l->to_pin_id, pan_x, pan_y, zoom, base_x, base_y, &p2x, &p2y);

        /* Color based on pin type */
        af_bp_pin_type ptype = AF_BP_PIN_EXEC;
        for (uint32_t p = 0; p < fn->pin_count; ++p) {
            if (fn->pins[p].id == l->from_pin_id) {
                ptype = fn->pins[p].type;
                break;
            }
        }
        struct nk_color wire_col = pin_color(ptype);
        float wire_thickness = (ptype == AF_BP_PIN_EXEC) ? 3.0f * zoom : 2.0f * zoom;

        /* If playing and node was recently executed, flash the wire! */
        if (is_playing && bp->last_executed_node_id == fn->id) {
            wire_col = nk_rgb(255, 230, 80);
            wire_thickness += 2.0f * zoom;
        }

        float cdist = fmaxf(40.0f * zoom, fabsf(p2x - p1x) * 0.5f);
        nk_stroke_curve(canvas, p1x, p1y, p1x + cdist, p1y, p2x - cdist, p2y, p2x, p2y, wire_thickness, wire_col);
    }

    /* 4. Live Wire dragging */
    if (state->dragging_wire) {
        const af_bp_node *fn = af_blueprint_get_node_const(bp, state->wire_from_node);
        if (fn) {
            float p1x, p1y;
            get_pin_pos(fn, state->wire_from_pin, pan_x, pan_y, zoom, base_x, base_y, &p1x, &p1y);
            float cdist = fmaxf(40.0f * zoom, fabsf(mx - p1x) * 0.5f);
            nk_stroke_curve(canvas, p1x, p1y, p1x + cdist, p1y, mx - cdist, my, mx, my, 2.5f * zoom,
                            nk_rgb(255, 220, 100));
        }
        if (!lmb_down) {
            /* Try to connect to pin under mouse */
            for (size_t n = 0; n < bp->node_count; ++n) {
                af_bp_node *tn = &bp->nodes[n];
                for (uint32_t p = 0; p < tn->pin_count; ++p) {
                    if (tn->pins[p].dir == AF_BP_PIN_INPUT) {
                        float px, py;
                        get_pin_pos(tn, tn->pins[p].id, pan_x, pan_y, zoom, base_x, base_y, &px, &py);
                        if (fabsf(mx - px) < 14.0f * zoom && fabsf(my - py) < 14.0f * zoom) {
                            af_blueprint_add_link(bp, state->wire_from_node, state->wire_from_pin, tn->id,
                                                  tn->pins[p].id);
                            break;
                        }
                    }
                }
            }
            state->dragging_wire = false;
        }
    }

    /* 5. Render Nodes */
    for (size_t i = 0; i < bp->node_count; ++i) {
        af_bp_node *n = &bp->nodes[i];
        float nx = base_x + (n->x + pan_x) * zoom;
        float ny = base_y + (n->y + pan_y) * zoom;
        float nw = n->width * zoom;

        /* Calculate height dynamically based on pins */
        uint32_t in_count = 0, out_count = 0;
        for (uint32_t p = 0; p < n->pin_count; ++p) {
            if (n->pins[p].dir == AF_BP_PIN_INPUT)
                in_count++;
            else
                out_count++;
        }
        uint32_t max_rows = in_count > out_count ? in_count : out_count;
        if (max_rows < 2)
            max_rows = 2;
        float nh = (34.0f + (float)max_rows * 22.0f + 10.0f) * zoom;
        n->height = nh / zoom;

        struct nk_rect node_rect = nk_rect(nx, ny, nw, nh);
        struct nk_rect header_rect = nk_rect(nx, ny, nw, 28.0f * zoom);

        /* Check node dragging */
        if (in_viewport && lmb_click && mx >= nx && mx <= nx + nw && my >= ny && my <= ny + 28.0f * zoom) {
            state->selected_node_id = n->id;
            state->dragging_node = true;
            state->drag_node_id = n->id;
        }

        if (state->dragging_node && state->drag_node_id == n->id) {
            if (lmb_down) {
                n->x += dx / zoom;
                n->y += dy / zoom;
            } else {
                state->dragging_node = false;
            }
        }

        /* Node Body */
        struct nk_color body_bg = nk_rgba(30, 32, 40, 245);
        nk_fill_rect(canvas, node_rect, 6.0f * zoom, body_bg);

        /* Selection outline */
        if (state->selected_node_id == n->id) {
            nk_stroke_rect(canvas, node_rect, 6.0f * zoom, 2.0f, nk_rgb(255, 210, 60));
        } else {
            nk_stroke_rect(canvas, node_rect, 6.0f * zoom, 1.0f, nk_rgba(60, 64, 78, 200));
        }

        /* Node Header */
        struct nk_color h_color = node_header_color(n->type);
        nk_fill_rect(canvas, header_rect, 6.0f * zoom, h_color);
        /* Flat bottom for header */
        nk_fill_rect(canvas, nk_rect(nx, ny + 16.0f * zoom, nw, 12.0f * zoom), 0, h_color);

        /* Node Title text */
        struct nk_rect title_rect = nk_rect(nx + 8.0f * zoom, ny + 4.0f * zoom, nw - 16.0f * zoom, 20.0f * zoom);
        nk_draw_text(canvas, title_rect, n->title, (int)strlen(n->title), ctx->style.font, h_color,
                     nk_rgb(255, 255, 255));

        /* Render Pins */
        for (uint32_t p = 0; p < n->pin_count; ++p) {
            af_bp_pin *pin = &n->pins[p];
            bool is_connected =
                (pin->dir == AF_BP_PIN_INPUT) ? (af_blueprint_find_link_to_pin(bp, n->id, pin->id) != NULL) : false;
            if (pin->dir == AF_BP_PIN_OUTPUT) {
                for (size_t l = 0; l < bp->link_count; ++l) {
                    if (bp->links[l].from_node_id == n->id && bp->links[l].from_pin_id == pin->id) {
                        is_connected = true;
                        break;
                    }
                }
            }

            float px, py;
            get_pin_pos(n, pin->id, pan_x, pan_y, zoom, base_x, base_y, &px, &py);
            draw_pin_icon(canvas, px, py, 5.0f * zoom, pin->type, is_connected);

            /* Check pin drag to wire */
            if (in_viewport && lmb_click && fabsf(mx - px) < 10.0f * zoom && fabsf(my - py) < 10.0f * zoom) {
                if (pin->dir == AF_BP_PIN_OUTPUT) {
                    state->dragging_wire = true;
                    state->wire_from_node = n->id;
                    state->wire_from_pin = pin->id;
                }
            }

            /* Pin labels */
            if (pin->dir == AF_BP_PIN_INPUT) {
                struct nk_rect plbl =
                    nk_rect(px + 10.0f * zoom, py - 8.0f * zoom, nw * 0.5f - 14.0f * zoom, 16.0f * zoom);
                nk_draw_text(canvas, plbl, pin->name, (int)strlen(pin->name), ctx->style.font, body_bg,
                             nk_rgb(200, 205, 215));
            } else {
                float txt_w = (float)strlen(pin->name) * 7.0f * zoom;
                struct nk_rect plbl = nk_rect(px - 10.0f * zoom - txt_w, py - 8.0f * zoom, txt_w, 16.0f * zoom);
                nk_draw_text(canvas, plbl, pin->name, (int)strlen(pin->name), ctx->style.font, body_bg,
                             nk_rgb(200, 205, 215));
            }
        }
    }

    nk_end(ctx);
    nk_style_pop_style_item(ctx);
}
