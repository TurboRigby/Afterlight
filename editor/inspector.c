/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "inspector.h"
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void local_light_controls(editor *app) {
    struct nk_context *ctx = &app->gui.context;
    size_t count = af_scene_local_light_count(app->document.scene);
    if (!app->light_selected || app->selected_light >= count)
        return;
    af_local_light local = *af_scene_local_light_at(app->document.scene, app->selected_light);
    af_local_light before = local;
    char header[64];
    snprintf(header, sizeof(header), "%s light", local.type == AF_LIGHT_SPOT ? "Spot" : "Point");
    label(ctx, header, 30);
    label(ctx, "Light name", 22);
    nk_layout_row_dynamic(ctx, 30, 1);
    nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, local.name, sizeof(local.name), nk_filter_default);
    nk_layout_row_dynamic(ctx, 26, 1);
    nk_bool enabled = local.enabled;
    nk_checkbox_label(ctx, "Light enabled", &enabled);
    local.enabled = enabled != 0;
    nk_layout_row_dynamic(ctx, 30, 1);
    static const char *types[] = {"Point light", "Spot light"};
    local.type = (af_light_type)nk_combo(ctx, types, 2, (int)local.type, 26, nk_vec2(290, 90));
    vector_fields(ctx, "Light position", &local.position, 0.1f, 0.01f);
    label(ctx, "Light color (linear)", 22);
    nk_layout_row_dynamic(ctx, 30, 3);
    nk_property_float(ctx, "#R", 0, &local.color.x, 100, 0.1f, 0.01f);
    nk_property_float(ctx, "#G", 0, &local.color.y, 100, 0.1f, 0.01f);
    nk_property_float(ctx, "#B", 0, &local.color.z, 100, 0.1f, 0.01f);
    nk_layout_row_dynamic(ctx, 30, 1);
    nk_property_float(ctx, "Light intensity", 0, &local.intensity, 10000, 1, 0.1f);
    nk_layout_row_dynamic(ctx, 30, 1);
    nk_property_float(ctx, "Light range", 0.01f, &local.range, 10000, 0.5f, 0.05f);
    if (local.type == AF_LIGHT_SPOT) {
        vector_fields(ctx, "Spot direction", &local.direction, 0.1f, 0.01f);
        float inner = local.inner_angle * 180 / AF_PI, outer = local.outer_angle * 180 / AF_PI;
        nk_layout_row_dynamic(ctx, 30, 1);
        nk_property_float(ctx, "Inner cone (deg)", 0, &inner, 89, 1, 0.1f);
        nk_layout_row_dynamic(ctx, 30, 1);
        nk_property_float(ctx, "Outer cone (deg)", inner, &outer, 89, 1, 0.1f);
        if (inner != before.inner_angle * 180 / AF_PI)
            local.inner_angle = inner * AF_PI / 180;
        if (outer != before.outer_angle * 180 / AF_PI)
            local.outer_angle = outer * AF_PI / 180;
    }
    nk_layout_row_dynamic(ctx, 26, 1);
    nk_bool cast = local.cast_shadows;
    nk_checkbox_label(ctx, "Cast local shadows", &cast);
    local.cast_shadows = cast != 0;
    if (local.cast_shadows) {
        nk_layout_row_dynamic(ctx, 30, 1);
        nk_property_float(ctx, "Shadow depth bias", 0, &local.shadow_bias, .1f, .0001f, .00001f);
        label(ctx, "First 4 enabled shadow lights share the budget.", 24);
    }
    if (memcmp(&local, &before, sizeof(local)))
        ed_document_local_light(&app->document, app->selected_light, local);
    nk_layout_row_dynamic(ctx, 32, 2);
    if (nk_button_label(ctx, "Focus"))
        focus_selection(app);
    if (nk_button_label(ctx, "Delete light")) {
        ed_document_remove_light(&app->document, app->selected_light);
        app->light_selected = false;
        app->gizmo.dragging = false;
    }
}

void collider_controls(editor *app, af_scene_object object) {
    struct nk_context *ctx = &app->gui.context;
    label(ctx, "Collider", 30);
    af_collider value = object.collider, before = value;
    static const char *types[] = {"No collider", "Box", "Sphere", "Capsule", "Imported mesh", "Compound"};
    nk_layout_row_dynamic(ctx, 30, 1);
    value.type = (af_collider_type)nk_combo(ctx, types, 6, (int)value.type, 26, nk_vec2(290, 150));
    if (before.type == AF_COLLIDER_NONE && value.type == AF_COLLIDER_BOX && !strcmp(object.mesh, "cube"))
        value.half_extents = (af_vec3){0.65f, 0.65f, 0.65f};
    if (value.type != AF_COLLIDER_NONE) {
        nk_layout_row_dynamic(ctx, 26, 2);
        nk_bool enabled = value.enabled, trigger = value.trigger;
        nk_checkbox_label(ctx, "Enabled", &enabled);
        nk_checkbox_label(ctx, "Trigger", &trigger);
        value.enabled = enabled != 0;
        value.trigger = trigger != 0;
        if (value.type == AF_COLLIDER_MESH || value.type == AF_COLLIDER_COMPOUND) {
            value.center = (af_vec3){0};
            label(ctx,
                  value.type == AF_COLLIDER_COMPOUND ? "Parts share one body and computed center of mass."
                                                     : "Uses imported static triangles; moving bodies unsupported.",
                  42);
        } else
            vector_fields(ctx, "Collider center", &value.center, 0.1f, 0.01f);
        if (value.type == AF_COLLIDER_BOX) {
            label(ctx, "Half extents", 22);
            nk_layout_row_dynamic(ctx, 30, 3);
            nk_property_float(ctx, "#X", 0.001f, &value.half_extents.x, 10000, 0.1f, 0.01f);
            nk_property_float(ctx, "#Y", 0.001f, &value.half_extents.y, 10000, 0.1f, 0.01f);
            nk_property_float(ctx, "#Z", 0.001f, &value.half_extents.z, 10000, 0.1f, 0.01f);
        } else if (value.type != AF_COLLIDER_MESH && value.type != AF_COLLIDER_COMPOUND) {
            nk_layout_row_dynamic(ctx, 30, 1);
            nk_property_float(ctx, "Radius", 0.001f, &value.radius, 10000, 0.05f, 0.005f);
            if (value.type == AF_COLLIDER_CAPSULE) {
                nk_layout_row_dynamic(ctx, 30, 1);
                nk_property_float(ctx, "Segment half height", 0, &value.half_height, 10000, 0.1f, 0.01f);
            }
        }
        nk_layout_row_dynamic(ctx, 30, 1);
        int layer = 0;
        uint32_t bit = value.layer;
        while (bit > 1) {
            bit >>= 1;
            ++layer;
        }
        int next = layer;
        nk_property_int(ctx, "Layer bit", 0, &next, 31, 1, 1);
        if (next != layer)
            value.layer = UINT32_C(1) << (unsigned)next;
        char mask[96];
        snprintf(mask, sizeof(mask), "Mask 0x%08" PRIx32 "; selector assigns one bit.", value.layer);
        label(ctx, mask, 24);
        label(ctx, "Collider visibility is independent of mesh visibility.", 32);
        af_world_collider world;
        if (value.enabled && !af_scene_world_collider(app->document.scene, object.id, &world)) {
            nk_layout_row_dynamic(ctx, 42, 1);
            nk_label_wrap(ctx, "Collider skipped: zero scale or sheared world transform.");
        }
    }
    if (memcmp(&value, &before, sizeof(value)))
        ed_document_collider(&app->document, value);
}

