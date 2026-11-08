/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "panels.h"
#include "inspector.h"
#include "project_tools.h"
#include "asset_browser.h"
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void toolbar(editor *app, float width) {
    struct nk_context *context = &app->gui.context;
    if (nk_begin(context, "Toolbar", nk_rect(0, 0, width, 64),
                 (NK_WINDOW_BORDER | ((app->modal || app->dialog) ? NK_WINDOW_NO_INPUT : 0)) |
                     NK_WINDOW_NO_SCROLLBAR)) {
        bool compact = width < 1160;
        if (app->runtime.playing) {
            const char *maximize =
                app->maximize_play ? (compact ? "Restore" : "Restore (F6)") : (compact ? "Maximize" : "Maximize (F6)");
            nk_layout_row_dynamic(context, 34, 11);
            if (nk_button_label(context, maximize))
                app->maximize_play = !app->maximize_play;
            if (nk_button_label(context, app->runtime.paused ? "Resume" : "Pause"))
                app->runtime.paused = !app->runtime.paused;
            if (nk_button_label(context, "Step 1/60")) {
                app->runtime.paused = true;
                af_engine_set_simulation_paused(app->engine, true);
                af_engine_step_simulation(app->engine);
            }
            if (nk_button_label(context, "Stop (F5)"))
                stop_play(app);
            if (nk_button_label(context, "Reload game"))
                ed_runtime_reload(&app->runtime);
            nk_bool automatic = app->runtime.auto_reload;
            nk_checkbox_label(context, compact ? "Auto" : "Auto reload", &automatic);
            app->runtime.auto_reload = automatic != 0;
            if (nk_button_label(context, "Extensions"))
                app->show_extensions = !app->show_extensions;
            if (nk_button_label(context, "Performance"))
                app->show_performance = !app->show_performance;
            if (nk_button_label(context, "Environment"))
                app->show_environment = !app->show_environment;
            if (nk_button_label(context, "Post-process"))
                app->show_post_processing = !app->show_post_processing;
            if (nk_button_label(context, "Assets"))
                app->show_assets = !app->show_assets;
        } else {
            bool show_title = width >= 1340;
            float basic = compact ? 50 : 60;
            nk_layout_row_begin(context, NK_STATIC, 34, show_title ? 14 : 13);
            nk_layout_row_push(context, compact ? 76 : 120);
            nk_label_colored(context, "Afterlight", NK_TEXT_LEFT, nk_rgb(245, 190, 112));
            nk_layout_row_push(context, basic);
            if (nk_button_label(context, "New"))
                request_action(app, ED_NEW);
            nk_layout_row_push(context, basic);
            if (nk_button_label(context, "Open"))
                request_action(app, ED_OPEN);
            nk_layout_row_push(context, basic);
            if (nk_button_label(context, "Save"))
                save_document(app, false, ED_NONE);
            nk_layout_row_push(context, compact ? 68 : 76);
            if (nk_button_label(context, "Save As"))
                save_document(app, true, ED_NONE);
            nk_layout_row_push(context, basic);
            if (nk_button_label(context, "Undo")) {
                ed_document_undo(&app->document);
                refresh(app);
            }
            nk_layout_row_push(context, basic);
            if (nk_button_label(context, "Redo")) {
                ed_document_redo(&app->document);
                refresh(app);
            }
            nk_layout_row_push(context, compact ? 64 : 74);
            if (nk_button_label(context, "Play"))
                toggle_play(app);
            nk_layout_row_push(context, compact ? 88 : 104);
            if (nk_button_label(context, "Extensions"))
                app->show_extensions = !app->show_extensions;
            nk_layout_row_push(context, compact ? 90 : 108);
            if (nk_button_label(context, "Performance"))
                app->show_performance = !app->show_performance;
            nk_layout_row_push(context, compact ? 90 : 108);
            if (nk_button_label(context, "Environment"))
                app->show_environment = !app->show_environment;
            nk_layout_row_push(context, compact ? 92 : 112);
            if (nk_button_label(context, "Post-process"))
                app->show_post_processing = !app->show_post_processing;
            nk_layout_row_push(context, compact ? 60 : 68);
            if (nk_button_label(context, "Assets"))
                app->show_assets = !app->show_assets;
            if (show_title) {
                nk_layout_row_push(context, width - 1280);
                char title[512];
                snprintf(title, sizeof(title), "%s%s",
                         app->document.path[0] ? ed_path_name(app->document.path) : "Untitled scene",
                         ed_document_dirty(&app->document) ? " *" : "");
                nk_label(context, title, NK_TEXT_RIGHT);
            }
            nk_layout_row_end(context);
        }
    }
    nk_end(context);
}

