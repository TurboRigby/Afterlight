/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "smoke_tests.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void smoke_click(editor *app, float x, float y) {
    SDL_Event event;
    SDL_zero(event);
    event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    event.button.windowID = SDL_GetWindowID(af_engine_sdl_window(app->engine));
    event.button.button = SDL_BUTTON_LEFT;
    event.button.down = true;
    event.button.x = x;
    event.button.y = y;
    app->failed |= !SDL_PushEvent(&event);
    event.type = SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.down = false;
    app->failed |= !SDL_PushEvent(&event);
}

void smoke_button(editor *app, Uint8 button, bool down) {
    SDL_Event event;
    SDL_zero(event);
    event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.windowID = SDL_GetWindowID(af_engine_sdl_window(app->engine));
    event.button.button = button;
    event.button.down = down;
    event.button.x = 550;
    event.button.y = 350;
    app->failed |= !SDL_PushEvent(&event);
}

void smoke_key(editor *app, SDL_Scancode key) {
    SDL_Event event;
    SDL_zero(event);
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.windowID = SDL_GetWindowID(af_engine_sdl_window(app->engine));
    event.key.scancode = key;
    event.key.down = true;
    app->failed |= !SDL_PushEvent(&event);
    event.type = SDL_EVENT_KEY_UP;
    event.key.down = false;
    app->failed |= !SDL_PushEvent(&event);
}