void water_controls(editor *app, af_scene_object object) {
    const af_water *source = af_scene_get_water(app->document.scene, object.id);
    if (!source)
        return;
    af_water w = *source, before = w;
    struct nk_context *ctx = &app->gui.context;
    label(ctx, "Water", 30);
    static const char *modes[] = {"Pool / lake", "River / stream", "Pourable fluid"};
    nk_layout_row_dynamic(ctx, 28, 1);
    w.mode = (af_water_mode)nk_combo(ctx, modes, 3, (int)w.mode, 26, nk_vec2(260, 100));
    nk_layout_row_dynamic(ctx, 28, 1);
    nk_bool enabled = w.enabled;
    if (nk_checkbox_label(ctx, "Simulate water", &enabled))
        w.enabled = enabled != 0;
    static const char *qualities[] = {"Low: 16 x 16", "Medium: 32 x 32", "High: 64 x 64"};
    nk_layout_row_dynamic(ctx, 28, 1);
    w.quality = (af_water_quality)nk_combo(ctx, qualities, 3, (int)w.quality, 26, nk_vec2(260, 120));
    nk_layout_row_dynamic(ctx, 28, 1);
    nk_property_float(ctx, "Half width (m)", .25f, &w.half_size.x, 1000, .25f, .05f);
    nk_property_float(ctx, "Half length (m)", .25f, &w.half_size.y, 1000, .25f, .05f);
    nk_property_float(ctx, "Depth (m)", .05f, &w.depth, 100, .1f, .02f);
    nk_property_float(ctx, "Density (kg/m3)", .01f, &w.density, 20000, 50, 5);
    nk_property_float(ctx, "Drag", 0, &w.drag, 20, .1f, .02f);
    nk_property_float(ctx, "Wave speed (m/s)", .01f, &w.wave_speed, 20, .1f, .02f);
    nk_property_float(ctx, "Damping", 0, &w.damping, 20, .1f, .02f);
    nk_property_float(ctx, "Local wave forcing", 0, &w.wind, 1, .01f, .002f);
    vector_fields(ctx, "Absorption (RGB / m)", &w.absorption, .05f, .01f);
    vector_fields(ctx, "Scattering color", &w.scattering, .05f, .01f);
    nk_layout_row_dynamic(ctx, 28, 1);
    nk_property_float(ctx, "Roughness", .04f, &w.roughness, 1, .02f, .005f);
    nk_property_float(ctx, "IOR", 1, &w.ior, 2.5f, .01f, .002f);
    nk_property_float(ctx, "Ripple size (m)", .1f, &w.ripple_scale, 20, .1f, .01f);
    nk_property_float(ctx, "Ripple detail", 0, &w.ripple_strength, 1, .05f, .005f);
    if (w.mode == AF_WATER_RIVER) {
        nk_layout_row_dynamic(ctx, 28, 1);
        nk_property_float(ctx, "Current (m/s)", 0, &w.flow_speed, 20, .1f, .02f);
        int count = (int)w.path_count;
        nk_property_int(ctx, "Path points", 2, &count, AF_WATER_MAX_PATH, 1, 1);
        if (count > (int)w.path_count)
            for (unsigned i = w.path_count; i < (unsigned)count; ++i) {
                w.path[i] = w.path[i - 1];
                w.path[i].z += 2;
            }
        w.path_count = (unsigned)count;
        for (unsigned i = 0; i < w.path_count; ++i) {
            char title[48];
            snprintf(title, sizeof(title), "Path point %u", i + 1);
            vector_fields(ctx, title, &w.path[i], .25f, .05f);
        }
    }
    if (w.mode == AF_WATER_PARTICLES) {
        nk_layout_row_dynamic(ctx, 28, 1);
        nk_property_float(ctx, "Particle spacing (m)", .06f, &w.particle_spacing, .5f, .01f, .002f);
        nk_property_float(ctx, "Initial volume (m3)", 0, &w.initial_volume, 100, .01f, .002f);
        nk_property_float(ctx, "Emitter (m3/s)", 0, &w.emission_rate, 2, .01f, .002f);
        nk_property_float(ctx, "Viscosity (1/s)", 0, &w.viscosity, 20, .1f, .02f);
        vector_fields(ctx, "Emission velocity", &w.emission_velocity, .1f, .02f);
        label(ctx, "Low / Medium / High: 256 / 1024 / 4096 particles", 40);
    }
    label(ctx, "Move/yaw the surface; edit its size above.", 34);
    if (memcmp(&w, &before, sizeof(w)))
        ed_document_water(&app->document, w);
    nk_layout_row_dynamic(ctx, 28, 1);
    if (nk_button_label(ctx, "Remove water component"))
        ed_document_remove_water(&app->document);
}