bool post_processing_controls(struct nk_context *ctx, af_render_settings *render) {
    bool changed = false;
    label(ctx, "Tone mapping & Exposure", 28);
    nk_layout_row_dynamic(ctx, 26, 1);
    nk_bool tone = render->tone_mapping;
    if (nk_checkbox_label(ctx, "Tone mapping", &tone)) {
        render->tone_mapping = tone != 0;
        changed = true;
    }
    if (render->tone_mapping) {
        static const char *tm_modes[] = {"Off", "Reinhard", "ACES (Filmic)", "AgX (Filmic)"};
        nk_layout_row_dynamic(ctx, 28, 1);
        int tm_sel =
            (render->tone_mapper >= 0 && render->tone_mapper <= 3) ? (int)render->tone_mapper : (int)AF_TONE_MAP_ACES;
        int tm = nk_combo(ctx, tm_modes, 4, tm_sel, 26, nk_vec2(280, 150));
        if (tm != (int)render->tone_mapper) {
            render->tone_mapper = (af_tone_mapping)tm;
            changed = true;
        }
    }
    nk_layout_row_dynamic(ctx, 28, 1);
    changed |= nk_property_float(ctx, "Exposure (stops)", -10, &render->exposure, 10, 0.25f, 0.025f) != 0;
    nk_layout_row_dynamic(ctx, 26, 1);
    nk_bool ae = render->auto_exposure;
    if (nk_checkbox_label(ctx, "Auto exposure (adaptation)", &ae)) {
        render->auto_exposure = ae != 0;
        changed = true;
    }
    if (render->auto_exposure) {
        nk_layout_row_dynamic(ctx, 28, 1);
        changed |= nk_property_float(ctx, "Adaptation speed", 0.1f, &render->auto_exposure_speed, 10, 0.1f, 0.01f) != 0;
        nk_layout_row_dynamic(ctx, 28, 1);
        changed |= nk_property_float(ctx, "Min exposure", -10, &render->auto_exposure_min, 10, 0.25f, 0.025f) != 0;
        nk_layout_row_dynamic(ctx, 28, 1);
        changed |= nk_property_float(ctx, "Max exposure", -10, &render->auto_exposure_max, 10, 0.25f, 0.025f) != 0;
    }

    label(ctx, "Bloom", 28);
    nk_layout_row_dynamic(ctx, 26, 1);
    nk_bool bloom = render->bloom;
    if (nk_checkbox_label(ctx, "Bloom", &bloom)) {
        render->bloom = bloom != 0;
        changed = true;
    }
    if (render->bloom) {
        nk_layout_row_dynamic(ctx, 28, 1);
        changed |= nk_property_float(ctx, "Bloom strength", 0, &render->bloom_strength, 5, 0.05f, 0.005f) != 0;
        nk_layout_row_dynamic(ctx, 28, 1);
        changed |= nk_property_float(ctx, "Bloom threshold", 0, &render->bloom_threshold, 100, 0.1f, 0.01f) != 0;
        nk_layout_row_dynamic(ctx, 28, 1);
        changed |= nk_property_float(ctx, "Bloom radius", 0.1f, &render->bloom_radius, 4, 0.1f, 0.01f) != 0;
    }

    label(ctx, "Color grading", 28);
    nk_layout_row_dynamic(ctx, 28, 1);
    changed |= nk_property_float(ctx, "Saturation", 0, &render->saturation, 3, 0.05f, 0.005f) != 0;
    nk_layout_row_dynamic(ctx, 28, 1);
    changed |= nk_property_float(ctx, "Contrast", 0, &render->contrast, 3, 0.05f, 0.005f) != 0;
    nk_layout_row_dynamic(ctx, 28, 3);
    changed |= nk_property_float(ctx, "Filter R", 0, &render->color_filter.x, 2, 0.05f, 0.005f) != 0;
    changed |= nk_property_float(ctx, "Filter G", 0, &render->color_filter.y, 2, 0.05f, 0.005f) != 0;
    changed |= nk_property_float(ctx, "Filter B", 0, &render->color_filter.z, 2, 0.05f, 0.005f) != 0;

    label(ctx, "Depth of field", 28);
    nk_layout_row_dynamic(ctx, 26, 1);
    nk_bool dof = render->depth_of_field;
    if (nk_checkbox_label(ctx, "Depth of field", &dof)) {
        render->depth_of_field = dof != 0;
        changed = true;
    }
    if (render->depth_of_field) {
        nk_layout_row_dynamic(ctx, 28, 1);
        changed |=
            nk_property_float(ctx, "Focus distance (m)", 0.1f, &render->dof_focus_distance, 500, 0.5f, 0.05f) != 0;
        nk_layout_row_dynamic(ctx, 28, 1);
        changed |= nk_property_float(ctx, "Focus range (m)", 0.1f, &render->dof_focus_range, 100, 0.25f, 0.025f) != 0;
        nk_layout_row_dynamic(ctx, 28, 1);
        changed |= nk_property_float(ctx, "Blur radius", 0.001f, &render->dof_blur_radius, 0.05f, 0.001f, 0.0001f) != 0;
    }

    label(ctx, "Motion blur", 28);
    nk_layout_row_dynamic(ctx, 26, 1);
    nk_bool mb = render->motion_blur;
    if (nk_checkbox_label(ctx, "Motion blur", &mb)) {
        render->motion_blur = mb != 0;
        changed = true;
    }
    if (render->motion_blur) {
        nk_layout_row_dynamic(ctx, 28, 1);
        changed |= nk_property_float(ctx, "Shutter strength", 0, &render->motion_blur_strength, 5, 0.05f, 0.005f) != 0;
    }

    label(ctx, "Lens effects", 28);
    nk_layout_row_dynamic(ctx, 26, 1);
    nk_bool vig = render->vignette;
    if (nk_checkbox_label(ctx, "Vignette", &vig)) {
        render->vignette = vig != 0;
        changed = true;
    }
    if (render->vignette) {
        nk_layout_row_dynamic(ctx, 28, 1);
        changed |= nk_property_float(ctx, "Vignette intensity", 0, &render->vignette_intensity, 2, 0.05f, 0.005f) != 0;
        nk_layout_row_dynamic(ctx, 28, 1);
        changed |=
            nk_property_float(ctx, "Vignette smoothness", 0.01f, &render->vignette_smoothness, 1, 0.05f, 0.005f) != 0;
    }

    nk_layout_row_dynamic(ctx, 26, 1);
    nk_bool ca = render->chromatic_aberration;
    if (nk_checkbox_label(ctx, "Chromatic aberration", &ca)) {
        render->chromatic_aberration = ca != 0;
        changed = true;
    }
    if (render->chromatic_aberration) {
        nk_layout_row_dynamic(ctx, 28, 1);
        changed |= nk_property_float(ctx, "Aberration strength", 0, &render->chromatic_aberration_strength, 0.05f,
                                     0.001f, 0.0001f) != 0;
    }

    label(ctx, "Film grain", 28);
    nk_layout_row_dynamic(ctx, 26, 1);
    nk_bool fg = render->film_grain;
    if (nk_checkbox_label(ctx, "Film grain", &fg)) {
        render->film_grain = fg != 0;
        changed = true;
    }
    if (render->film_grain) {
        nk_layout_row_dynamic(ctx, 28, 1);
        changed |=
            nk_property_float(ctx, "Grain intensity", 0, &render->film_grain_intensity, 0.2f, 0.005f, 0.001f) != 0;
    }
    return changed;
}