#define SMOKE_CHECK(app, condition)                                                                                    \
    do {                                                                                                               \
        if (!(condition)) {                                                                                            \
            fprintf(stderr, "Editor smoke frame %u: %s\n", (app)->updates, #condition);                                \
            (app)->failed = true;                                                                                      \
        }                                                                                                              \
    } while (0)

void animation_tools_smoke(editor *app) {
    if (!app->animation_test_path || app->updates < 8)
        return;
    if (!(SDL_GetWindowFlags(af_engine_sdl_window(app->engine)) & SDL_WINDOW_INPUT_FOCUS) &&
        app->animation_test_step == 0) {
        SDL_RaiseWindow(af_engine_sdl_window(app->engine));
        return;
    }
    af_animation_status status = af_engine_animation_status(app->engine, app->animation_target);
    int click = -1;
    switch (app->animation_test_step++) {
    case 0:
        SMOKE_CHECK(app, nk_window_find(&app->gui.context, "Animation") && status.clip_count == 3 &&
                             !app->animation_preview);
        click = 0;
        break;
    case 1:
        SMOKE_CHECK(app, app->animation_preview);
        break;
    case 2:
        SMOKE_CHECK(app, status.time > 0 && status.playing);
        click = 1;
        break;
    case 3:
        SMOKE_CHECK(app, !status.playing);
        app->animation_test_time = status.time;
        break;
    case 4:
        SMOKE_CHECK(app, status.time == app->animation_test_time);
        click = 1;
        break;
    case 5:
        SMOKE_CHECK(app, status.playing);
        break;
    case 6:
        SMOKE_CHECK(app, status.time > app->animation_test_time);
        click = 2;
        break;
    case 7:
        SMOKE_CHECK(app, !app->animation_preview && status.time == 0);
        break;
    case 8:
        SMOKE_CHECK(app, app->document.revision == app->animation_test_revision &&
                             app->document.selection == app->animation_target);
        toggle_play(app);
        SMOKE_CHECK(app, app->runtime.playing && af_engine_animation_play(app->engine, app->animation_target, 0, 0));
        break;
    case 9:
        if (af_engine_animation_status(app->engine, app->animation_target).time == 0) {
            app->animation_test_step = 9;
            break;
        }
        stop_play(app);
        break;
    case 10:
        SMOKE_CHECK(app, !app->runtime.playing && app->document.revision == app->animation_test_revision &&
                             af_scene_get_object(app->document.scene, app->animation_target)->animation.clip == 0);
        puts("Editor animation: actual Preview/Pause/Resume/Stop clicks, clean history and Play isolation passed.");
        af_engine_request_quit(app->engine);
        return;
    default:
        break;
    }
    if (click >= 0) {
        af_rect b = app->animation_buttons[click];
        smoke_click(app, b.x + b.width * .5f, b.y + b.height * .5f);
    }
}

void light_tools_smoke(editor *app) {
    if (!app->light_test || app->updates < 8)
        return;
    uint64_t losses = af_engine_test_focus_losses(app->engine);
    bool focused = (SDL_GetWindowFlags(af_engine_sdl_window(app->engine)) & SDL_WINDOW_INPUT_FOCUS) != 0;
    if (app->light_test_step && (losses != app->light_test_focus_losses || !focused)) {
        app->light_test_step = app->light_test_focus = 0;
        app->light_dragging = app->gizmo.dragging = false;
        fputs("Native focus changed; restarting scripted light controls.\n", stderr);
    }
    if (!app->light_test_step) {
        app->light_test_focus_losses = losses;
        if (!focused) {
            app->light_test_focus = 0;
            SDL_RaiseWindow(af_engine_sdl_window(app->engine));
            return;
        }
        if (++app->light_test_focus < 8)
            return;
    }
    SDL_WindowID window = SDL_GetWindowID(af_engine_sdl_window(app->engine));
    switch (app->light_test_step++) {
    case 0: {
        SMOKE_CHECK(app, ed_document_new(&app->document));
        SMOKE_CHECK(app, ed_document_add_light(&app->document, AF_LIGHT_POINT));
        af_local_light light = *af_scene_local_light_at(app->document.scene, 0);
        light.position = (af_vec3){0};
        SMOKE_CHECK(app, ed_document_local_light(&app->document, 0, light));
        ed_document_end_edit(&app->document);
        app->light_selected = false;
        app->gizmo.mode = ED_GIZMO_MOVE;
        app->fields_active = false;
        app->show_assets = false;
        break;
    }
    case 1: {
        af_vec2 position;
        SMOKE_CHECK(app, ed_project(&app->camera, app->viewport, (af_vec3){0}, &position));
        smoke_click(app, position.x, position.y);
        break;
    }
    case 2:
        SMOKE_CHECK(app, app->light_selected && app->selected_light == 0);
        break;
    case 3:
    case 9: {
        af_vec2 p = {app->gizmo.origin.x + (app->gizmo.ends[0].x - app->gizmo.origin.x) * .7f,
                     app->gizmo.origin.y + (app->gizmo.ends[0].y - app->gizmo.origin.y) * .7f};
        SDL_Event event;
        SDL_zero(event);
        event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        event.button.windowID = window;
        event.button.button = SDL_BUTTON_LEFT;
        event.button.down = true;
        event.button.x = p.x;
        event.button.y = p.y;
        SMOKE_CHECK(app, SDL_PushEvent(&event));
        break;
    }
    case 4:
    case 10:
        SMOKE_CHECK(app, app->light_dragging && app->gizmo.dragging);
        break;
    case 5:
    case 11: {
        SDL_Event event;
        SDL_zero(event);
        event.type = SDL_EVENT_MOUSE_MOTION;
        event.motion.windowID = window;
        event.motion.x = app->gizmo.start.x + app->gizmo.direction.x * .5f;
        event.motion.y = app->gizmo.start.y + app->gizmo.direction.y * .5f;
        SMOKE_CHECK(app, SDL_PushEvent(&event));
        break;
    }
    case 6:
    case 13: {
        SDL_Event event;
        SDL_zero(event);
        event.type = SDL_EVENT_MOUSE_BUTTON_UP;
        event.button.windowID = window;
        event.button.button = SDL_BUTTON_LEFT;
        event.button.x = app->gizmo.start.x + app->gizmo.direction.x * .5f;
        event.button.y = app->gizmo.start.y + app->gizmo.direction.y * .5f;
        SMOKE_CHECK(app, SDL_PushEvent(&event));
        break;
    }
    case 7: {
        const af_local_light *light = af_scene_local_light_at(app->document.scene, 0);
        SMOKE_CHECK(app, light && light->position.x > 0 && !app->gizmo.dragging);
        SMOKE_CHECK(app, ed_document_undo(&app->document));
        SMOKE_CHECK(app, af_scene_local_light_at(app->document.scene, 0)->position.x == 0);
        SMOKE_CHECK(app, ed_document_redo(&app->document));
        app->light_test_position = af_scene_local_light_at(app->document.scene, 0)->position;
        break;
    }
    case 8:
        break;
    case 12:
        smoke_key(app, SDL_SCANCODE_ESCAPE);
        break;
    case 14:
        SMOKE_CHECK(app, !app->light_dragging && !app->gizmo.dragging);
        SMOKE_CHECK(app, af_vec3_length(af_vec3_sub(af_scene_local_light_at(app->document.scene, 0)->position,
                                                    app->light_test_position)) < .0001f);
        break;
    case 15:
        puts("Editor light tools: viewport pick, axis drag, grouped undo/redo and Escape cancellation passed.");
        af_engine_request_quit(app->engine);
        return;
    default:
        break;
    }
}

#define PLAY_CHECK(condition)                                                                                          \
    do {                                                                                                               \
        if (!(condition)) {                                                                                            \
            app->play_test_failure = #condition;                                                                       \
            app->play_test_failure_frame = app->updates;                                                               \
            app->play_test_failure_line = __LINE__;                                                                    \
            return;                                                                                                    \
        }                                                                                                              \
    } while (0)

void play_controls_smoke(editor *app) {
    if (!app->play_controls_test)
        return;
    uint64_t focus_losses = af_engine_test_focus_losses(app->engine);
    if (app->play_test_started && (focus_losses != app->play_test_focus_losses ||
                                   !(SDL_GetWindowFlags(af_engine_sdl_window(app->engine)) & SDL_WINDOW_INPUT_FOCUS))) {
        /* Native focus loss clears all held/injected buttons in the engine.
           Restart this sequence instead of treating OS focus as a game failure. */
        stop_play(app);
        app->play_test_started = app->play_test_focus = 0;
        app->play_test_focus_losses = focus_losses;
        app->play_test_failure = NULL;
        fputs("Native focus changed; restarting scripted Play controls.\n", stderr);
        return;
    }
    if (app->play_test_failure) {
        /* Cocoa may revoke capture before SDL delivers FOCUS_LOST. Confirm a
           failed snapshot after two event batches; the guard above restarts on
           focus loss, while a genuine stable-focus failure still fails. */
        if (app->updates - app->play_test_failure_frame < 2)
            return;
        fprintf(stderr, "Editor Play check %u: %s\n", app->play_test_failure_line, app->play_test_failure);
        app->failed = true;
        af_engine_request_quit(app->engine);
        return;
    }
    /* Play normally follows a foreground user click. Wait for that same native
       focus state before injecting events, rather than racing Cocoa startup. */
    if (!app->play_test_started) {
        app->play_test_focus_losses = focus_losses;
        if (SDL_GetWindowFlags(af_engine_sdl_window(app->engine)) & SDL_WINDOW_INPUT_FOCUS) {
            if (++app->play_test_focus >= 8)
                app->play_test_started = app->updates;
        } else {
            app->play_test_focus = 0;
            if (app->updates % 10 == 1)
                SDL_RaiseWindow(af_engine_sdl_window(app->engine));
        }
        return;
    }
    unsigned stage = app->updates - app->play_test_started;
    if (stage == 3) {
        toggle_play(app);
        smoke_button(app, SDL_BUTTON_RIGHT, true);
    }
    if (stage == 4) {
        PLAY_CHECK(af_engine_mouse_captured(app->engine) && fabsf(app->camera.position.z - 7.0f) < .01f);
    }
    if (stage == 5) {
        PLAY_CHECK(app->free_camera && af_engine_mouse_captured(app->engine) && !app->runtime.host.camera_enabled);
        app->camera.position.x += 10;
        smoke_button(app, SDL_BUTTON_RIGHT, false);
    }
    if (stage == 7) {
        PLAY_CHECK(!app->free_camera && af_engine_mouse_captured(app->engine) && fabsf(app->camera.position.x) < .01f);
        smoke_key(app, SDL_SCANCODE_ESCAPE);
    }
    if (stage == 9) {
        PLAY_CHECK(!af_engine_mouse_captured(app->engine));
        smoke_click(app, 550, 350);
        smoke_key(app, SDL_SCANCODE_SPACE);
    }
    if (stage == 11) {
        PLAY_CHECK(af_engine_mouse_captured(app->engine));
        PLAY_CHECK(af_engine_audio_stats(app->engine).clips == 4 &&
                   af_engine_get_stats(app->engine).loaded_meshes >= 2 &&
                   af_engine_get_stats(app->engine).missing_meshes == 0);
    }
    if (stage == 13) {
        PLAY_CHECK(af_engine_audio_stats(app->engine).plays > 0 && af_engine_gameplay_status(app->engine).elapsed > 0);
        app->runtime.paused = true;
        af_engine_set_simulation_paused(app->engine, true);
    }
    if (stage == 14) {
        app->smoke_ticks = af_engine_get_stats(app->engine).simulation_ticks;
        af_engine_step_simulation(app->engine);
    }
    if (stage == 15) {
        PLAY_CHECK(af_engine_get_stats(app->engine).simulation_ticks == app->smoke_ticks + 1);
        smoke_key(app, SDL_SCANCODE_F6);
    }
    if (stage == 18) {
        PLAY_CHECK(app->maximize_play && app->viewport.x == 0 && app->runtime.paused &&
                   nk_window_find(&app->gui.context, "Play HUD") != NULL);
        smoke_key(app, SDL_SCANCODE_F6);
    }
    if (stage == 21) {
        PLAY_CHECK(!app->maximize_play && app->viewport.x == 252);
        stop_play(app);
        PLAY_CHECK(!af_engine_mouse_captured(app->engine) &&
                   memcmp(&app->camera, &app->edit_camera, sizeof(af_camera)) == 0);
    }
    if (stage == 22) {
        PLAY_CHECK(!app->runtime.playing && af_scene_object_count(app->document.scene) == 21 &&
                   !ed_document_dirty(&app->document));
        af_engine_request_quit(app->engine);
    }
}
#undef PLAY_CHECK

void smoke(editor *app) {
    if (!app->smoke_path)
        return;
    ed_document *d = &app->document;
    if (app->updates == 152) {
        app->show_colliders = true;
        for (size_t i = 0; i < af_scene_object_count(d->scene) && i < 3; ++i) {
            af_collider collider = af_collider_default();
            collider.type = (af_collider_type)(i + 1);
            af_scene_set_object_collider(d->scene, af_scene_object_at(d->scene, i)->id, collider);
        }
    }
    if (app->updates == 155) {
        app->show_performance = true;
        app->show_assets = true;
        app->show_environment = true;
        app->show_post_processing = true;
    }
    if (app->updates == 158) {
        app->failed |= nk_window_find(&app->gui.context, "Performance") == NULL ||
                       nk_window_find(&app->gui.context, "Assets") == NULL ||
                       nk_window_find(&app->gui.context, "Environment") == NULL ||
                       nk_window_find(&app->gui.context, "Post-processing") == NULL;
        app->show_performance = false;
        app->show_assets = false;
        app->show_environment = false;
        app->show_post_processing = false;
    }
    if (app->updates == 3)
        toggle_play(app);
    if (app->updates == 7) {
        const af_scene_object *o = af_scene_get_object(app->runtime.scene, 1);
        app->failed |= !app->runtime.playing || !o || o->transform.rotation.y == 0 ||
                       af_scene_get_object(d->scene, 1)->transform.rotation.y != 0;
    }
    if (app->updates == 8) {
        app->runtime.paused = true;
        af_engine_set_simulation_paused(app->engine, true);
    }
    if (app->updates == 9) {
        app->smoke_ticks = af_engine_get_stats(app->engine).simulation_ticks;
        af_engine_step_simulation(app->engine);
    }
    if (app->updates == 10) {
        app->failed |= af_engine_get_stats(app->engine).simulation_ticks != app->smoke_ticks + 1;
        stop_play(app);
    }
    if (app->updates == 10) {
        af_transform t = af_scene_get_object(d->scene, d->selection)->transform;
        t.position.x = -3;
        app->failed |= !ed_document_transform(d, t);
        ed_document_end_edit(d);
    }
    if (app->updates == 15)
        SMOKE_CHECK(app, ed_document_undo(d));
    if (app->updates == 20)
        SMOKE_CHECK(app, ed_document_redo(d));
    if (app->updates == 21)
        smoke_click(app, 1020, 195);
    if (app->updates == 23) {
        SMOKE_CHECK(app, SDL_TextInputActive(af_engine_sdl_window(app->engine)));
        SDL_Event event;
        SDL_zero(event);
        event.type = SDL_EVENT_TEXT_INPUT;
        event.text.windowID = SDL_GetWindowID(af_engine_sdl_window(app->engine));
        event.text.text = "x";
        SMOKE_CHECK(app, SDL_PushEvent(&event));
    }
    if (app->updates == 25) {
        SMOKE_CHECK(app, strchr(af_scene_get_object(d->scene, 1)->name, 'x') != NULL);
        SMOKE_CHECK(app, ed_document_name(d, "Editor smoke cube"));
        ed_document_end_edit(d);
    }
    if (app->updates == 26 || app->updates == 28 || app->updates == 29) {
        if (app->updates == 28)
            SMOKE_CHECK(app, strlen(af_scene_get_object(d->scene, 1)->name) == 16);
        SDL_Event event;
        SDL_zero(event);
        event.type = app->updates == 29 ? SDL_EVENT_KEY_UP : SDL_EVENT_KEY_DOWN;
        event.key.windowID = SDL_GetWindowID(af_engine_sdl_window(app->engine));
        event.key.scancode = SDL_SCANCODE_BACKSPACE;
        event.key.down = app->updates != 29;
        event.key.repeat = app->updates == 28;
        SMOKE_CHECK(app, SDL_PushEvent(&event));
    }
    if (app->updates == 30) {
        SMOKE_CHECK(app, strlen(af_scene_get_object(d->scene, 1)->name) == 15);
        SMOKE_CHECK(app, ed_document_duplicate(d));
    }
    if (app->updates == 35)
        SMOKE_CHECK(app, ed_document_delete(d));
    if (app->updates == 40)
        SMOKE_CHECK(app, ed_document_undo(d));
    if (app->updates == 45)
        SMOKE_CHECK(app, ed_document_save(d, app->smoke_path));
    if (app->updates == 50)
        SMOKE_CHECK(app, ed_document_load(d, app->smoke_path, false));
    if (app->updates == 55) {
        uint64_t revision = d->revision;
        SMOKE_CHECK(app, !ed_document_load(d, "missing-smoke-scene.json", false) && d->revision == revision);
    }
    if (app->updates == 65)
        app->failed |= !af_engine_set_window_size(app->engine, 1100, 720);
    if (app->updates == 95)
        app->failed |= !af_engine_set_window_size(app->engine, 1280, 800);
    if (app->updates <= 55)
        refresh(app);
    if (app->updates == 100) {
        app->gizmo.mode = ED_GIZMO_MOVE;
        af_vec2 p = {app->gizmo.origin.x + (app->gizmo.ends[0].x - app->gizmo.origin.x) * 0.7f,
                     app->gizmo.origin.y + (app->gizmo.ends[0].y - app->gizmo.origin.y) * 0.7f};
        SDL_Event event;
        SDL_zero(event);
        event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        event.button.windowID = SDL_GetWindowID(af_engine_sdl_window(app->engine));
        event.button.button = SDL_BUTTON_LEFT;
        event.button.down = true;
        event.button.x = p.x;
        event.button.y = p.y;
        app->failed |= !SDL_PushEvent(&event);
    }
    if (app->updates == 102) {
        SDL_Event event;
        SDL_zero(event);
        event.type = SDL_EVENT_MOUSE_MOTION;
        event.motion.windowID = SDL_GetWindowID(af_engine_sdl_window(app->engine));
        event.motion.x = app->gizmo.start.x + app->gizmo.direction.x * 0.4f;
        event.motion.y = app->gizmo.start.y + app->gizmo.direction.y * 0.4f;
        app->failed |= !SDL_PushEvent(&event);
    }
    if (app->updates == 104) {
        SDL_Event event;
        SDL_zero(event);
        event.type = SDL_EVENT_MOUSE_BUTTON_UP;
        event.button.windowID = SDL_GetWindowID(af_engine_sdl_window(app->engine));
        event.button.button = SDL_BUTTON_LEFT;
        app->failed |= !SDL_PushEvent(&event);
    }
    if (app->updates == 107) {
        const af_scene_object *o = af_scene_get_object(d->scene, d->selection);
        app->failed |= !o || o->transform.position.x <= app->gizmo.initial.position.x || app->gizmo.dragging;
        app->failed |= !ed_document_undo(d);
        refresh(app);
    }
    if (app->updates == 114)
        smoke_click(app, 70, 167); /* Add object: open dropdown */
    if (app->updates == 116)
        smoke_click(app, 70, 202); /* Select Cube (Mesh) */
    if (app->updates == 118)
        app->failed |= af_scene_object_count(d->scene) != 6;
    if (app->updates == 120)
        smoke_click(app, 180, 167);
    if (app->updates == 123)
        app->failed |= af_scene_object_count(d->scene) != 7;
    if (app->updates == 125)
        smoke_click(app, 490, 33);
    if (app->updates == 128)
        app->failed |= af_scene_object_count(d->scene) != 6;
    if (app->updates == 130)
        smoke_click(app, 560, 33);
    if (app->updates == 133)
        app->failed |= af_scene_object_count(d->scene) != 7;
    if (app->updates == 135)
        smoke_click(app, 971, 310);
    if (app->updates == 138) {
        const af_scene_object *o = af_scene_get_object(d->scene, d->selection);
        app->failed |= !o || o->visible;
    }
    if (app->updates == 140)
        smoke_click(app, 1042, 384);
    if (app->updates == 143) {
        const af_scene_object *o = af_scene_get_object(d->scene, d->selection);
        app->failed |= !o || fabsf(o->transform.position.x - 1.6f) > 0.001f;
    }
    if (app->updates == 145)
        app->failed |= !ed_document_save(d, app->smoke_path);
    if (app->updates == 150)
        app->failed |= !ed_document_load(d, app->smoke_path, false);
    if (app->updates == 151)
        refresh(app);
    if (app->updates == 155) {
        ed_document_name(d, "Unsaved modal test");
        ed_document_end_edit(d);
        SDL_Event close;
        SDL_zero(close);
        close.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
        close.window.windowID = SDL_GetWindowID(af_engine_sdl_window(app->engine));
        app->failed |= !SDL_PushEvent(&close);
    }
    if (app->updates == 157)
        app->failed |= !app->modal;
    if (app->updates == 158)
        smoke_click(app, 800, 430); /* Cancel window close. */
    if (app->updates == 161)
        app->failed |= app->modal || af_scene_object_count(d->scene) != 7;
    if (app->updates == 165)
        request_action(app, ED_NEW);
    if (app->updates == 168)
        smoke_click(app, 640, 430); /* Discard into a new scene. */
    if (app->updates == 171)
        app->failed |= app->modal || af_scene_object_count(d->scene) != 0 || ed_document_dirty(d);
}