void part_controls(editor *app, af_scene_object object) {
    struct nk_context *ctx = &app->gui.context;
    if (object.gameplay.player.enabled)
        return;
    label(ctx, "Collider parts", 28);
    nk_layout_row_dynamic(ctx, 28, 2);
    for (unsigned type = 0; type < 2; ++type)
        if (nk_button_label(ctx, type ? "Add sphere part" : "Add box part")) {
            af_collider_part part = af_collider_part_default();
            part.collider.type = type ? AF_COLLIDER_SPHERE : AF_COLLIDER_BOX;
            if (!af_scene_collider_part_count(app->document.scene, object.id) &&
                (object.collider.type == AF_COLLIDER_BOX || object.collider.type == AF_COLLIDER_SPHERE)) {
                part.collider = object.collider;
                part.collider.trigger = false;
                part.local.position = part.collider.center;
                part.collider.center = (af_vec3){0};
            }
            ed_document_end_edit(&app->document);
            ed_document_part(&app->document, part);
            ed_document_end_edit(&app->document);
            return;
        }
    for (size_t i = 0; i < af_scene_collider_part_count(app->document.scene, object.id); ++i) {
        af_collider_part part = *af_scene_collider_part_at(app->document.scene, object.id, i), before = part;
        char title[96];
        snprintf(title, sizeof(title), "Part %llu: %s", (unsigned long long)part.id, part.name);
        if (!nk_tree_push_id(ctx, NK_TREE_NODE, title, NK_MINIMIZED, (int)part.id))
            continue;
        static const char *types[] = {"Box", "Sphere"};
        nk_layout_row_dynamic(ctx, 28, 1);
        part.collider.type = nk_combo(ctx, types, 2, part.collider.type == AF_COLLIDER_SPHERE, 24, nk_vec2(250, 100))
                                 ? AF_COLLIDER_SPHERE
                                 : AF_COLLIDER_BOX;
        vector_fields(ctx, "Part position", &part.local.position, .1f, .01f);
        af_vec3 degrees = af_vec3_scale(part.local.rotation, 180 / AF_PI);
        if (vector_fields(ctx, "Part rotation (degrees)", &degrees, 5, .5f))
            part.local.rotation = af_vec3_scale(degrees, AF_PI / 180);
        vector_fields(ctx, "Part scale", &part.local.scale, .1f, .01f);
        if (part.collider.type == AF_COLLIDER_BOX)
            vector_fields(ctx, "Half extents", &part.collider.half_extents, .1f, .01f);
        else {
            nk_layout_row_dynamic(ctx, 28, 1);
            nk_property_float(ctx, "Radius", .001f, &part.collider.radius, 10000, .1f, .01f);
        }
        nk_layout_row_dynamic(ctx, 28, 1);
        nk_property_float(ctx, "Density (kg/m3)", .001f, &part.density, 1000000, 50, 5);
        nk_layout_row_dynamic(ctx, 28, 3);
        const char *tools[] = {"Move part", "Rotate part", "Scale part"};
        for (unsigned tool = 0; tool < 3; ++tool)
            if (nk_button_label(ctx, tools[tool])) {
                app->edit_part = part.id;
                app->edit_joint = 0;
                app->gizmo.mode = (ed_gizmo_mode)tool;
                app->gizmo.dragging = false;
                app->show_colliders = true;
                ed_document_end_edit(&app->document);
            }
        nk_layout_row_dynamic(ctx, 28, 2);
        bool fit = nk_button_label(ctx, "Fit mesh bounds") != 0, remove = nk_button_label(ctx, "Delete part") != 0;
        nk_tree_pop(ctx);
        if (fit) {
            ed_document_fit_part(&app->document, part.id);
            return;
        }
        if (remove) {
            ed_document_remove_part(&app->document, part.id);
            return;
        }
        if (memcmp(&part, &before, sizeof(part))) {
            ed_document_part(&app->document, part);
            return;
        }
    }
    if (object.collider.type == AF_COLLIDER_COMPOUND) {
        af_mass_properties mass;
        if (af_scene_compound_mass(app->document.scene, object.id, &mass)) {
            char text[140];
            snprintf(text, sizeof(text), "Volume %.4f m3 | material mass %.2f kg", mass.volume, mass.mass);
            label(ctx, text, 30);
            nk_layout_row_dynamic(ctx, 28, 1);
            if (nk_button_label(ctx, "Use computed total mass")) {
                af_rigid_body body = object.body;
                body.mass = mass.mass;
                ed_document_end_edit(&app->document);
                ed_document_body(&app->document, body);
                ed_document_end_edit(&app->document);
            }
        }
    }
    if (app->edit_part) {
        nk_layout_row_dynamic(ctx, 26, 1);
        if (nk_button_label(ctx, "Finish part edit")) {
            app->edit_part = 0;
            app->gizmo.dragging = false;
            ed_document_end_edit(&app->document);
        }
    }
}