void environment_panel(editor *app, float width, float height) {
    if (!app->show_environment)
        return;
    struct nk_context *ctx = &app->gui.context;
    nk_flags flags = NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_SCALABLE;
    if (app->modal || app->dialog)
        flags |= NK_WINDOW_NO_INPUT;
    else
        flags |= NK_WINDOW_MOVABLE;
    if (nk_begin(ctx, "Environment",
                 nk_rect(fmaxf(20, (width - 600) * .5f), 60, fminf(600, width - 40), fminf(760, height - 80)), flags)) {
        af_scene *scene = app->runtime.playing ? app->runtime.scene : app->document.scene;
        af_directional_light sun = af_scene_get_light(scene);
        af_render_settings render = af_scene_get_render_settings(scene);
        bool changed = false, preset = false;
        label(ctx,
              app->runtime.playing ? "Live preview: changes reset when Play stops."
                                   : "Scene environment: changes support Undo and Save.",
              28);
        nk_layout_row_dynamic(ctx, 32, 2);
        if (nk_button_label(ctx, "Day preset")) {
            ed_environment_preset(scene, false, &sun, &render);
            changed = preset = true;
        }
        if (nk_button_label(ctx, "Night preset")) {
            ed_environment_preset(scene, true, &sun, &render);
            changed = preset = true;
        }
        label(ctx, "Anti-aliasing", 28);
        static const char *aa_modes[] = {"Off", "FXAA", "SMAA", "TAA"};
        nk_layout_row_dynamic(ctx, 28, 1);
        int aa = nk_combo(ctx, aa_modes, 4, (int)render.anti_aliasing, 26, nk_vec2(280, 150));
        if (aa != (int)render.anti_aliasing) {
            render.anti_aliasing = (af_anti_aliasing)aa;
            changed = true;
        }
        if (render.anti_aliasing == AF_AA_TAA) {
            nk_layout_row_dynamic(ctx, 28, 1);
            changed |= nk_property_float(ctx, "History weight", 0, &render.taa_history_weight, .98f, .05f, .005f) != 0;
        }
        if (render.anti_aliasing != AF_AA_OFF) {
            nk_layout_row_dynamic(ctx, 28, 1);
            changed |= nk_property_float(ctx, "AA sharpening", 0, &render.aa_sharpness, 1, .05f, .005f) != 0;
        }
        nk_layout_row_dynamic(ctx, 28, 1);
        nk_bool ray_shadows = render.ray_traced_shadows;
        if (nk_checkbox_label(ctx, "Ray-traced shadows (all lights)", &ray_shadows)) {
            render.ray_traced_shadows = ray_shadows != 0;
            changed = true;
        }
        nk_bool ray_reflections = render.ray_traced_water_reflections;
        nk_layout_row_dynamic(ctx, 28, 1);
        if (nk_checkbox_label(ctx, "Ray-traced water reflections", &ray_reflections)) {
            render.ray_traced_water_reflections = ray_reflections != 0;
            changed = true;
        }
        nk_bool ray_gi = render.ray_traced_gi;
        nk_layout_row_dynamic(ctx, 28, 1);
        if (nk_checkbox_label(ctx, "Ray-traced global illumination (RTGI)", &ray_gi)) {
            render.ray_traced_gi = ray_gi != 0;
            changed = true;
        }
        if (render.ray_traced_gi) {
            nk_layout_row_dynamic(ctx, 28, 2);
            changed |= nk_property_float(ctx, "RTGI intensity", 0.0f, &render.rtgi_intensity, 5.0f, 0.1f, 0.01f) != 0;
            changed |= nk_property_float(ctx, "Max distance", 1.0f, &render.rtgi_ray_distance, 100.0f, 1.0f, 0.1f) != 0;
            nk_layout_row_dynamic(ctx, 28, 1);
            int samples = (int)render.rtgi_samples;
            if (nk_property_int(ctx, "RTGI samples", 1, &samples, 4, 1, 1)) {
                render.rtgi_samples = (uint32_t)samples;
                changed = true;
            }
        }
        af_engine_stats ray_stats = af_engine_get_stats(app->engine);
        label(ctx,
              ray_stats.ray_query_supported ? "Hardware ray queries active (sun, point and spot lights)."
                                            : "Hardware ray queries unavailable: using raster shadows.",
              38);
        if (ray_stats.ray_tracing_failed)
            label(ctx, "Ray resources unavailable: raster fallback active.", 28);
        label(ctx, "Sun / directional light", 28);
        float heading = atan2f(-sun.direction.z, -sun.direction.x) * 180 / AF_PI;
        float elevation = atan2f(-sun.direction.y, hypotf(sun.direction.x, sun.direction.z)) * 180 / AF_PI;
        float old_heading = heading, old_elevation = elevation;
        nk_layout_row_dynamic(ctx, 28, 1);
        nk_property_float(ctx, "Sun heading (degrees)", -180, &heading, 180, 5, .5f);
        nk_property_float(ctx, "Sun elevation (degrees)", -90, &elevation, 90, 5, .5f);
        if (heading != old_heading || elevation != old_elevation) {
            float h = heading * AF_PI / 180, e = elevation * AF_PI / 180;
            sun.direction = (af_vec3){-cosf(e) * cosf(h), -sinf(e), -cosf(e) * sinf(h)};
            changed = true;
        }
        nk_layout_row_dynamic(ctx, 28, 3);
        changed |= nk_property_float(ctx, "Red", 0, &sun.color.x, 10, .05f, .005f) != 0;
        changed |= nk_property_float(ctx, "Green", 0, &sun.color.y, 10, .05f, .005f) != 0;
        changed |= nk_property_float(ctx, "Blue", 0, &sun.color.z, 10, .05f, .005f) != 0;
        nk_layout_row_dynamic(ctx, 28, 1);
        changed |= nk_property_float(ctx, "Sun intensity", 0, &sun.intensity, 100, .1f, .01f) != 0;
        changed |= nk_property_float(ctx, "Ambient light", 0, &sun.ambient, 10, .02f, .002f) != 0;
        label(ctx, "Skybox and environment lighting", 28);
        nk_layout_row_dynamic(ctx, 26, 1);
        nk_bool sky = render.skybox;
        if (nk_checkbox_label(ctx, "Show skybox", &sky)) {
            render.skybox = sky != 0;
            changed = true;
        }
        nk_layout_row_dynamic(ctx, 28, 1);
        if (nk_combo_begin_label(ctx, render.environment_asset[0] ? render.environment_asset : "Procedural sky",
                                 nk_vec2(420, 180))) {
            nk_layout_row_dynamic(ctx, 26, 1);
            if (nk_combo_item_label(ctx, "Procedural sky", NK_TEXT_LEFT)) {
                render.environment_asset[0] = 0;
                changed = true;
            }
            for (size_t i = 0; i < af_scene_asset_count(scene); ++i) {
                const af_scene_asset *asset = af_scene_asset_at(scene, i);
                if (asset->kind == AF_ASSET_ENVIRONMENT && nk_combo_item_label(ctx, asset->name, NK_TEXT_LEFT)) {
                    snprintf(render.environment_asset, sizeof(render.environment_asset), "%s", asset->name);
                    changed = true;
                }
            }
            nk_combo_end(ctx);
        }
        if (!render.environment_asset[0]) {
            static const char *profiles[] = {"Studio", "Sunset", "Overcast"};
            nk_layout_row_dynamic(ctx, 28, 1);
            int profile = nk_combo(ctx, profiles, 3, (int)render.environment_profile, 26, nk_vec2(280, 110));
            if (profile != (int)render.environment_profile) {
                render.environment_profile = (uint32_t)profile;
                changed = true;
            }
        }
        nk_layout_row_dynamic(ctx, 28, 1);
        changed |= nk_property_float(ctx, "Sky brightness", 0, &render.skybox_intensity, 100, .1f, .01f) != 0;
        float rotation = remainderf(render.environment_rotation * 180 / AF_PI, 360), old_rotation = rotation;
        nk_property_float(ctx, "Sky rotation (degrees)", -180, &rotation, 180, 5, .5f);
        if (rotation != old_rotation) {
            render.environment_rotation = rotation * AF_PI / 180;
            changed = true;
        }
        nk_layout_row_dynamic(ctx, 26, 1);
        nk_bool lighting = render.environment_enabled;
        if (nk_checkbox_label(ctx, "Environment lights objects", &lighting)) {
            render.environment_enabled = lighting != 0;
            changed = true;
        }
        nk_layout_row_dynamic(ctx, 28, 1);
        changed |=
            nk_property_float(ctx, "Environment intensity", 0, &render.environment_intensity, 100, .1f, .01f) != 0;
        if (!app->runtime.playing && nk_button_label(ctx, "Import HDR skybox"))
            start_import(app, AF_ASSET_ENVIRONMENT);
        label(ctx, "Wind", 28);
        af_wind wind = af_scene_get_wind(scene), wind_before = wind;
        nk_layout_row_dynamic(ctx, 26, 1);
        nk_bool wind_enabled = wind.enabled;
        if (nk_checkbox_label(ctx, "Enable world wind", &wind_enabled))
            wind.enabled = wind_enabled != 0;
        float wind_heading = atan2f(wind.direction.z, wind.direction.x) * 180 / AF_PI, old_wind_heading = wind_heading;
        nk_layout_row_dynamic(ctx, 28, 1);
        nk_property_float(ctx, "Wind direction (degrees)", -180, &wind_heading, 180, 5, .5f);
        if (wind_heading != old_wind_heading)
            wind.direction = (af_vec3){cosf(wind_heading * AF_PI / 180), 0, sinf(wind_heading * AF_PI / 180)};
        nk_property_float(ctx, "Wind speed (m/s)", 0, &wind.speed, 60, .5f, .05f);
        nk_property_float(ctx, "Gust strength", 0, &wind.gust_strength, 1, .05f, .005f);
        nk_property_float(ctx, "Gust frequency (Hz)", 0, &wind.gust_frequency, 2, .05f, .005f);
        nk_property_float(ctx, "Air density (kg/m3)", .01f, &wind.air_density, 10, .1f, .01f);
        if (memcmp(&wind, &wind_before, sizeof(wind))) {
            if (app->runtime.playing)
                af_scene_set_wind(scene, wind);
            else
                ed_document_wind(&app->document, wind);
        }
        label(ctx, "Shadows", 28);
        af_shadow_settings shadows = af_scene_get_shadows(scene), shadows_before = shadows;
        nk_layout_row_dynamic(ctx, 26, 1);
        nk_bool shadow_on = shadows.enabled;
        if (nk_checkbox_label(ctx, "Sun shadows enabled", &shadow_on))
            shadows.enabled = shadow_on != 0;
        nk_layout_row_dynamic(ctx, 30, 1);
        static const char *smodes[] = {"Single map (simple)", "4 cascades"};
        int smode = shadows.mode == AF_SHADOW_CASCADED ? 1 : 0,
            next_smode = nk_combo(ctx, smodes, 2, smode, 28, nk_vec2(290, 90));
        if (next_smode != smode)
            shadows.mode = next_smode ? AF_SHADOW_CASCADED : AF_SHADOW_SINGLE;
        label(ctx, "Resolution per map", 22);
        nk_layout_row_dynamic(ctx, 30, 1);
        static const char *ssizes[] = {"512", "1024", "2048", "4096"};
        int ssize = shadows.resolution == 512 ? 0 : shadows.resolution == 1024 ? 1 : shadows.resolution == 2048 ? 2 : 3;
        int next_ssize = nk_combo(ctx, ssizes, 4, ssize, 28, nk_vec2(290, 150));
        if (next_ssize != ssize)
            shadows.resolution = 512u << (unsigned)next_ssize;
        nk_layout_row_dynamic(ctx, 30, 1);
        nk_property_float(ctx, "Shadow distance", 1, &shadows.distance, 1000, 1, 0.2f);
        nk_property_float(ctx, "Constant bias", 0, &shadows.constant_bias, 16, 0.1f, 0.01f);
        nk_property_float(ctx, "Slope bias", 0, &shadows.slope_bias, 16, 0.1f, 0.01f);
        nk_property_float(ctx, "Normal offset", 0, &shadows.normal_bias, 1, 0.005f, 0.001f);
        nk_layout_row_dynamic(ctx, 30, 1);
        static const char *sfilters[] = {"Hard edges", "Soft edges", "Softer edges"};
        int sfilter = nk_combo(ctx, sfilters, 3, (int)shadows.filter_radius, 28, nk_vec2(290, 120));
        if (sfilter != (int)shadows.filter_radius)
            shadows.filter_radius = (uint32_t)sfilter;
        if (shadows.mode == AF_SHADOW_CASCADED) {
            nk_layout_row_dynamic(ctx, 30, 1);
            nk_property_float(ctx, "Near detail", 0, &shadows.split_lambda, 1, 0.05f, 0.005f);
            nk_property_float(ctx, "Cascade blend", 0, &shadows.blend_fraction, 0.3f, 0.01f, 0.001f);
            nk_layout_row_dynamic(ctx, 26, 1);
            nk_bool sdebug = shadows.debug_cascades;
            if (nk_checkbox_label(ctx, "Show cascade regions", &sdebug))
                shadows.debug_cascades = sdebug != 0;
        }
        if (memcmp(&shadows, &shadows_before, sizeof(shadows))) {
            if (app->runtime.playing)
                af_scene_set_shadows(scene, shadows);
            else
                ed_document_shadows(&app->document, shadows);
        }
        label(ctx, "Local shadow quality", 28);
        static const char *local_resolutions[] = {"256 px / face", "512 px / face", "1024 px / face"};
        int local_quality = render.local_shadow_resolution == 256 ? 0 : render.local_shadow_resolution == 1024 ? 2 : 1;
        nk_layout_row_dynamic(ctx, 30, 1);
        int next_quality = nk_combo(ctx, local_resolutions, 3, local_quality, 26, nk_vec2(290, 120));
        if (next_quality != local_quality) {
            render.local_shadow_resolution = 256u << (unsigned)next_quality;
            changed = true;
        }
        label(ctx, "Water scene effects", 28);
        nk_layout_row_dynamic(ctx, 26, 1);
        nk_bool refract = render.refraction;
        if (nk_checkbox_label(ctx, "Water scene refraction", &refract)) {
            render.refraction = refract != 0;
            changed = true;
        }
        if (render.refraction) {
            nk_layout_row_dynamic(ctx, 30, 1);
            changed |=
                nk_property_float(ctx, "Refraction strength", 0, &render.refraction_strength, 2, .05f, .01f) != 0;
        }
        nk_layout_row_dynamic(ctx, 26, 1);
        nk_bool submerged = render.underwater;
        if (nk_checkbox_label(ctx, "Underwater absorption", &submerged)) {
            render.underwater = submerged != 0;
            changed = true;
        }
        if (changed) {
            if (app->runtime.playing) {
                af_scene_set_light(scene, sun);
                af_scene_set_render_settings(scene, render);
            } else {
                if (preset)
                    ed_document_end_edit(&app->document);
                ed_document_environment(&app->document, sun, render);
                if (preset)
                    ed_document_end_edit(&app->document);
            }
        }
        nk_layout_row_dynamic(ctx, 30, 1);
        if (nk_button_label(ctx, "Close environment"))
            app->show_environment = false;
    }
    nk_end(ctx);
}

