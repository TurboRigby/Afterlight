/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "viewport_overlays.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void draw_colliders(editor *app) {
    if (!app->show_colliders || app->blueprint_mode)
        return;
    struct nk_context *ctx = &app->gui.context;
    af_rect r = app->viewport;
    nk_style_push_style_item(ctx, &ctx->style.window.fixed_background, nk_style_item_color(nk_rgba(0, 0, 0, 0)));
    if (nk_begin(ctx, "Collision shapes", nk_rect(r.x, r.y, r.width, r.height),
                 NK_WINDOW_NO_INPUT | NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_BACKGROUND)) {
        struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);
        const af_scene *scene = app->runtime.playing ? app->runtime.scene : app->document.scene;
        for (size_t i = 0; i < af_scene_object_count(scene); ++i)
            for (size_t part = 0; part < af_scene_collider_shape_count(scene, af_scene_object_at(scene, i)->id);
                 ++part) {
                af_world_collider collider;
                if (!af_scene_collider_shape_at(scene, af_scene_object_at(scene, i)->id, part, &collider))
                    continue;
                ed_line3 lines[ED_COLLIDER_MAX_LINES];
                size_t count = ed_collider_lines(collider, lines, ED_COLLIDER_MAX_LINES);
                struct nk_color color = collider.object == app->document.selection ? nk_rgb(255, 211, 115)
                                        : collider.trigger                         ? nk_rgb(195, 135, 255)
                                                                                   : nk_rgb(92, 225, 204);
                for (size_t j = 0; j < count && j < ED_COLLIDER_MAX_LINES; ++j) {
                    af_vec2 a, b;
                    if (ed_project(&app->camera, r, lines[j].a, &a) && ed_project(&app->camera, r, lines[j].b, &b))
                        nk_stroke_line(canvas, a.x, a.y, b.x, b.y, 1.5f, color);
                }
            }
    }
    nk_end(ctx);
    nk_style_pop_style_item(ctx);
}

void draw_skeleton(editor *app) {
    if (!app->show_skeleton || !app->animation_target || app->blueprint_mode)
        return;
    struct nk_context *ctx = &app->gui.context;
    af_rect r = app->viewport;
    af_camera camera = app->runtime.playing ? af_engine_get_camera(app->engine) : app->camera;
    af_animation_bone bones[256];
    size_t count = af_engine_animation_bones(app->engine, app->animation_target, bones, 256);
    nk_style_push_style_item(ctx, &ctx->style.window.fixed_background, nk_style_item_color(nk_rgba(0, 0, 0, 0)));
    if (nk_begin(ctx, "Posed skeleton", nk_rect(r.x, r.y, r.width, r.height),
                 NK_WINDOW_NO_INPUT | NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_BACKGROUND)) {
        struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);
        for (size_t i = 0; i < count && i < 256; ++i) {
            af_vec2 a, b;
            if (!ed_project(&camera, r, bones[i].position, &a))
                continue;
            if (bones[i].has_parent && ed_project(&camera, r, bones[i].parent_position, &b))
                nk_stroke_line(canvas, a.x, a.y, b.x, b.y, 2, nk_rgb(255, 194, 84));
            nk_fill_circle(canvas, nk_rect(a.x - 3, a.y - 3, 6, 6), nk_rgb(130, 240, 235));
        }
    }
    nk_end(ctx);
    nk_style_pop_style_item(ctx);
}

void draw_physics(editor *app) {
    if (!app->show_physics || app->blueprint_mode)
        return;
    struct nk_context *ctx = &app->gui.context;
    af_rect r = app->viewport;
    af_camera camera = app->runtime.playing ? af_engine_get_camera(app->engine) : app->camera;
    const af_scene *scene = app->runtime.playing ? app->runtime.scene : app->document.scene;
    nk_style_push_style_item(ctx, &ctx->style.window.fixed_background, nk_style_item_color(nk_rgba(0, 0, 0, 0)));
    if (nk_begin(ctx, "Physics debug", nk_rect(r.x, r.y, r.width, r.height),
                 NK_WINDOW_NO_INPUT | NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_BACKGROUND)) {
        struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);
        for (size_t i = 0; i < af_scene_object_count(scene); ++i) {
            const af_scene_object *o = af_scene_object_at(scene, i);
            if (!o->body.enabled || o->body.type != AF_BODY_DYNAMIC)
                continue;
            af_physics_state state = af_engine_body_state(app->engine, o->id);
            af_vec3 center = state.available ? state.position : o->transform.position;
            af_vec2 a, b;
            struct nk_color color = state.sleeping ? nk_rgb(110, 220, 230) : nk_rgb(255, 180, 80);
            if (ed_project(&camera, r, center, &a)) {
                nk_stroke_circle(canvas, nk_rect(a.x - 4, a.y - 4, 8, 8), 2, color);
                if (state.available &&
                    ed_project(&camera, r, af_vec3_add(center, af_vec3_scale(state.velocity, .2f)), &b))
                    nk_stroke_line(canvas, a.x, a.y, b.x, b.y, 2, color);
            }
        }

        for (size_t i = 0; i < af_scene_joint_count(scene); ++i) {
            af_joint j = *af_scene_joint_at(scene, i);
            ed_line3 lines[64];
            size_t count = ed_joint_lines(scene, j, lines, 64);
            af_physics_joint_state state = af_engine_joint_state(app->engine, j.id);
            struct nk_color color = !j.enabled                                     ? nk_rgb(110, 110, 110)
                                    : state.available && state.linear_error > .05f ? nk_rgb(255, 100, 100)
                                    : state.available && state.sleeping            ? nk_rgb(100, 210, 150)
                                                                                   : nk_rgb(154, 190, 255);
            for (size_t k = 0; k < count && k < 64; ++k) {
                af_vec2 a, b;
                if (ed_project(&camera, r, lines[k].a, &a) && ed_project(&camera, r, lines[k].b, &b))
                    nk_stroke_line(canvas, a.x, a.y, b.x, b.y, 2, color);
            }
        }
        af_physics_contact contacts[1024];
        size_t count = af_engine_physics_contacts(app->engine, contacts, 1024);
        for (size_t i = 0; i < count && i < 1024; ++i) {
            af_vec2 a, b;
            if (ed_project(&camera, r, contacts[i].point, &a)) {
                nk_fill_circle(canvas, nk_rect(a.x - 2, a.y - 2, 4, 4), nk_rgb(255, 110, 140));
                if (ed_project(&camera, r, af_vec3_add(contacts[i].point, af_vec3_scale(contacts[i].normal, .3f)), &b))
                    nk_stroke_line(canvas, a.x, a.y, b.x, b.y, 1, nk_rgb(255, 110, 140));
            }
        }
    }
    nk_end(ctx);
    nk_style_pop_style_item(ctx);
}