void joint_controls(editor *app, af_scene_object object) {
    struct nk_context *ctx = &app->gui.context;
    label(ctx, "Joints", 28);
    if (object.body.enabled && object.body.type == AF_BODY_DYNAMIC) {
        nk_layout_row_dynamic(ctx, 28, 2);
        for (unsigned type = 0; type < 2; ++type)
            if (nk_button_label(ctx, type ? "Add hinge" : "Add distance")) {
                af_joint j = af_joint_default();
                j.a = object.id;
                j.type = (af_joint_type)type;
                snprintf(j.name, sizeof(j.name), "%s joint", type ? "Hinge" : "Distance");
                af_world_collider c;
                if (af_scene_world_collider(app->document.scene, object.id, &c))
                    j.anchor_b = af_vec3_add(c.center, (af_vec3){0, type ? 0 : 1, 0});
                ed_document_end_edit(&app->document);
                ed_document_joint(&app->document, j);
                ed_document_end_edit(&app->document);
                return;
            }
    }
    for (size_t i = 0; i < af_scene_joint_count(app->document.scene); ++i) {
        af_joint j = *af_scene_joint_at(app->document.scene, i);
        if (j.a != object.id && j.b != object.id)
            continue;
        char title[96];
        snprintf(title, sizeof(title), "%s [%llu]", j.name, (unsigned long long)j.id);
        if (nk_tree_push_id(ctx, NK_TREE_NODE, title, NK_MINIMIZED, (int)j.id)) {
            af_joint before = j;
            nk_layout_row_dynamic(ctx, 26, 2);
            nk_bool enabled = j.enabled, collide = j.collide_connected;
            nk_checkbox_label(ctx, "Enabled", &enabled);
            nk_checkbox_label(ctx, "Collide bodies", &collide);
            j.enabled = enabled != 0;
            j.collide_connected = collide != 0;
            label(ctx, "Body B (zero means world)", 22);
            const af_scene_object *other = af_scene_get_object(app->document.scene, j.b);
            nk_layout_row_dynamic(ctx, 28, 1);
            if (nk_combo_begin_label(ctx, other ? other->name : "World", nk_vec2(280, 200))) {
                nk_layout_row_dynamic(ctx, 25, 1);
                if (nk_combo_item_label(ctx, "World", NK_TEXT_LEFT))
                    j.b = 0;
                for (size_t k = 0; k < af_scene_object_count(app->document.scene); ++k) {
                    const af_scene_object *o = af_scene_object_at(app->document.scene, k);
                    if (o->id != j.a && o->collider.enabled && !o->collider.trigger &&
                        o->collider.type != AF_COLLIDER_NONE && nk_combo_item_label(ctx, o->name, NK_TEXT_LEFT))
                        j.b = o->id;
                }
                nk_combo_end(ctx);
            }

            if (!app->runtime.playing) {
                nk_layout_row_dynamic(ctx, 28, 2);
                const char *buttons[] = {"Drag anchor A", "Drag anchor B", "Rotate axis A", "Rotate axis B"};
                for (unsigned part = 0; part < (j.type == AF_JOINT_HINGE ? 4u : 2u); ++part)
                    if (nk_button_label(ctx, buttons[part])) {
                        ed_document_end_edit(&app->document);
                        app->edit_joint = j.id;
                        app->edit_part = 0;
                        app->joint_part = (ed_joint_part)part;
                        app->show_physics = true;
                        app->gizmo.dragging = false;
                    }
                if (app->edit_joint == j.id) {
                    nk_layout_row_dynamic(ctx, 24, 1);
                    label(ctx,
                          app->joint_part < ED_JOINT_AXIS_A ? "Drag an axis handle; Snap uses 0.5 units."
                                                            : "Drag a rotation ring; Snap uses 15 degrees.",
                          36);
                    nk_layout_row_dynamic(ctx, 26, 1);
                    if (nk_button_label(ctx, "Finish joint edit")) {
                        app->edit_joint = 0;
                        ed_document_end_edit(&app->document);
                    }
                }
            }
            vector_fields(ctx, "Anchor A (body units)", &j.anchor_a, .1f, .01f);
            vector_fields(ctx, j.b ? "Anchor B (body units)" : "World anchor B", &j.anchor_b, .1f, .01f);
            if (j.type == AF_JOINT_DISTANCE) {
                nk_layout_row_dynamic(ctx, 28, 1);
                nk_property_float(ctx, "Distance", 0, &j.distance, 10000, .1f, .01f);
            } else {
                bool axes = vector_fields(ctx, "Axis A", &j.axis_a, .1f, .01f);
                axes |= vector_fields(ctx, "Axis B", &j.axis_b, .1f, .01f);
                axes |= vector_fields(ctx, "Zero reference A", &j.reference_a, .1f, .01f);
                axes |= vector_fields(ctx, "Zero reference B", &j.reference_b, .1f, .01f);
                if (axes) {
                    j.axis_a = af_vec3_normalize(j.axis_a);
                    j.axis_b = af_vec3_normalize(j.axis_b);
                    j.reference_a = af_vec3_normalize(
                        af_vec3_sub(j.reference_a, af_vec3_scale(j.axis_a, af_vec3_dot(j.axis_a, j.reference_a))));
                    j.reference_b = af_vec3_normalize(
                        af_vec3_sub(j.reference_b, af_vec3_scale(j.axis_b, af_vec3_dot(j.axis_b, j.reference_b))));
                }
                nk_bool limits = j.limits_enabled;
                nk_layout_row_dynamic(ctx, 26, 1);
                nk_checkbox_label(ctx, "Angular limits", &limits);
                j.limits_enabled = limits != 0;
                float low = j.lower_limit * 180 / AF_PI, high = j.upper_limit * 180 / AF_PI;
                nk_layout_row_dynamic(ctx, 28, 2);
                if (nk_property_float(ctx, "Min deg", -180, &low, high, 5, .5f))
                    j.lower_limit = low * AF_PI / 180;
                if (nk_property_float(ctx, "Max deg", low, &high, 180, 5, .5f))
                    j.upper_limit = high * AF_PI / 180;
            }

            if (j.type == AF_JOINT_HINGE) {
                nk_bool motor = j.motor_enabled;
                nk_layout_row_dynamic(ctx, 26, 1);
                nk_checkbox_label(ctx, "Hinge motor", &motor);
                j.motor_enabled = motor != 0;
                if (j.motor_enabled) {
                    label(ctx, "Relative B-A speed (rad/s)", 24);
                    nk_layout_row_dynamic(ctx, 28, 1);
                    nk_property_float(ctx, "Speed", -100, &j.motor_speed, 100, .1f, .01f);
                    nk_layout_row_dynamic(ctx, 28, 1);
                    nk_property_float(ctx, "Max torque (Nm)", 0, &j.max_motor_torque, 1000000, 1, .1f);
                }
            }
            if (app->runtime.playing) {
                af_physics_joint_state state = af_engine_joint_state(app->engine, j.id);
                if (state.available) {
                    char text[160];
                    snprintf(text, sizeof(text), "%s | error %.4f | angle %.1f deg",
                             state.sleeping ? "Sleeping" : "Awake", state.linear_error, state.angle * 180 / AF_PI);
                    label(ctx, text, 28);
                    snprintf(text, sizeof(text), "Reaction %.2f N | %.2f Nm | motor %.2f Nm", state.force, state.torque,
                             state.motor_torque);
                    label(ctx, text, 28);
                }
            }
            bool remove;
            nk_layout_row_dynamic(ctx, 28, 1);
            remove = nk_button_label(ctx, "Delete joint") != 0;
            nk_tree_pop(ctx);
            if (remove) {
                ed_document_remove_joint(&app->document, j.id);
                return;
            }
            if (memcmp(&before, &j, sizeof(j))) {
                ed_document_joint(&app->document, j);
                return;
            }
        }
    }
}