void post_processing_panel(editor *app, float width, float height) {
    if (!app->show_post_processing)
        return;
    struct nk_context *ctx = &app->gui.context;
    nk_flags flags = NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_SCALABLE;
    if (app->modal || app->dialog)
        flags |= NK_WINDOW_NO_INPUT;
    else
        flags |= NK_WINDOW_MOVABLE;
    if (nk_begin(ctx, "Post-processing",
                 nk_rect(fmaxf(20, (width - 560) * .5f), 70, fminf(560, width - 40), fminf(720, height - 90)), flags)) {
        af_scene *scene = app->runtime.playing ? app->runtime.scene : app->document.scene;
        af_render_settings render = af_scene_get_render_settings(scene);
        label(ctx,
              app->runtime.playing ? "Live preview: changes reset when Play stops."
                                   : "Scene post-processing: changes support Undo and Save.",
              28);
        if (post_processing_controls(ctx, &render)) {
            if (app->runtime.playing)
                af_scene_set_render_settings(scene, render);
            else
                ed_document_render_settings(&app->document, render);
        }
        nk_layout_row_dynamic(ctx, 30, 1);
        if (nk_button_label(ctx, "Close post-processing"))
            app->show_post_processing = false;
    }
    nk_end(ctx);
}

void performance_panel(editor *app, float width, float height) {
    if (!app->show_performance)
        return;
    struct nk_context *ctx = &app->gui.context;
    af_engine_stats stats = af_engine_get_stats(app->engine);
    char text[256];
    if (nk_begin(ctx, "Performance", nk_rect(fmaxf(20, width - 760), 110, 420, fminf(540, height - 160)),
                 NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE)) {
        snprintf(text, sizeof(text), "%.1f FPS / %.2f ms (paced)", stats.frames_per_second, stats.average_frame_ms);
        label(ctx, text, 26);
        snprintf(text, sizeof(text), "CPU events %.3f ms", stats.cpu_events_ms);
        label(ctx, text, 24);
        snprintf(text, sizeof(text), "CPU update %.3f ms", stats.cpu_update_ms);
        label(ctx, text, 24);
        af_physics_stats physics = af_engine_physics_stats(app->engine);
        snprintf(text, sizeof(text), "Physics %.3f ms | dynamic %zu | sleeping %zu", stats.cpu_physics_ms,
                 physics.dynamic_bodies, physics.sleeping);
        label(ctx, text, 24);
        snprintf(text, sizeof(text), "Contacts %zu | pairs %zu | substeps %u | CCD %u", physics.contacts,
                 physics.broadphase_pairs, physics.substeps, physics.ccd_hits);
        label(ctx, text, 24);
        snprintf(text, sizeof(text), "Islands %zu | awake %zu | largest %zu | CCD clamps %u", physics.islands,
                 physics.awake_islands, physics.largest_island, physics.ccd_clamped);
        label(ctx, text, 24);
        snprintf(text, sizeof(text), "Sync %.3f | broad %.3f | narrow %.3f ms", physics.sync_ms, physics.broadphase_ms,
                 physics.narrowphase_ms);
        label(ctx, text, 24);
        snprintf(text, sizeof(text), "CCD %.3f | solve %.3f ms | cache probes %zu", physics.ccd_ms, physics.solver_ms,
                 physics.cache_probes);
        label(ctx, text, 24);
        snprintf(text, sizeof(text), "Joint error %.4f | peak %.2f N / %.2f Nm", physics.max_joint_error,
                 physics.max_joint_force, physics.max_joint_torque);
        label(ctx, text, 24);
        if (app->runtime.playing && nk_button_label(ctx, "Wake selected body"))
            af_engine_wake_body(app->engine, app->document.selection);
        snprintf(text, sizeof(text), "CPU animation %.3f ms", stats.cpu_animation_ms);
        label(ctx, text, 24);
        snprintf(text, sizeof(text), "CPU submit/wait %.3f ms", stats.cpu_render_ms);
        label(ctx, text, 24);
        snprintf(text, sizeof(text), "Shadow passes %u / reused %u", stats.shadow_passes, stats.shadow_cached_passes);
        label(ctx, text, 24);
        if (stats.gpu_timing_valid) {
            snprintf(text, sizeof(text), "GPU frame %.3f ms", stats.gpu_frame_ms);
            label(ctx, text, 28);
            snprintf(text, sizeof(text), "Shadows %.3f / Scene %.3f ms", stats.gpu_shadow_ms, stats.gpu_scene_ms);
            label(ctx, text, 24);
            snprintf(text, sizeof(text), "Post %.3f / UI %.3f ms", stats.gpu_post_ms, stats.gpu_overlay_ms);
            label(ctx, text, 24);
            snprintf(text, sizeof(text), "CPU fence %.3f / acquire %.3f ms", stats.cpu_fence_ms, stats.cpu_acquire_ms);
            label(ctx, text, 24);
        } else
            label(ctx, stats.gpu_timing_supported ? "GPU sample pending" : "GPU timestamps unavailable", 26);
        snprintf(text, sizeof(text), "Simulation %" PRIu64 " ticks / %u steps / %.3f alpha", stats.simulation_ticks,
                 stats.simulation_steps, stats.simulation_alpha);
        label(ctx, text, 24);
        snprintf(text, sizeof(text), "Dropped catch-up %.3f s", stats.simulation_dropped_seconds);
        label(ctx, text, 24);
        static const char *views[] = {"Shaded", "Depth (log scale)", "Motion vectors", "Previous history",
                                      "History rejection"};
        nk_layout_row_dynamic(ctx, 28, 1);
        int view = nk_combo(ctx, views, 5, (int)stats.debug_view, 26, nk_vec2(320, 190));
        if (view != (int)stats.debug_view)
            af_engine_set_debug_view(app->engine, (af_debug_view)view);
        snprintf(text, sizeof(text), "Ray shadows: %s / %zu instances / %zu meshes",
                 stats.ray_traced_shadows_active ? "active"
                 : stats.ray_query_supported     ? "off"
                                                 : "unsupported",
                 stats.ray_instances, stats.ray_meshes);
        label(ctx, text, 26);
        snprintf(text, sizeof(text), "Ray water reflections: %s",
                 stats.ray_traced_water_reflections_active ? "active"
                 : stats.ray_query_supported               ? "off"
                                                           : "unsupported");
        label(ctx, text, 26);
        snprintf(text, sizeof(text), "Ray-traced GI: %s",
                 stats.ray_traced_gi_active  ? "active"
                 : stats.ray_query_supported ? "off"
                                             : "unsupported");
        label(ctx, text, 26);
        snprintf(text, sizeof(text), "Ray AS %.2f MiB / build %.3f ms / %zu raster casters",
                 (double)stats.ray_tracing_bytes / 1048576, stats.gpu_ray_build_ms, stats.ray_raster_casters);
        label(ctx, text, 26);
        snprintf(text, sizeof(text), "Visible %zu / culled %zu", stats.visible_objects, stats.culled_objects);
        label(ctx, text, 24);
        snprintf(text, sizeof(text), "Tracked GPU %.1f / %.0f MiB%s", (double)stats.tracked_gpu_bytes / 1048576,
                 (double)stats.gpu_budget_bytes / 1048576, stats.gpu_memory_over_budget ? " OVER BUDGET" : "");
        label(ctx, text, 24);
        snprintf(text, sizeof(text), "Post %.1f / shadows %.1f / refraction %.1f MiB",
                 (double)stats.post_bytes / 1048576, (double)stats.shadow_bytes / 1048576,
                 (double)stats.refraction_bytes / 1048576);
        label(ctx, text, 24);
        snprintf(text, sizeof(text), "GPU target %.2f ms%s", stats.gpu_budget_ms,
                 stats.gpu_time_over_budget ? " OVER BUDGET" : "");
        label(ctx, text, 24);
        label(ctx, "Tracked bytes exclude UI and driver/presentation allocations.", 38);
        snprintf(text, sizeof(text), "Draws %zu + %zu shadow", stats.draw_calls, stats.shadow_draw_calls);
        label(ctx, text, 26);
        snprintf(text, sizeof(text), "Textures %zu / %.2f MiB / %u mip levels", stats.loaded_textures,
                 (double)stats.texture_bytes / 1048576, stats.texture_mip_levels);
        label(ctx, text, 24);
        snprintf(text, sizeof(text), "Anisotropy %.0fx / Shadow %u x %u", stats.texture_anisotropy,
                 stats.shadow_resolution, stats.shadow_cascades);
        label(ctx, text, 24);
        snprintf(text, sizeof(text), "Missing meshes %zu / textures %zu", stats.missing_meshes, stats.missing_textures);
        label(ctx, text, 24);
        snprintf(text, sizeof(text), "HDR: %s / %u local lights / %u shadow faces",
                 stats.hdr_enabled ? "RGBA16F" : "unavailable", stats.local_shadow_lights, stats.local_shadow_maps);
        label(ctx, text, 24);
        if (stats.local_shadow_overflow) {
            snprintf(text, sizeof(text), "%u extra local lights render without shadows", stats.local_shadow_overflow);
            label(ctx, text, 24);
        }
        label(ctx, "GPU samples are delayed; presentation is excluded.", 24);
        nk_layout_row_dynamic(ctx, 30, 1);
        if (nk_button_label(ctx, "Close performance"))
            app->show_performance = false;
    }
    nk_end(ctx);
}