void draw_lights(editor *app) {
    if (app->runtime.playing || app->blueprint_mode)
        return;
    struct nk_context *ctx = &app->gui.context;
    af_rect r = app->viewport;
    nk_style_push_style_item(ctx, &ctx->style.window.fixed_background, nk_style_item_color(nk_rgba(0, 0, 0, 0)));
    if (nk_begin(ctx, "Light shapes", nk_rect(r.x, r.y, r.width, r.height),
                 NK_WINDOW_NO_INPUT | NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_BACKGROUND)) {
        struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);
        for (size_t i = 0; i < af_scene_local_light_count(app->document.scene); ++i) {
            const af_local_light *light = af_scene_local_light_at(app->document.scene, i);
            af_vec2 screen;
            struct nk_color color = !light->enabled                                   ? nk_rgb(120, 120, 130)
                                    : app->light_selected && i == app->selected_light ? nk_rgb(255, 218, 120)
                                                                                      : nk_rgb(240, 170, 75);
            if (ed_project(&app->camera, r, light->position, &screen)) {
                nk_stroke_circle(canvas, nk_rect(screen.x - 7, screen.y - 7, 14, 14), 2, color);
                nk_stroke_line(canvas, screen.x - 11, screen.y, screen.x + 11, screen.y, 1, color);
                nk_stroke_line(canvas, screen.x, screen.y - 11, screen.x, screen.y + 11, 1, color);
            }
            if (app->light_selected && i == app->selected_light) {
                ed_line3 lines[ED_LIGHT_MAX_LINES];
                size_t count = ed_light_lines(*light, lines, ED_LIGHT_MAX_LINES);
                for (size_t j = 0; j < count && j < ED_LIGHT_MAX_LINES; ++j) {
                    af_vec2 a, b;
                    if (ed_project(&app->camera, r, lines[j].a, &a) && ed_project(&app->camera, r, lines[j].b, &b))
                        nk_stroke_line(canvas, a.x, a.y, b.x, b.y, 1, color);
                }
            }
        }
    }
    nk_end(ctx);
    nk_style_pop_style_item(ctx);
}

void draw_gizmo(editor *app) {
    ed_gizmo *g = &app->gizmo;
    if (!g->projected || app->runtime.playing || app->blueprint_mode)
        return;
    struct nk_context *ctx = &app->gui.context;
    nk_style_push_style_item(ctx, &ctx->style.window.fixed_background, nk_style_item_color(nk_rgba(0, 0, 0, 0)));
    af_rect r = app->viewport;
    if (nk_begin(ctx, "Gizmos", nk_rect(r.x, r.y, r.width, r.height),
                 NK_WINDOW_NO_INPUT | NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_BACKGROUND)) {
        struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);
        struct nk_color colors[] = {nk_rgb(240, 95, 91), nk_rgb(107, 211, 138), nk_rgb(104, 161, 255)};
        for (int a = 0; a < 3; ++a) {
            struct nk_color c = g->dragging && a == g->axis ? nk_rgb(255, 220, 125) : colors[a];
            if (g->mode == ED_GIZMO_ROTATE) {
                for (int i = 1; i <= ED_GIZMO_SEGMENTS; ++i)
                    nk_stroke_line(canvas, g->rings[a][i - 1].x, g->rings[a][i - 1].y, g->rings[a][i].x,
                                   g->rings[a][i].y, 2, c);
            } else {
                nk_stroke_line(canvas, g->origin.x, g->origin.y, g->ends[a].x, g->ends[a].y, 3, c);
                struct nk_rect end = nk_rect(g->ends[a].x - 5, g->ends[a].y - 5, 10, 10);
                if (g->mode == ED_GIZMO_SCALE)
                    nk_fill_rect(canvas, end, 1, c);
                else
                    nk_fill_circle(canvas, end, c);
            }
        }
        nk_fill_circle(canvas, nk_rect(g->origin.x - 4, g->origin.y - 4, 8, 8), nk_rgb(245, 225, 180));
    }
    nk_end(ctx);
    nk_style_pop_style_item(ctx);
}