void body_controls(editor *app, af_scene_object object) {
    struct nk_context *ctx = &app->gui.context;
    af_rigid_body body = object.body;
    bool changed = false;
    label(ctx, "Rigid body", 28);
    nk_layout_row_dynamic(ctx, 26, 1);
    nk_bool enabled = body.enabled;
    if (nk_checkbox_label(ctx, "Enable physics", &enabled)) {
        body.enabled = enabled != 0;
        changed = true;
    }
    if (body.enabled) {
        static const char *types[] = {"Static", "Kinematic", "Dynamic"};
        nk_layout_row_dynamic(ctx, 28, 1);
        int type = nk_combo(ctx, types, 3, (int)body.type, 26, nk_vec2(280, 150));
        if (type != (int)body.type) {
            body.type = (af_body_type)type;
            changed = true;
        }
        float *values[] = {&body.mass,           &body.friction,        &body.restitution,
                           &body.linear_damping, &body.angular_damping, &body.gravity_scale};
        const char *names[] = {"Mass", "Friction", "Bounce", "Linear damping", "Angular damping", "Gravity scale"};
        float lo[] = {.001f, 0, 0, 0, 0, -10}, hi[] = {10000, 2, 1, 20, 20, 10};
        for (unsigned i = 0; i < 6; ++i) {
            nk_layout_row_dynamic(ctx, 28, 1);
            changed |= nk_property_float(ctx, names[i], lo[i], values[i], hi[i], .1f, .01f) != 0;
        }
        nk_layout_row_dynamic(ctx, 26, 1);
        nk_bool sleep = body.allow_sleep;
        if (nk_checkbox_label(ctx, "Allow sleeping", &sleep)) {
            body.allow_sleep = sleep != 0;
            changed = true;
        }
        nk_layout_row_dynamic(ctx, 26, 1);
        nk_bool ccd = body.continuous;
        if (nk_checkbox_label(ctx, "Continuous movement", &ccd)) {
            body.continuous = ccd != 0;
            changed = true;
        }
        float *velocity[] = {&body.velocity.x,         &body.velocity.y,         &body.velocity.z,
                             &body.angular_velocity.x, &body.angular_velocity.y, &body.angular_velocity.z};
        const char *axes[] = {"Velocity X", "Velocity Y", "Velocity Z", "Spin X", "Spin Y", "Spin Z"};
        for (unsigned i = 0; i < 6; ++i) {
            nk_layout_row_dynamic(ctx, 28, 1);
            changed |=
                nk_property_float(ctx, axes[i], i < 3 ? -100 : -50, velocity[i], i < 3 ? 100 : 50, .25f, .025f) != 0;
        }
        if (body.type == AF_BODY_KINEMATIC)
            label(ctx, "Move kinematic poses from C gameplay.", 24);
    }
    if (changed)
        ed_document_body(&app->document, body);
    if (app->runtime.playing) {
        af_physics_state state = af_engine_body_state(app->engine, object.id);
        if (state.available) {
            char text[128];
            snprintf(text, sizeof(text), "%s | speed %.3f | spin %.3f", state.sleeping ? "Sleeping" : "Awake",
                     af_vec3_length(state.velocity), af_vec3_length(state.angular_velocity));
            label(ctx, text, 24);
        }
    }
}

void gameplay_controls(editor *app, af_scene_object object) {
    struct nk_context *ctx = &app->gui.context;
    af_gameplay_component g = object.gameplay;
    bool changed = false;
    label(ctx, "Gameplay components", 30);
    nk_layout_row_dynamic(ctx, 27, 1);
    if (!app->runtime.playing && nk_button_label(ctx, "Edit Interactable Blueprint") &&
        ed_blueprint_attach_interactable(&app->blueprint_state, app->document.scene, object.id)) {
        app->blueprint_mode = true;
        app->landscape_mode = false;
    }
    nk_layout_row_dynamic(ctx, 26, 1);
    nk_bool player = g.player.enabled;
    if (nk_checkbox_label(ctx, "Player controller", &player)) {
        g.player.enabled = player != 0;
        if (player) {
            g.camera.enabled = true;
            g.camera.primary = true;
        }
        changed = true;
    }
    if (g.player.enabled) {
        if (object.parent || object.transform.scale.x != 1 || object.transform.scale.y != 1 ||
            object.transform.scale.z != 1)
            label(ctx, "Player requires root and unit scale.", 24);
        nk_layout_row_dynamic(ctx, 30, 1);
        const af_scene_object *spawn = af_scene_get_object(app->document.scene, g.player.spawn);
        if (nk_combo_begin_label(ctx, spawn ? spawn->name : "Spawn at player position", nk_vec2(290, 180))) {
            nk_layout_row_dynamic(ctx, 26, 1);
            if (nk_combo_item_label(ctx, "Spawn at player position", NK_TEXT_LEFT)) {
                g.player.spawn = 0;
                changed = true;
            }
            for (size_t i = 0; i < af_scene_object_count(app->document.scene); ++i) {
                const af_scene_object *candidate = af_scene_object_at(app->document.scene, i);
                if (candidate->gameplay.role == AF_ROLE_SPAWN &&
                    nk_combo_item_label(ctx, candidate->name, NK_TEXT_LEFT)) {
                    g.player.spawn = candidate->id;
                    changed = true;
                }
            }
            nk_combo_end(ctx);
        }
        nk_layout_row_dynamic(ctx, 30, 1);
        changed |= nk_property_float(ctx, "Walk speed", 0, &g.player.controller.speed, 100, .25f, .025f) != 0;
        nk_layout_row_dynamic(ctx, 30, 1);
        changed |= nk_property_float(ctx, "Gravity", .1f, &g.player.controller.gravity, 1000, .5f, .05f) != 0;
        float jump = g.player.controller.jump_speed * g.player.controller.jump_speed /
                     (2 * fmaxf(.1f, g.player.controller.gravity));
        nk_layout_row_dynamic(ctx, 30, 1);
        if (nk_property_float(ctx, "Jump height", 0, &jump, 20, .1f, .01f)) {
            g.player.controller.jump_speed = sqrtf(2 * g.player.controller.gravity * jump);
            changed = true;
        }
        nk_layout_row_dynamic(ctx, 30, 1);
        changed |= nk_property_float(ctx, "Capsule radius", .05f, &g.player.controller.radius, 10, .05f, .005f) != 0;
        nk_layout_row_dynamic(ctx, 30, 1);
        changed |=
            nk_property_float(ctx, "Capsule half segment", 0, &g.player.controller.half_height, 10, .05f, .005f) != 0;
        nk_layout_row_dynamic(ctx, 30, 1);
        changed |= nk_property_float(ctx, "Mouse sensitivity", .0001f, &g.player.mouse_sensitivity, .05f, .0001f,
                                     .00001f) != 0;
    }
    nk_layout_row_dynamic(ctx, 26, 1);
    nk_bool camera = g.camera.enabled;
    if (nk_checkbox_label(ctx, "Camera", &camera)) {
        g.camera.enabled = camera != 0;
        changed = true;
    }
    if (g.camera.enabled) {
        nk_layout_row_dynamic(ctx, 26, 1);
        nk_bool primary = g.camera.primary;
        if (nk_checkbox_label(ctx, "Primary game camera", &primary)) {
            g.camera.primary = primary != 0;
            changed = true;
        }
        float fov = g.camera.vertical_fov * 180 / AF_PI;
        nk_layout_row_dynamic(ctx, 30, 1);
        if (nk_property_float(ctx, "Field of view", 10, &fov, 150, 1, .1f)) {
            g.camera.vertical_fov = fov * AF_PI / 180;
            changed = true;
        }
        nk_layout_row_dynamic(ctx, 30, 1);
        changed |= nk_property_float(ctx, "Eye height", -10, &g.camera.eye_height, 10, .05f, .005f) != 0;
        nk_layout_row_dynamic(ctx, 30, 1);
        changed |= nk_property_float(ctx, "Near clip", .001f, &g.camera.near_clip, 1, .01f, .001f) != 0;
        nk_layout_row_dynamic(ctx, 30, 1);
        changed |= nk_property_float(ctx, "Far clip", 1, &g.camera.far_clip, 100000, 10, 1) != 0;
    }
    static const char *const roles[] = {"No course role", "Spawn",           "Checkpoint",
                                        "Goal trigger",   "Restart trigger", "Moving platform"};
    label(ctx, "Course role", 24);
    nk_layout_row_dynamic(ctx, 30, 1);
    int role = (int)g.role;
    int next = nk_combo(ctx, roles, AF_ROLE_COUNT, role, 26, nk_vec2(290, 190));
    if (next != role) {
        g.role = (af_gameplay_role)next;
        changed = true;
    }
    if (g.role == AF_ROLE_CHECKPOINT) {
        int order = (int)g.checkpoint_order;
        nk_layout_row_dynamic(ctx, 30, 1);
        if (nk_property_int(ctx, "Checkpoint order", 1, &order, 1024, 1, 1)) {
            g.checkpoint_order = (unsigned)order;
            changed = true;
        }
    }
    if (changed)
        ed_document_gameplay(&app->document, g);
}