void asset_browser_panel(editor *app, float width, float height) {
    if (!app->show_assets)
        return;
    struct nk_context *ctx = &app->gui.context;
    nk_flags flags = NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE;
    if (app->runtime.playing || app->modal || app->dialog)
        flags |= NK_WINDOW_ROM;
    if (nk_begin(ctx, "Assets", nk_rect(270, 120, fminf(620, width - 310), fminf(650, height - 170)), flags)) {
        nk_layout_row_dynamic(ctx, 26, 1);
        nk_bool watch = app->watch_assets;
        nk_checkbox_label(ctx, "Reload changed assets", &watch);
        app->watch_assets = watch != 0;
        label(ctx, "Search names or paths", 22);
        nk_layout_row_dynamic(ctx, 30, 1);
        nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, app->asset_search, sizeof(app->asset_search),
                                       nk_filter_default);
        static const char *filters[] = {"All assets", "Meshes",           "Color textures",   "Normal maps",
                                        "Audio",      "Metal/rough maps", "HDR environments", "Animations"};
        nk_layout_row_dynamic(ctx, 30, 1);
        app->asset_filter = nk_combo(ctx, filters, 8, app->asset_filter, 26, nk_vec2(220, 150));
        nk_layout_row_dynamic(ctx, 30, 3);
        if (nk_button_label(ctx, "Import mesh"))
            start_import(app, AF_ASSET_MESH);
        if (nk_button_label(ctx, "Import texture"))
            start_import(app, AF_ASSET_TEXTURE);
        if (nk_button_label(ctx, "Import normal"))
            start_import(app, AF_ASSET_NORMAL_MAP);
        nk_layout_row_dynamic(ctx, 28, 2);
        if (nk_button_label(ctx, "Import WAV sound"))
            start_import(app, AF_ASSET_AUDIO);
        if (nk_button_label(ctx, "Import metal/rough"))
            start_import(app, AF_ASSET_METALLIC_ROUGHNESS);
        nk_layout_row_dynamic(ctx, 30, 1);
        if (nk_button_label(ctx, "Import HDR panorama"))
            start_import(app, AF_ASSET_ENVIRONMENT);
        size_t matches = 0;
        nk_layout_row_dynamic(ctx, 240, 1);
        if (nk_group_begin(ctx, "Asset results", NK_WINDOW_BORDER)) {
            af_scene_asset builtin = {.name = "cube", .path = "Built-in mesh", .kind = AF_ASSET_MESH};
            if (!af_scene_find_asset(app->document.scene, "cube") &&
                ed_asset_matches(&builtin, app->asset_search, app->asset_filter - 1)) {
                asset_entry(app, &builtin);
                ++matches;
            }
            for (size_t i = 0; i < af_scene_asset_count(app->document.scene); ++i) {
                const af_scene_asset *asset = af_scene_asset_at(app->document.scene, i);
                if (!ed_asset_matches(asset, app->asset_search, app->asset_filter - 1))
                    continue;
                ++matches;
                asset_entry(app, asset);
            }
            if (!matches)
                label(ctx, "No matching assets.", 28);
            nk_group_end(ctx);
        }
        char count[80];
        snprintf(count, sizeof(count), "%zu matches / %zu imported assets", matches,
                 af_scene_asset_count(app->document.scene));
        label(ctx, count, 24);
        const af_scene_asset *selected = af_scene_find_asset(app->document.scene, app->selected_asset);
        bool builtin = !strcmp(app->selected_asset, "cube") && !selected;
        if (selected || builtin) {
            af_scene_asset asset =
                selected ? *selected : (af_scene_asset){.name = "cube", .path = "Built-in mesh", .kind = AF_ASSET_MESH};
            nk_layout_row_dynamic(ctx, 46, 1);
            nk_label_wrap(ctx, asset.path);
            af_texture_info info;
            if (af_engine_texture_info(app->engine, asset.name, &info)) {
                char text[160];
                snprintf(text, sizeof(text), "%u x %u / %u mips / %.2f MiB / %.0fx / %s", info.width, info.height,
                         info.mip_levels, (double)info.allocation_bytes / 1048576, info.anisotropy,
                         info.srgb ? "sRGB" : "linear");
                label(ctx, text, 24);
            }
            if (asset.kind == AF_ASSET_AUDIO) {
                nk_layout_row_dynamic(ctx, 30, 1);
                if (nk_button_label(ctx, "Preview sound"))
                    af_engine_play_sound(app->engine, asset.name, .5f);
            } else if (asset.kind == AF_ASSET_ANIMATION) {
                label(ctx, "Clip data for the matching imported model.", 26);
            } else if (asset.kind == AF_ASSET_ENVIRONMENT) {
                nk_layout_row_dynamic(ctx, 30, 1);
                if (nk_button_label(ctx, "Use environment lighting"))
                    ed_document_assign_asset(&app->document, asset.name);
            } else if (af_scene_get_object(app->document.scene, app->document.selection)) {
                nk_layout_row_dynamic(ctx, 30, 1);
                if (nk_button_label(ctx, "Assign to selected object")) {
                    if (ed_document_assign_asset(&app->document, asset.name))
                        refresh(app);
                }
            } else
                label(ctx, "Select an object to assign this asset.", 24);
            if (asset.kind == AF_ASSET_MESH) {
                nk_layout_row_dynamic(ctx, 30, 1);
                if (nk_button_label(ctx, "Create mesh object")) {
                    ed_document_create_mesh_object(&app->document, asset.name);
                    refresh(app);
                }
            }
            if (!builtin) {
                nk_layout_row_dynamic(ctx, 30, 1);
                if (nk_button_label(ctx, "Reload asset from disk")) {
                    af_asset_error error;
                    char source[AF_ASSET_PATH_CAPACITY];
                    size_t n = strcspn(asset.path, "#");
                    memcpy(source, asset.path, n);
                    source[n] = 0;
                    bool loaded = af_engine_reload_asset_source(app->engine, app->document.scene, source, &error);
                    if (loaded)
                        ed_thumbnails_invalidate(&app->thumbnails);
                    set_status(app, loaded ? "Asset reloaded." : error.message);
                }
            }
        }
        nk_layout_row_dynamic(ctx, 30, 1);
        if (nk_button_label(ctx, "Close assets"))
            app->show_assets = false;
    }
    nk_end(ctx);
}

