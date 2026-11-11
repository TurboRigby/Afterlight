/* SPDX-License-Identifier: MPL-2.0 */
#include "core/editor_state.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static nk_flags author_flags(editor *app) {
    return (panel_flags(app) & ~(nk_flags)NK_WINDOW_NO_SCROLLBAR) | (app->modal || app->dialog ? NK_WINDOW_ROM : 0);
}

static bool author_button(editor *app, unsigned index, const char *label_text) {
    struct nk_rect bounds = nk_widget_bounds(&app->gui.context);
    app->author_buttons[index] = (af_rect){bounds.x, bounds.y, bounds.w, bounds.h};
    return nk_button_label(&app->gui.context, label_text) != 0;
}
static void finish_close(editor *app) {
    ed_action after = app->pending;
    app->pending = ED_NONE;
    af_engine_animation_preview_pose(app->engine, app->animation_author.root, NULL, 0);
    ed_animation_editor_destroy(&app->animation_author);
    app->gizmo.dragging = false;
    if (after != ED_NONE)
        request_action(app, after);
}
void request_animation_close(editor *app, ed_action after) {
    app->pending = after;
    if (ed_animation_editor_dirty(&app->animation_author)) {
        app->animation_author.close_requested = true;
        app->animation_author.playing = false;
    } else {
        finish_close(app);
    }
}
void start_animation_authoring(editor *app) {
    if (app->runtime.playing || app->dialog || app->modal)
        return;
    if (app->animation_author.open) {
        set_status(app, "Finish this animation draft before opening another model.");
        return;
    }
    const af_scene_object *root = af_scene_get_object(app->document.scene, app->animation_target);
    const af_scene_asset *asset = root ? af_scene_find_asset(app->document.scene, root->animation.asset) : NULL;
    char path[AF_ASSET_PATH_CAPACITY];
    af_asset_error error = {0};
    if (!asset || !af_asset_resolve_path(app->document.path, asset->path, path, sizeof(path)) ||
        !ed_animation_editor_open(&app->animation_author, root->id, path, root->animation.clip, &error)) {
        set_status(app, error.message[0] ? error.message : "Select an imported rigged model first.");
        return;
    }
    app->animation_preview = app->show_animation = false;
    app->blueprint_mode = app->landscape_mode = false;
    app->edit_part = app->edit_joint = 0;
    app->gizmo.dragging = false;
    app->gizmo.mode = ED_GIZMO_ROTATE;
    app->show_skeleton = true;
    set_status(app, "Choose a bone, pose it in the viewport or Inspector, then Record pose.");
}
void save_authored_animation_as(editor *app) {
    ed_animation_editor *e = &app->animation_author;
    if (!e->open || app->runtime.playing || app->dialog)
        return;
    char suggested[AF_ASSET_PATH_CAPACITY];
    if (!af_asset_resolve_path(app->document.path, "animation.afanim", suggested, sizeof(suggested)))
        return;
    af_engine_set_mouse_capture(app->engine, false);
    app->free_camera = false;
    app->dialog = ed_dialog_begin_animation(app->engine, e->path[0] ? e->path : suggested);
    app->dialog_asset = 40;
    if (!app->dialog)
        set_status(app, "Cannot open the animation save dialog.");
}
bool save_authored_animation(editor *app, const char *path) {
    ed_animation_editor *e = &app->animation_author;
    if (!e->open || app->runtime.playing || app->dialog)
        return false;
    if (!path && !e->path[0]) {
        save_authored_animation_as(app);
        return false;
    }
    if (e->pose_dirty && !ed_animation_editor_record(e)) {
        set_status(app, "Recording this pose failed. Check the clip and channel limits.");
        return false;
    }
    char destination[AF_ASSET_PATH_CAPACITY], absolute[AF_ASSET_PATH_CAPACITY];
    const char *selected = path ? path : e->path;
    const char *extension = strrchr(selected, '.');
    bool native = extension && SDL_strcasecmp(extension, ".afanim") == 0;
    int length = snprintf(destination, sizeof(destination), "%s%s", selected, native ? "" : ".afanim");
    af_asset_error error = {0};
    if (length < 0 || (size_t)length >= sizeof(destination) ||
        !af_asset_resolve_path(app->document.path, destination, absolute, sizeof(absolute)) ||
        !ed_document_save_animation(&app->document, e->root, absolute, &e->file, e->clip, &error)) {
        set_status(app, error.message[0] ? error.message : "Saving the animation failed. Check the model and path.");
        return false;
    }
    ed_animation_editor_saved(e, absolute);
    const af_scene_object *root = af_scene_get_object(app->document.scene, e->root);
    if (!af_engine_load_animation(app->engine, root->animation.asset, absolute, &error)) {
        set_status(app, error.message);
        return false;
    }
    refresh(app);
    set_status(app, "Animation saved and assigned to the model. Save the scene to keep its binding.");
    if (e->close_requested)
        finish_close(app);
    return true;
}
void update_animation_authoring(editor *app, double delta) {
    ed_animation_editor *e = &app->animation_author;
    if (!e->open)
        return;
    if (app->runtime.playing || app->blueprint_mode) {
        af_engine_animation_preview_pose(app->engine, e->root, NULL, 0);
        e->playing = false;
        return;
    }
    if (e->playing && !app->dialog && !app->modal && !e->close_requested) {
        double duration = e->file.clips[e->clip].duration;
        double time = e->time + delta;
        if (duration <= 0) {
            time = 0;
            e->playing = false;
        } else if (time >= duration) {
            time = e->loop ? fmod(time, duration) : duration;
            e->playing = e->loop;
        }
        e->time = (float)time;
        ed_animation_editor_sample(e);
    }
    if (!af_engine_animation_preview_pose(app->engine, e->root, e->poses, e->file.node_count)) {
        e->playing = false;
        set_status(app, "The animation model is unavailable. Save the draft or close the authoring workspace.");
    }
}
static const af_scene_object *bone_object(const editor *app, uint32_t node) {
    for (size_t i = 0; i < af_scene_object_count(app->document.scene); ++i) {
        const af_scene_object *object = af_scene_object_at(app->document.scene, i);
        if (object->model.root == app->animation_author.root && !object->model.primitive && object->model.node == node)
            return object;
    }
    return NULL;
}
void project_animation_bone(editor *app) {
    ed_animation_editor *e = &app->animation_author;
    const af_scene_object *object = bone_object(app, e->node);
    af_mat4 parent = af_mat4_identity(), inverse;
    if (!object || !af_mat4_inverse(af_animation_pose_matrix(e->file.bind_pose[e->node]), &inverse) ||
        (object->parent && !af_engine_animated_world_matrix(app->engine, object->parent, &parent))) {
        app->gizmo.projected = false;
        return;
    }
    af_mat4 basis = af_mat4_mul(parent, af_mat4_mul(af_transform_matrix(object->transform), inverse));
    af_animation_pose pose = e->poses[e->node];
    af_vec4 center = af_mat4_transform(basis, (af_vec4){pose.translation.x, pose.translation.y, pose.translation.z, 1});
    ed_gizmo_project(&app->gizmo, &app->camera, app->viewport, (af_vec3){center.x, center.y, center.z}, basis,
                     af_vec3_scale(e->rotation_degrees, AF_PI / 180));
}
void animation_bone_input(editor *app) {
    ed_animation_editor *e = &app->animation_author;
    af_engine *engine = app->engine;
    af_vec2 mouse = af_engine_mouse_position(engine);
    if (app->gizmo.dragging) {
        bool cancel = af_engine_key_pressed(engine, AF_KEY_ESCAPE);
        af_transform pose = cancel ? app->gizmo.initial : ed_gizmo_drag(&app->gizmo, mouse);
        af_animation_pose *target = &e->poses[e->node];
        target->translation = pose.position;
        target->rotation = af_quaternion_from_euler(pose.rotation);
        target->scale = pose.scale;
        e->pose_dirty = true;
        ed_animation_editor_select(e, e->node);
        if (cancel || !af_engine_mouse_down(engine, AF_MOUSE_LEFT))
            app->gizmo.dragging = false;
        return;
    }
    if (e->close_requested || app->fields_active || e->playing || af_engine_mouse_captured(engine) ||
        !inside(app->viewport, mouse) || !af_engine_mouse_pressed(engine, AF_MOUSE_LEFT))
        return;
    ed_gui_unfocus(&app->gui);
    int axis = ed_gizmo_hit(&app->gizmo, mouse);
    if (axis >= 0) {
        af_animation_pose pose = e->poses[e->node];
        af_transform transform = {.position = pose.translation,
                                  .scale = pose.scale,
                                  .rotation = af_vec3_scale(e->rotation_degrees, AF_PI / 180)};
        ed_gizmo_begin(&app->gizmo, axis, e->root, transform, mouse);
    } else {
        af_animation_bone bones[AF_ANIMATION_MAX_JOINTS];
        size_t count = af_engine_animation_bones(engine, e->root, bones, AF_ANIMATION_MAX_JOINTS);
        float nearest = 12;
        for (size_t i = 0; i < count && i < AF_ANIMATION_MAX_JOINTS; ++i) {
            af_vec2 screen;
            if (!ed_project(&app->camera, app->viewport, bones[i].position, &screen))
                continue;
            float distance = hypotf(screen.x - mouse.x, screen.y - mouse.y);
            if (distance < nearest) {
                nearest = distance;
                ed_animation_editor_select(e, bones[i].node);
            }
        }
    }
}
static void bone_list(editor *app, float height) {
    ed_animation_editor *e = &app->animation_author;
    struct nk_context *ctx = &app->gui.context;
    if (nk_begin(ctx, "Rig bones", nk_rect(0, 64, app->left_sidebar_width, height - 96), author_flags(app))) {
        label(ctx, "Rig hierarchy", 28);
        for (size_t i = 0; i < af_scene_object_count(app->document.scene); ++i) {
            const af_scene_object *object = af_scene_object_at(app->document.scene, i);
            if (object->model.root != e->root || object->model.primitive || object->model.node >= e->file.node_count)
                continue;
            unsigned depth = 0;
            int32_t parent = e->file.parents[object->model.node];
            while (parent >= 0 && depth < 8) {
                ++depth;
                parent = e->file.parents[parent];
            }
            char text[160];
            snprintf(text, sizeof(text), "%*s%s", (int)(depth * 2), "", object->name);
            nk_layout_row_dynamic(ctx, 28, 1);
            if (nk_select_label(ctx, text, NK_TEXT_LEFT, e->node == object->model.node) &&
                e->node != object->model.node) {
                e->playing = false;
                ed_animation_editor_select(e, object->model.node);
            }
        }
        label(ctx, "Click joints in the viewport to select.", 36);
    }
    nk_end(ctx);
}
static void bone_inspector(editor *app, float width, float height) {
    ed_animation_editor *e = &app->animation_author;
    struct nk_context *ctx = &app->gui.context;
    if (nk_begin(ctx, "Bone pose", nk_rect(width - app->right_sidebar_width, 64, app->right_sidebar_width, height - 96),
                 author_flags(app))) {
        const af_scene_object *object = bone_object(app, e->node);
        label(ctx, object ? object->name : "Bone pose", 28);
        label(ctx, "Local bone transform", 24);
        if (e->playing || e->close_requested)
            nk_widget_disable_begin(ctx);
        af_animation_pose *pose = &e->poses[e->node];
        bool edited = vector_fields(ctx, "Position", &pose->translation, .05f, .005f);
        if (vector_fields(ctx, "Rotation (degrees)", &e->rotation_degrees, 1, .1f)) {
            pose->rotation = af_quaternion_from_euler(af_vec3_scale(e->rotation_degrees, AF_PI / 180));
            edited = true;
        }
        edited |= vector_fields(ctx, "Scale", &pose->scale, .05f, .005f);
        if (edited)
            e->pose_dirty = true;
        nk_layout_row_dynamic(ctx, 30, 1);
        if (author_button(app, 1, "Record pose")) {
            if (!ed_animation_editor_record(e))
                set_status(app, "Recording the pose failed. Check the animation limits.");
        }
        nk_layout_row_dynamic(ctx, 28, 2);
        if (nk_button_label(ctx, "Bind pose")) {
            e->poses[e->node] = e->file.bind_pose[e->node];
            e->pose_dirty = true;
            ed_animation_editor_select(e, e->node);
        }
        if (nk_button_label(ctx, "Revert pose"))
            ed_animation_editor_sample(e);
        const char *interpolations[] = {"Linear / quaternion SLERP", "Step (hold pose)"};
        label(ctx, "Interpolation for recorded channels", 24);
        nk_layout_row_dynamic(ctx, 28, 1);
        e->interpolation = nk_combo(ctx, interpolations, 2, e->interpolation, 26, nk_vec2(280, 90));
        label(ctx, "Clip name", 24);
        nk_layout_row_dynamic(ctx, 28, 1);
        nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, e->clip_name, sizeof(e->clip_name), nk_filter_default);
        nk_layout_row_dynamic(ctx, 28, 1);
        nk_property_float(ctx, "Duration (s)", .01f, &e->clip_duration, 3600, .1f, .01f);
        nk_layout_row_dynamic(ctx, 28, 1);
        if (nk_button_label(ctx, "Apply clip settings") &&
            !ed_animation_editor_clip_settings(e, e->clip_name, e->clip_duration))
            set_status(app, "A clip needs a name and enough duration to contain its keys and events.");
        if (e->pose_dirty)
            label(ctx, "Pose edited. Record it to keep the keys.", 42);
        if (e->playing || e->close_requested)
            nk_widget_disable_end(ctx);
    }
    nk_end(ctx);
}
static void timeline(editor *app) {
    ed_animation_editor *e = &app->animation_author;
    struct nk_context *ctx = &app->gui.context;
    af_animation_clip *clip = &e->file.clips[e->clip];
    nk_layout_row_dynamic(ctx, 52, 1);
    struct nk_rect bounds;
    nk_widget(&bounds, ctx);
    struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);
    nk_fill_rect(canvas, bounds, 3, nk_rgb(24, 29, 36));
    float duration = fmaxf(.01f, (float)clip->duration);
    for (unsigned i = 0; i <= 4; ++i) {
        float x = bounds.x + bounds.w * (float)i / 4;
        nk_stroke_line(canvas, x, bounds.y + 24, x, bounds.y + 52, 1, nk_rgb(60, 70, 83));
        char text[24];
        int length = snprintf(text, sizeof(text), "%.2f", (double)(duration * (float)i / 4));
        if (length > 0 && (size_t)length < sizeof(text))
            nk_draw_text(canvas, nk_rect(fminf(x, bounds.x + bounds.w - 64), bounds.y, 64, 22), text, length,
                         ctx->style.font, nk_rgb(24, 29, 36), nk_rgb(182, 192, 205));
    }
    for (size_t i = 0; i < clip->channel_count; ++i) {
        const af_animation_channel *channel = &clip->channels[i];
        if (channel->node != e->node || channel->path > AF_ANIM_SCALE)
            continue;
        for (size_t key = 0; key < channel->key_count; ++key) {
            float x = bounds.x + bounds.w * channel->times[key] / duration;
            nk_fill_circle(canvas, nk_rect(x - 3, bounds.y + 28, 6, 6), nk_rgb(255, 194, 84));
        }
    }
    float playhead = bounds.x + bounds.w * e->time / duration;
    nk_stroke_line(canvas, playhead, bounds.y, playhead, bounds.y + bounds.h, 2, nk_rgb(130, 240, 235));
    if (!e->close_requested && !app->modal && !app->dialog && nk_input_is_mouse_hovering_rect(&ctx->input, bounds) &&
        nk_input_is_mouse_down(&ctx->input, NK_BUTTON_LEFT)) {
        float time = fmaxf(0, fminf(duration, (ctx->input.mouse.pos.x - bounds.x) / fmaxf(1, bounds.w) * duration));
        float requested = time, nearest = 8 * duration / fmaxf(1, bounds.w);
        for (size_t i = 0; i < clip->channel_count; ++i) {
            const af_animation_channel *channel = &clip->channels[i];
            if (channel->node != e->node || channel->path > AF_ANIM_SCALE)
                continue;
            for (size_t key = 0; key < channel->key_count; ++key) {
                float distance = fabsf(channel->times[key] - requested);
                if (distance < nearest) {
                    nearest = distance;
                    time = channel->times[key];
                }
            }
        }
        e->time = time;
        e->playing = false;
        ed_animation_editor_sample(e);
    }
}
void animation_author_panel(editor *app, float width, float height) {
    ed_animation_editor *e = &app->animation_author;
    if (!e->open || app->runtime.playing)
        return;
    bone_list(app, height);
    bone_inspector(app, width, height);
    struct nk_context *ctx = &app->gui.context;
    af_rect view = app->viewport;
    if (nk_begin(ctx, "Skeletal animation", nk_rect(view.x, view.y + view.height, view.width, 244),
                 author_flags(app) | NK_WINDOW_NO_SCROLLBAR)) {
        label(ctx, ed_animation_editor_dirty(e) ? "Skeletal animation *" : "Skeletal animation", 24);
        if (e->close_requested) {
            label(ctx, "Save the animation draft before closing?", 28);
            nk_layout_row_dynamic(ctx, 32, 3);
            if (nk_button_label(ctx, "Save"))
                save_authored_animation(app, NULL);
            if (nk_button_label(ctx, "Discard"))
                finish_close(app);
            if (nk_button_label(ctx, "Cancel")) {
                e->close_requested = false;
                app->pending = ED_NONE;
            }
            nk_end(ctx);
            return;
        }
        nk_layout_row_dynamic(ctx, 28, 4);
        if (nk_combo_begin_label(ctx, e->file.clips[e->clip].name, nk_vec2(220, 200))) {
            nk_layout_row_dynamic(ctx, 26, 1);
            for (size_t i = 0; i < e->file.clip_count; ++i)
                if (nk_combo_item_label(ctx, e->file.clips[i].name, NK_TEXT_LEFT)) {
                    e->clip = i;
                    e->time = 0;
                    e->playing = false;
                    ed_animation_editor_sample(e);
                }
            nk_combo_end(ctx);
        }
        if (author_button(app, 0, "New clip") && !ed_animation_editor_new_clip(e))
            set_status(app, "Cannot add another clip. Check the animation limits.");
        if (author_button(app, 2, "Save clips"))
            save_authored_animation(app, NULL);
        if (nk_button_label(ctx, "Save As"))
            save_authored_animation_as(app);
        timeline(app);
        nk_layout_row_dynamic(ctx, 28, 3);
        if (nk_property_float(ctx, "Time (s)", 0, &e->time, (float)e->file.clips[e->clip].duration, .05f, .005f)) {
            e->playing = false;
            ed_animation_editor_sample(e);
        }
        if (author_button(app, 3, e->playing ? "Pause" : "Play clip")) {
            if (e->pose_dirty && !ed_animation_editor_record(e)) {
                set_status(app, "Recording this pose failed. Check the clip and channel limits.");
            } else {
                e->playing = !e->playing;
                if (e->time >= e->file.clips[e->clip].duration)
                    e->time = 0;
            }
        }
        nk_bool loop = e->loop;
        nk_checkbox_label(ctx, "Loop", &loop);
        e->loop = loop != 0;
        nk_layout_row_dynamic(ctx, 28, 4);
        if (author_button(app, 4, "Undo"))
            ed_animation_editor_undo(e, false);
        if (author_button(app, 5, "Redo"))
            ed_animation_editor_undo(e, true);
        if (author_button(app, 6, "Delete key"))
            ed_animation_editor_delete_key(e);
        if (author_button(app, 7, "Close"))
            request_animation_close(app, ED_NONE);
    }
    nk_end(ctx);
}