void thumbnail_widget(editor *app, const af_scene_asset *asset, const af_material *material) {
    struct nk_context *ctx = &app->gui.context;
    struct nk_rect bounds;
    enum nk_widget_layout_states visibility = nk_widget(&bounds, ctx);
    if (visibility == NK_WIDGET_INVALID)
        return;
    float side = fminf(bounds.w, bounds.h);
    bounds.x += (bounds.w - side) * .5f;
    bounds.w = side;
    bounds.h = side;
    uint32_t texture = ed_thumbnails_get(&app->thumbnails, app->engine, app->document.scene, asset, material);
    struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);
    if (texture) {
        struct nk_image image = nk_image_id((int)texture);
        nk_draw_image(canvas, bounds, &image, nk_rgba(255, 255, 255, 255));
    } else
        nk_fill_rect(canvas, bounds, 4, nk_rgb(48, 45, 57));
    nk_stroke_rect(canvas, bounds, 3, 1, nk_rgb(82, 75, 91));
    if (asset && visibility == NK_WIDGET_VALID && nk_input_is_mouse_hovering_rect(&ctx->input, bounds) &&
        nk_input_is_mouse_pressed(&ctx->input, NK_BUTTON_LEFT))
        snprintf(app->selected_asset, sizeof(app->selected_asset), "%s", asset->name);
}

void asset_entry(editor *app, const af_scene_asset *asset) {
    struct nk_context *ctx = &app->gui.context;
    char title[128];
    snprintf(title, sizeof(title), "%s  /  %s", asset->name,
             asset->kind == AF_ASSET_ANIMATION            ? "animation"
             : asset->kind == AF_ASSET_ENVIRONMENT        ? "HDR environment"
             : asset->kind == AF_ASSET_METALLIC_ROUGHNESS ? "metal/rough"
             : asset->kind == AF_ASSET_AUDIO              ? "audio"
             : asset->kind == AF_ASSET_MESH               ? "mesh"
             : asset->kind == AF_ASSET_NORMAL_MAP         ? "normal map"
                                                          : "color texture");
    nk_layout_row_begin(ctx, NK_DYNAMIC, 64, 2);
    nk_layout_row_push(ctx, .17f);
    thumbnail_widget(app, asset, NULL);
    nk_layout_row_push(ctx, .83f);
    if (nk_select_label(ctx, title, NK_TEXT_LEFT, !strcmp(app->selected_asset, asset->name)))
        snprintf(app->selected_asset, sizeof(app->selected_asset), "%s", asset->name);
    nk_layout_row_end(ctx);
}