void play_hud(editor *app) {
    if (!app->runtime.playing)
        return;
    bool player = false;
    for (size_t i = 0; i < af_scene_object_count(app->runtime.scene); ++i)
        if (af_scene_object_at(app->runtime.scene, i)->gameplay.player.enabled) {
            player = true;
            break;
        }
    ed_play_hud(&app->gui.context, app->engine, app->viewport, app->runtime.paused, app->free_camera, true, player);
}

static bool animation_button(editor *app, unsigned index, const char *text) {
    struct nk_rect b = nk_widget_bounds(&app->gui.context);
    app->animation_buttons[index] = (af_rect){b.x, b.y, b.w, b.h};
    return nk_button_label(&app->gui.context, text) != 0;
}

void animation_panel(editor *app, float width, float height) {
    if (!app->show_animation)
        return;
    const af_scene *scene = app->runtime.playing ? app->runtime.scene : app->document.scene;
    struct nk_context *ctx = &app->gui.context;
    const af_scene_object *target = af_scene_get_object(scene, app->animation_target);
    if (!target || !target->animation.asset[0]) {
        app->animation_target = 0;
        for (size_t i = 0; i < af_scene_object_count(scene); ++i) {
            const af_scene_object *o = af_scene_object_at(scene, i);
            if (o->animation.asset[0]) {
                app->animation_target = o->id;
                target = o;
                break;
            }
        }
    }
    nk_flags flags = NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_MOVABLE;
    if (app->modal || app->dialog)
        flags |= NK_WINDOW_ROM;
    if (nk_begin(ctx, "Animation", nk_rect(fmaxf(20, (width - 560) / 2), 110, 560, fminf(590, height - 150)), flags)) {
        nk_layout_row_dynamic(ctx, 30, 1);
        if (nk_combo_begin_label(ctx, target ? target->name : "No animated model selected", nk_vec2(500, 180))) {
            nk_layout_row_dynamic(ctx, 26, 1);
            for (size_t i = 0; i < af_scene_object_count(scene); ++i) {
                const af_scene_object *o = af_scene_object_at(scene, i);
                if (o->animation.asset[0] && nk_combo_item_label(ctx, o->name, NK_TEXT_LEFT)) {
                    app->animation_target = o->id;
                    target = o;
                }
            }
            nk_combo_end(ctx);
        }
        if (target && target->animation.asset[0]) {
            af_animation_component component = target->animation;
            af_object_id root = target->id;
            af_animation_status status = af_engine_animation_status(app->engine, root);
            bool changed = false;
            label(ctx, component.asset, 24);
            if (!app->runtime.playing) {
                af_character_rig rig = target->rig;
                nk_layout_row_dynamic(ctx, 26, 1);
                nk_bool enabled = rig.enabled;
                bool rig_changed = nk_checkbox_label(ctx, "Character animation rig", &enabled) != 0;
                rig.enabled = enabled != 0;
                if (rig.enabled) {
                    for (unsigned state = 0; state < 5; ++state) {
                        char title[100];
                        snprintf(title, sizeof(title), "%s clip", af_locomotion_state_name((af_locomotion_state)state));
                        label(ctx, title, 22);
                        nk_layout_row_dynamic(ctx, 28, 1);
                        af_animation_clip_info info;
                        const char *name = af_engine_animation_clip(app->engine, root, rig.clips[state], &info)
                                               ? info.name
                                               : "Missing clip";
                        if (nk_combo_begin_label(ctx, name, nk_vec2(400, 160))) {
                            nk_layout_row_dynamic(ctx, 26, 1);
                            for (size_t clip = 0; clip < status.clip_count; ++clip)
                                if (af_engine_animation_clip(app->engine, root, clip, &info) &&
                                    nk_combo_item_label(ctx, info.name, NK_TEXT_LEFT)) {
                                    rig.clips[state] = (uint32_t)clip;
                                    rig_changed = true;
                                }
                            nk_combo_end(ctx);
                        }
                    }
                    const char *root_name = "Choose root motion source";
                    for (size_t i = 0; i < af_scene_object_count(scene); ++i) {
                        const af_scene_object *node = af_scene_object_at(scene, i);
                        if (node->model.root == root && !node->model.primitive && node->model.node == rig.root_node) {
                            root_name = node->name;
                            break;
                        }
                    }
                    label(ctx, "Root motion source", 22);
                    nk_layout_row_dynamic(ctx, 28, 1);
                    if (nk_combo_begin_label(ctx, root_name, nk_vec2(420, 180))) {
                        nk_layout_row_dynamic(ctx, 26, 1);
                        for (size_t i = 0; i < af_scene_object_count(scene); ++i) {
                            const af_scene_object *node = af_scene_object_at(scene, i);
                            if (node->model.root == root && !node->model.primitive &&
                                nk_combo_item_label(ctx, node->name, NK_TEXT_LEFT)) {
                                rig.root_node = node->model.node;
                                rig_changed = true;
                            }
                        }
                        nk_combo_end(ctx);
                    }
                    float *v[] = {&rig.walk_speed, &rig.run_speed, &rig.camera_distance, &rig.camera_height,
                                  &rig.turn_speed};
                    const char *names[] = {"Walk model speed", "Run model speed", "Camera distance", "Camera height",
                                           "Turn speed"};
                    float minimum[] = {.01f, .02f, .2f, 0, 0}, maximum[] = {99, 100, 30, 10, 100};
                    for (unsigned i = 0; i < 5; ++i) {
                        nk_layout_row_dynamic(ctx, 28, 1);
                        rig_changed |= nk_property_float(ctx, names[i], minimum[i], v[i], maximum[i], .1f, .01f) != 0;
                    }
                    nk_layout_row_dynamic(ctx, 26, 1);
                    nk_bool motion = rig.root_motion;
                    if (nk_checkbox_label(ctx, "Use root motion", &motion)) {
                        rig.root_motion = motion != 0;
                        rig_changed = true;
                    }
                }
                if (rig_changed && af_character_rig_valid(rig))
                    ed_document_rig(&app->document, root, rig);
            }

            nk_layout_row_dynamic(ctx, 26, 1);
            nk_bool skeleton = app->show_skeleton;
            if (nk_checkbox_label(ctx, "Show posed skeleton", &skeleton))
                app->show_skeleton = skeleton != 0;
            af_animation_debug debug = af_engine_animation_debug(app->engine, root);
            char diagnostic[160];
            if (debug.driven) {
                snprintf(diagnostic, sizeof(diagnostic), "%s | speed %.2f | phase %.2f",
                         af_locomotion_state_name(debug.motion.state), debug.motion.speed, debug.motion.phase);
                label(ctx, diagnostic, 24);
                snprintf(diagnostic, sizeof(diagnostic), "Idle %.2f  Walk %.2f  Run %.2f", debug.weights[0],
                         debug.weights[1], debug.weights[2]);
                label(ctx, diagnostic, 24);
                snprintf(diagnostic, sizeof(diagnostic), "Root motion %s | delta %.3f, %.3f",
                         debug.root_motion ? "on" : "off", debug.root_delta.x, debug.root_delta.z);
                label(ctx, diagnostic, 24);
            }
            snprintf(diagnostic, sizeof(diagnostic), "Events %llu | queued %zu | dropped %llu | %s",
                     (unsigned long long)debug.emitted_events, debug.pending_events,
                     (unsigned long long)debug.dropped_events, debug.last_event);
            label(ctx, diagnostic, 24);

            if (!status.available)
                label(ctx, "Animation asset unavailable. Reload its source.", 26);
            else if (!status.clip_count)
                label(ctx, "Skeleton bind pose (no animation clips).", 26);
            else {
                if (debug.driven)
                    nk_widget_disable_begin(ctx);
                nk_layout_row_dynamic(ctx, 30, 1);
                if (nk_combo_begin_label(ctx, status.name, nk_vec2(500, 180))) {
                    nk_layout_row_dynamic(ctx, 26, 1);
                    for (size_t i = 0; i < status.clip_count; ++i) {
                        af_animation_clip_info info;
                        if (af_engine_animation_clip(app->engine, root, i, &info) &&
                            nk_combo_item_label(ctx, info.name, NK_TEXT_LEFT)) {
                            if (!app->runtime.playing)
                                app->animation_preview = true;
                            af_engine_animation_play(app->engine, root, i, app->animation_blend);
                        }
                    }
                    nk_combo_end(ctx);
                }
                nk_layout_row_dynamic(ctx, 32, 3);
                if (animation_button(app, 0, app->runtime.playing ? "Restart" : "Preview")) {
                    if (!app->runtime.playing)
                        app->animation_preview = true;
                    af_engine_animation_play(app->engine, root, status.clip, app->animation_blend);
                }
                if (animation_button(app, 1, status.playing ? "Pause" : "Resume"))
                    af_engine_animation_pause(app->engine, root, status.playing);
                if (animation_button(app, 2, "Stop")) {
                    af_engine_animation_stop(app->engine, root);
                    if (!app->runtime.playing)
                        app->animation_preview = false;
                }
                float time = (float)status.time;
                nk_layout_row_dynamic(ctx, 30, 1);
                if (nk_property_float(ctx, "Time", 0, &time, (float)status.duration, .05f, .005f)) {
                    if (!app->runtime.playing)
                        app->animation_preview = true;
                    af_engine_animation_pause(app->engine, root, true);
                    af_engine_animation_seek(app->engine, root, time);
                }
                char text[128];
                snprintf(text, sizeof(text), "%.3f / %.3f s%s", status.time, status.duration,
                         status.fading ? "  (blending)" : "");
                label(ctx, text, 24);
                float speed = status.speed;
                nk_bool loop = status.loop;
                nk_layout_row_dynamic(ctx, 30, 1);
                bool options = nk_property_float(ctx, "Speed", -4, &speed, 4, .1f, .01f) != 0;
                nk_layout_row_dynamic(ctx, 26, 1);
                options |= nk_checkbox_label(ctx, "Loop", &loop) != 0;
                if (options)
                    af_engine_animation_options(app->engine, root, speed, loop != 0);
                if (!app->runtime.playing) {
                    nk_layout_row_dynamic(ctx, 26, 1);
                    nk_bool autoplay = component.autoplay;
                    if (nk_checkbox_label(ctx, "Autoplay in game", &autoplay)) {
                        component.autoplay = autoplay != 0;
                        changed = true;
                    }
                }
                if (!app->runtime.playing) {
                    nk_layout_row_dynamic(ctx, 30, 1);
                    if (nk_button_label(ctx, "Use clip, speed and loop as startup defaults")) {
                        component.clip = status.clip;
                        component.speed = speed;
                        component.loop = loop != 0;
                        changed = true;
                    }
                }
                nk_layout_row_dynamic(ctx, 30, 1);
                nk_property_float(ctx, "Blend seconds", 0, &app->animation_blend, 5, .1f, .01f);
                if (debug.driven)
                    nk_widget_disable_end(ctx);
                label(ctx,
                      debug.driven           ? "Character gameplay controls this animation."
                      : app->runtime.playing ? "Playback follows simulation pause/step."
                                             : "Preview leaves authored transforms and history unchanged.",
                      28);
            }
            if (changed && !app->runtime.playing) {
                if (ed_document_animation(&app->document, root, component)) {
                    af_engine_set_scene(app->engine, app->document.scene);
                    if (app->animation_preview)
                        af_engine_animation_play(app->engine, root, component.clip, app->animation_blend);
                }
            }
        } else
            label(ctx, "Import a glTF model containing animation or a skin.", 30);
        nk_layout_row_dynamic(ctx, 30, 1);
        if (nk_button_label(ctx, "Close animation panel"))
            app->show_animation = false;
    }
    nk_end(ctx);
}

void input_panel(editor *app) {
    if (!app->show_input)
        return;
    struct nk_context *ctx = &app->gui.context;
    if (nk_begin(ctx, "Input actions", nk_rect(280, 130, 540, 570),
                 NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_MOVABLE)) {
        af_input_map map = af_engine_input_map(app->engine);
        static const char *keys[] = {
            "None",       "Escape",       "Space",      "R",     "W",   "A",         "S",      "D",    "Q",     "E",
            "Left Shift", "Left Control", "Left Super", "Enter", "Tab", "Backspace", "Delete", "Left", "Right", "Up",
            "Down",       "Home",         "End",        "Z",     "Y",   "C",         "V",      "X",    "N",     "O",
            "F",          "F5",           "F6"};
        label(ctx, "Bindings apply immediately; save them for reuse.", 24);
        for (size_t i = 0; i < map.count; ++i) {
            af_action_binding b = map.bindings[i];
            label(ctx, b.name, 24);
            nk_layout_row_dynamic(ctx, 28, 2);
            int negative = nk_combo(ctx, keys, AF_KEY_COUNT + 1, b.negative_key + 1, 24, nk_vec2(230, 180)) - 1,
                positive = nk_combo(ctx, keys, AF_KEY_COUNT + 1, b.positive_key + 1, 24, nk_vec2(230, 180)) - 1;
            static const char *axes[] = {"No stick axis", "Left X",        "Left Y", "Right X", "Right Y",
                                         "Left trigger",  "Right trigger", "Axis 6", "Axis 7"};
            nk_layout_row_dynamic(ctx, 28, 1);
            int axis = nk_combo(ctx, axes, 9, b.pad_axis + 1, 24, nk_vec2(280, 160)) - 1;
            const char *buttons[33];
            char names[32][32];
            buttons[0] = "No button";
            for (int button_index = 0; button_index < 32; ++button_index) {
                const char *name = button_index < SDL_GAMEPAD_BUTTON_COUNT
                                       ? SDL_GetGamepadStringForButton((SDL_GamepadButton)button_index)
                                       : NULL;
                if (name)
                    snprintf(names[button_index], 32, "%s", name);
                else
                    snprintf(names[button_index], 32, "Button %d", button_index);
                buttons[button_index + 1] = names[button_index];
            }
            nk_layout_row_dynamic(ctx, 28, 1);
            int button = nk_combo(ctx, buttons, 33, b.pad_button + 1, 24, nk_vec2(300, 180)) - 1;
            if (negative != b.negative_key || positive != b.positive_key || axis != b.pad_axis ||
                button != b.pad_button) {
                b.negative_key = negative;
                b.positive_key = positive;
                b.pad_axis = axis;
                b.pad_button = button;
                af_engine_bind_action(app->engine, b);
            }
        }
        if (!app->input_path[0]) {
            if (!app->project_path[0] ||
                !af_asset_resolve_path(app->project_path, "input.json", app->input_path, sizeof(app->input_path)))
                snprintf(app->input_path, sizeof(app->input_path), "input.json");
        }
        nk_layout_row_dynamic(ctx, 28, 1);
        nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, app->input_path, sizeof(app->input_path), nk_filter_default);
        nk_layout_row_dynamic(ctx, 30, 3);
        af_asset_error error;
        if (nk_button_label(ctx, "Save bindings")) {
            bool ok = af_engine_save_input_map(app->engine, app->input_path, &error);
            snprintf(app->document.status, sizeof(app->document.status), "%s",
                     ok ? "Input bindings saved." : error.message);
        }
        if (nk_button_label(ctx, "Load bindings")) {
            bool ok = af_engine_load_input_map(app->engine, app->input_path, &error);
            snprintf(app->document.status, sizeof(app->document.status), "%s",
                     ok ? "Input bindings loaded." : error.message);
        }
        if (nk_button_label(ctx, "Close"))
            app->show_input = false;
    }
    nk_end(ctx);
}