void inspector(editor *app, float width, float height) {
    struct nk_context *context = &app->gui.context;
    if (nk_begin(context, "Inspector",
                 nk_rect(width - app->right_sidebar_width, 64, app->right_sidebar_width, height - 96),
                 panel_flags(app))) {
        label(context, "Inspector", 28);
        if (app->blueprint_mode) {
            af_scene *scene = app->runtime.playing ? app->runtime.scene : app->document.scene;
            size_t bp_count = af_scene_blueprint_count(scene);
            label(context, "Blueprint Overview", 24);
            if (bp_count > 0 && app->blueprint_state.active_graph_index < bp_count) {
                const af_blueprint *bp = af_scene_get_blueprint_const(scene, app->blueprint_state.active_graph_index);
                char info[128];
                snprintf(info, sizeof(info), "Graph: %s", bp->name);
                label(context, info, 20);
                snprintf(info, sizeof(info), "Nodes: %zu | Links: %zu", bp->node_count, bp->link_count);
                label(context, info, 20);
                snprintf(info, sizeof(info), "Variables: %zu", bp->var_count);
                label(context, info, 20);
                const af_scene_object *target = af_scene_get_object(scene, bp->bound_object);
                snprintf(info, sizeof(info), "Target: %s", target ? target->name : "Scene Global");
                label(context, info, 20);
                snprintf(info, sizeof(info), "Execution: %s", app->runtime.playing ? "Live Active" : "Editing");
                label(context, info, 20);
            } else {
                label(context, "No active graph.", 20);
            }
            nk_end(context);
            return;
        }
        if (app->landscape_mode) {
            const af_landscape *ls = af_scene_get_landscape(app->document.scene);
            label(context, "Terrain Overview", 24);
            if (ls) {
                char info[128];
                snprintf(info, sizeof(info), "Chunks: %u x %u (%zu total)", ls->chunk_count_x, ls->chunk_count_z,
                         ls->chunk_count);
                label(context, info, 20);
                snprintf(info, sizeof(info), "Vertex spacing: %.2fm", ls->vertex_spacing);
                label(context, info, 20);
                snprintf(info, sizeof(info), "Foliage layers: %zu", ls->foliage_layer_count);
                label(context, info, 20);
                snprintf(info, sizeof(info), "Total foliage: %zu", ls->total_foliage_instances);
                label(context, info, 20);
                nk_layout_row_dynamic(context, 26, 1);
                nk_bool en = ls->enabled;
                if (nk_checkbox_label(context, "Landscape Enabled", &en)) {
                    af_landscape *mut_ls = af_scene_get_landscape_mut(app->document.scene);
                    if (mut_ls)
                        mut_ls->enabled = en != 0;
                    ++app->document.revision;
                }
            } else {
                label(context, "No terrain in scene.", 20);
            }
            nk_end(context);
            return;
        }
        if (app->light_selected) {
            local_light_controls(app);
            label(context, "W move / E rotate / R range / F focus", 26);
            nk_end(context);
            return;
        }
        const af_scene_object *found = af_scene_get_object(app->document.scene, app->document.selection);
        if (!found) {
            label(context, "Select an object or light to edit.", 36);
            app->invalid_name = false;
            nk_layout_row_dynamic(context, 32, 1);
            if (nk_button_label(context, "Open Environment"))
                app->show_environment = true;
            if (nk_button_label(context, "Open Post-processing"))
                app->show_post_processing = true;
            if (nk_button_label(context, "Open Assets"))
                app->show_assets = true;
            nk_end(context);
            return;
        } else {
            af_scene_object object = *found;
            if (app->refresh_fields) {
                snprintf(app->name, sizeof(app->name), "%s", object.name);
                snprintf(app->mesh, sizeof(app->mesh), "%s", object.mesh);
                snprintf(app->texture, sizeof(app->texture), "%s", object.material.texture);
                snprintf(app->normal_map, sizeof(app->normal_map), "%s", object.material.normal_map);
                app->refresh_fields = false;
            }
            char id[64];
            snprintf(id, sizeof(id), "Object ID  %" PRIu64, object.id);
            label(context, id, 22);
            if (object.animation.asset[0] || object.model.root) {
                nk_layout_row_dynamic(context, 30, 1);
                if (nk_button_label(context, "Animation clips")) {
                    app->show_animation = true;
                    app->animation_target = object.animation.asset[0] ? object.id : object.model.root;
                }
            }
            label(context, "Name", 23);
            nk_layout_row_dynamic(context, 34, 1);
            nk_edit_string_zero_terminated(context, (nk_flags)NK_EDIT_FIELD | (nk_flags)NK_EDIT_SIG_ENTER, app->name,
                                           sizeof(app->name), nk_filter_default);
            if (app->name[0] && strcmp(app->name, object.name))
                ed_document_name(&app->document, app->name);
            label(context, "Mesh asset", 23);
            nk_layout_row_dynamic(context, 34, 1);
            nk_edit_string_zero_terminated(context, (nk_flags)NK_EDIT_FIELD | (nk_flags)NK_EDIT_SIG_ENTER, app->mesh,
                                           sizeof(app->mesh), nk_filter_default);
            if (app->mesh[0] && strcmp(app->mesh, object.mesh))
                ed_document_mesh(&app->document, app->mesh);
            app->invalid_name = !app->name[0] || !app->mesh[0];
            if (app->invalid_name)
                label(context, "Name and mesh name cannot be empty.", 24);
            if (strcmp(app->mesh, "cube") && strcmp(app->mesh, "__node") &&
                !af_scene_find_asset(app->document.scene, app->mesh))
                label(context, "Unresolved mesh reference.", 24);
            nk_layout_row_dynamic(context, 28, 1);
            nk_bool visible = object.visible;
            if (nk_checkbox_label(context, "Visible", &visible))
                ed_document_visible(&app->document, visible != 0);
            af_transform transform = object.transform;
            bool changed = vector_fields(context, "Position", &transform.position, 0.1f, 0.01f);
            af_vec3 degrees = af_vec3_scale(transform.rotation, 180.0f / AF_PI);
            if (vector_fields(context, "Rotation (degrees)", &degrees, 1, 0.2f)) {
                transform.rotation = af_vec3_scale(degrees, AF_PI / 180);
                changed = true;
            }
            changed |= vector_fields(context, "Scale", &transform.scale, 0.1f, 0.01f);
            if (changed)
                ed_document_transform(&app->document, transform);
            nk_layout_row_dynamic(context, 32, 2);
            if (nk_button_label(context, "Focus"))
                focus_selection(app);
            if (nk_button_label(context, "Reset transform")) {
                ed_document_transform(&app->document, af_transform_identity());
                ed_document_end_edit(&app->document);
            }
            label(context, "Parent (keeps local transform)", 26);
            const af_scene_object *parent = af_scene_get_object(app->document.scene, object.parent);
            nk_layout_row_dynamic(context, 30, 1);
            if (nk_combo_begin_label(context, parent ? parent->name : "Scene root", nk_vec2(290, 240))) {
                nk_layout_row_dynamic(context, 28, 1);
                if (nk_combo_item_label(context, "Scene root", NK_TEXT_LEFT))
                    ed_document_parent(&app->document, AF_OBJECT_NONE);
                for (size_t i = 0; i < af_scene_object_count(app->document.scene); ++i) {
                    af_scene_object candidate = *af_scene_object_at(app->document.scene, i);
                    if (candidate.id == object.id ||
                        af_scene_is_descendant(app->document.scene, candidate.id, object.id))
                        continue;
                    char text[96];
                    snprintf(text, sizeof(text), "%s (#%" PRIu64 ")", candidate.name, candidate.id);
                    if (nk_combo_item_label(context, text, NK_TEXT_LEFT))
                        ed_document_parent(&app->document, candidate.id);
                }
                nk_combo_end(context);
            }
            water_controls(app, object);
            if (!af_scene_get_water(app->document.scene, object.id)) {
                collider_controls(app, object);
                part_controls(app, object);
                body_controls(app, object);
                joint_controls(app, object);
            }
            gameplay_controls(app, object);
            label(context, "Material", 30);
            af_material material = object.material;
            nk_layout_row_dynamic(context, 80, 1);
            thumbnail_widget(app, NULL, &material);
            label(context, "Metallic/roughness texture asset", 22);
            nk_layout_row_dynamic(context, 30, 1);
            char surface[AF_MESH_NAME_CAPACITY];
            snprintf(surface, sizeof(surface), "%s", material.metallic_roughness_map);
            nk_edit_string_zero_terminated(context, NK_EDIT_FIELD, surface, sizeof(surface), nk_filter_default);
            if (strcmp(surface, material.metallic_roughness_map)) {
                snprintf(material.metallic_roughness_map, sizeof(material.metallic_roughness_map), "%s", surface);
                ed_document_material(&app->document, material);
            }
            nk_layout_row_dynamic(context, 30, 3);
            bool material_changed = nk_property_float(context, "#R", 0, &material.base_color.r, 1, 0.05f, 0.005f) != 0;
            material_changed |= nk_property_float(context, "#G", 0, &material.base_color.g, 1, 0.05f, 0.005f) != 0;
            material_changed |= nk_property_float(context, "#B", 0, &material.base_color.b, 1, 0.05f, 0.005f) != 0;
            nk_layout_row_dynamic(context, 26, 1);
            nk_bool pbr = material.physically_based;
            if (nk_checkbox_label(context, "Physically based", &pbr)) {
                material.physically_based = pbr != 0;
                material_changed = true;
            }
            nk_layout_row_dynamic(context, 30, 1);
            material_changed |= nk_property_float(context, "Roughness", 0, &material.roughness, 1, 0.05f, 0.005f) != 0;
            nk_layout_row_dynamic(context, 30, 1);
            material_changed |= nk_property_float(context, "Metallic", 0, &material.metallic, 1, 0.05f, 0.005f) != 0;
            if (material.alpha_mode == AF_ALPHA_BLEND) {
                nk_layout_row_dynamic(context, 30, 1);
                material_changed |= nk_property_float(context, "Index of refraction", 1.0f, &material.ior, 3.0f, 0.05f, 0.01f) != 0;
            }
            label(context, "Emission (linear RGB)", 22);
            nk_layout_row_dynamic(context, 30, 3);
            material_changed |= nk_property_float(context, "#R", 0, &material.emission.x, 1, 0.05f, 0.005f) != 0;
            material_changed |= nk_property_float(context, "#G", 0, &material.emission.y, 1, 0.05f, 0.005f) != 0;
            material_changed |= nk_property_float(context, "#B", 0, &material.emission.z, 1, 0.05f, 0.005f) != 0;
            nk_layout_row_dynamic(context, 30, 1);
            material_changed |=
                nk_property_float(context, "Emission strength", 0, &material.emission_strength, 1000, 0.1f, 0.01f) != 0;
            nk_layout_row_dynamic(context, 26, 1);
            nk_bool unlit = material.unlit;
            if (nk_checkbox_label(context, "Unlit", &unlit)) {
                material.unlit = unlit != 0;
                material_changed = true;
            }
            nk_layout_row_dynamic(context, 26, 1);
            nk_bool casts = material.cast_shadows, receives = material.receive_shadows;
            if (nk_checkbox_label(context, "Cast shadows", &casts)) {
                material.cast_shadows = casts != 0;
                material_changed = true;
            }
            nk_layout_row_dynamic(context, 26, 1);
            if (nk_checkbox_label(context, "Receive shadows", &receives)) {
                material.receive_shadows = receives != 0;
                material_changed = true;
            }
            label(context, "Texture asset (empty = white)", 22);
            nk_layout_row_dynamic(context, 30, 1);
            nk_edit_string_zero_terminated(context, NK_EDIT_FIELD, app->texture, sizeof(app->texture),
                                           nk_filter_default);
            if (strcmp(material.texture, app->texture)) {
                snprintf(material.texture, sizeof(material.texture), "%s", app->texture);
                material_changed = true;
            }
            label(context, "Normal map (linear)", 22);
            nk_layout_row_dynamic(context, 30, 1);
            nk_edit_string_zero_terminated(context, NK_EDIT_FIELD, app->normal_map, sizeof(app->normal_map),
                                           nk_filter_default);
            if (strcmp(material.normal_map, app->normal_map)) {
                snprintf(material.normal_map, sizeof(material.normal_map), "%s", app->normal_map);
                material_changed = true;
            }
            static const char *alpha_modes[] = {"Opaque", "Cutout", "Blended"};
            nk_layout_row_dynamic(context, 28, 1);
            int mode = nk_combo(context, alpha_modes, 3, (int)material.alpha_mode, 26, nk_vec2(270, 140));
            if (mode != (int)material.alpha_mode) {
                material.alpha_mode = (af_alpha_mode)mode;
                material_changed = true;
            }
            nk_layout_row_dynamic(context, 28, 1);
            material_changed |= nk_property_float(context, "Opacity", 0, &material.base_color.a, 1, .05f, .005f) != 0;
            if (material.alpha_mode == AF_ALPHA_MASK) {
                nk_layout_row_dynamic(context, 28, 1);
                material_changed |=
                    nk_property_float(context, "Cutout threshold", 0, &material.alpha_cutoff, 1, .05f, .005f) != 0;
            }
            nk_layout_row_dynamic(context, 30, 1);
            material_changed |=
                nk_property_float(context, "Normal strength", 0, &material.normal_strength, 4, 0.1f, 0.01f) != 0;
            if (material_changed)
                ed_document_material(&app->document, material);
            nk_layout_row_dynamic(context, 30, 1);
            if (nk_button_label(context, "Import texture"))
                start_import(app, AF_ASSET_TEXTURE);
            nk_layout_row_dynamic(context, 30, 1);
            if (nk_button_label(context, "Import normal map"))
                start_import(app, AF_ASSET_NORMAL_MAP);
        }
    }
    nk_end(context);
}