void prefab_panel(editor *app) {
    if (!app->show_prefab)
        return;
    struct nk_context *ctx = &app->gui.context;
    if (nk_begin(ctx, "Prefabs", nk_rect(280, 160, 550, 220), NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_MOVABLE)) {
        if (!app->prefab_path[0])
            snprintf(app->prefab_path, sizeof(app->prefab_path), "object.afterlight.prefab");
        label(ctx, "Save a selected hierarchy or add an independent instance.", 26);
        nk_layout_row_dynamic(ctx, 28, 1);
        nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, app->prefab_path, sizeof(app->prefab_path),
                                       nk_filter_default);
        nk_layout_row_dynamic(ctx, 32, 2);
        if (app->runtime.playing)
            nk_widget_disable_begin(ctx);
        if (nk_button_label(ctx, "Save selected hierarchy")) {
            af_scene_error e;
            bool ok = af_prefab_save(app->document.scene, app->document.selection,
                                     app->document.path[0] ? app->document.path : NULL, app->prefab_path, &e);
            set_status(app, ok ? "Prefab saved." : e.message);
        }
        if (nk_button_label(ctx, "Instantiate prefab") && ed_document_prefab(&app->document, app->prefab_path)) {
            load_assets(app);
            refresh(app);
        }
        if (app->runtime.playing)
            nk_widget_disable_end(ctx);
        nk_layout_row_dynamic(ctx, 28, 1);
        if (nk_button_label(ctx, "Close"))
            app->show_prefab = false;
    }
    nk_end(ctx);
}

void draw_splitters(editor *app, float width, float height) {
    if (app->runtime.playing && app->maximize_play)
        return;
    struct nk_context *ctx = &app->gui.context;
    af_vec2 mouse = af_engine_mouse_position(app->engine);
    float l_x = app->left_sidebar_width;
    float r_x = width - app->right_sidebar_width;
    float top_y = 64.0f;
    float bot_h = height - 96.0f;

    bool hover_l = fabsf(mouse.x - l_x) <= 4.0f && mouse.y >= top_y && mouse.y <= top_y + bot_h;
    bool hover_r = fabsf(mouse.x - r_x) <= 4.0f && mouse.y >= top_y && mouse.y <= top_y + bot_h;

    /* Set cursor when hovering or dragging splitters */
    static SDL_Cursor *cursor_resize_ew = NULL;
    static SDL_Cursor *cursor_default = NULL;
    if (hover_l || hover_r || app->dragging_left_splitter || app->dragging_right_splitter) {
        if (!cursor_resize_ew)
            cursor_resize_ew = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_EW_RESIZE);
        if (cursor_resize_ew)
            SDL_SetCursor(cursor_resize_ew);
    } else if (cursor_resize_ew) {
        if (!cursor_default)
            cursor_default = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
        if (cursor_default)
            SDL_SetCursor(cursor_default);
    }

    /* Left splitter handle */
    struct nk_color col_l = (app->dragging_left_splitter || hover_l) ? nk_rgb(145, 125, 235) : nk_rgb(46, 49, 60);
    nk_style_push_style_item(ctx, &ctx->style.window.fixed_background, nk_style_item_color(col_l));
    if (nk_begin(ctx, "Splitter L", nk_rect(l_x - 2.0f, top_y, 4.0f, bot_h),
                 NK_WINDOW_NO_INPUT | NK_WINDOW_NO_SCROLLBAR)) {
        nk_end(ctx);
    }
    nk_style_pop_style_item(ctx);

    /* Right splitter handle */
    struct nk_color col_r = (app->dragging_right_splitter || hover_r) ? nk_rgb(145, 125, 235) : nk_rgb(46, 49, 60);
    nk_style_push_style_item(ctx, &ctx->style.window.fixed_background, nk_style_item_color(col_r));
    if (nk_begin(ctx, "Splitter R", nk_rect(r_x - 2.0f, top_y, 4.0f, bot_h),
                 NK_WINDOW_NO_INPUT | NK_WINDOW_NO_SCROLLBAR)) {
        nk_end(ctx);
    }
    nk_style_pop_style_item(ctx);
}
