/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "agent_internal.h"
#include "cube.h"
#include "render/render_test.h"
#include "runtime/runtime.h"
#include <afterlight/extension_vulkan.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct capture_ctx {
    const cJSON *args;
    const char *scene_path;
    const char *project_path;
    const char *rgba_path;
    af_project project;
    af_scene *scene;
    ed_runtime runtime;
    bool run_game;
    bool has_camera_override;
    af_camera camera_override;
    af_debug_view debug_view;
    bool has_clear_color;
    af_color clear_color;
    bool enable_metalfx;
    int upscaler_quality;
    uint32_t target_frames;
    uint32_t frame_counter;
    bool capture_requested;
    bool captured;
    uint32_t out_width;
    uint32_t out_height;
    af_camera final_camera;
    af_engine_stats final_stats;
    char error[512];
} capture_ctx;

static af_render_backend parse_backend(const char *s) {
    if (!s || !s[0] || !strcmp(s, "auto"))
        return AF_RENDER_BACKEND_AUTO;
    if (!strcmp(s, "metal"))
        return AF_RENDER_BACKEND_METAL;
    if (!strcmp(s, "vulkan"))
        return AF_RENDER_BACKEND_VULKAN;
    return AF_RENDER_BACKEND_AUTO;
}

static af_debug_view parse_debug_view(const char *s) {
    if (!s || !s[0] || !strcmp(s, "shaded"))
        return AF_DEBUG_SHADED;
    if (!strcmp(s, "depth"))
        return AF_DEBUG_DEPTH;
    if (!strcmp(s, "motion"))
        return AF_DEBUG_MOTION;
    if (!strcmp(s, "history"))
        return AF_DEBUG_HISTORY;
    if (!strcmp(s, "rejection"))
        return AF_DEBUG_REJECTION;
    return AF_DEBUG_SHADED;
}

static const char *backend_name(af_render_backend b) {
    if (b == AF_RENDER_BACKEND_METAL)
        return "metal";
    if (b == AF_RENDER_BACKEND_VULKAN)
        return "vulkan";
    return "auto";
}

static af_camera camera_from_look_at(af_vec3 pos, af_vec3 target, float fov_rad, float near_clip, float far_clip) {
    af_camera c = af_camera_default();
    c.position = pos;
    af_vec3 d = af_vec3_sub(target, pos);
    float len = af_vec3_length(d);
    if (len > 1e-5f) {
        d = af_vec3_scale(d, 1.0f / len);
        float limit = AF_PI * (89.0f / 180.0f);
        float p = asinf(fminf(1.0f, fmaxf(-1.0f, d.y)));
        c.pitch = fminf(limit, fmaxf(-limit, p));
        c.yaw = atan2f(d.z, d.x);
    }
    if (fov_rad > 0.05f && fov_rad < AF_PI - 0.05f)
        c.vertical_fov = fov_rad;
    if (near_clip > 0.0f)
        c.near_clip = near_clip;
    if (far_clip > c.near_clip)
        c.far_clip = far_clip;
    return c;
}

static bool write_rgba_file(const char *path, const unsigned char *rgba, uint32_t w, uint32_t h) {
    FILE *f = fopen(path, "wb");
    if (!f)
        return false;
    unsigned char header[12];
    header[0] = 'A';
    header[1] = 'F';
    header[2] = 'R';
    header[3] = 'A';
    header[4] = (unsigned char)(w & 0xffu);
    header[5] = (unsigned char)((w >> 8) & 0xffu);
    header[6] = (unsigned char)((w >> 16) & 0xffu);
    header[7] = (unsigned char)((w >> 24) & 0xffu);
    header[8] = (unsigned char)(h & 0xffu);
    header[9] = (unsigned char)((h >> 8) & 0xffu);
    header[10] = (unsigned char)((h >> 16) & 0xffu);
    header[11] = (unsigned char)((h >> 24) & 0xffu);
    size_t bytes = (size_t)w * (size_t)h * 4u;
    bool ok = fwrite(header, 1, sizeof(header), f) == sizeof(header) && fwrite(rgba, 1, bytes, f) == bytes;
    fclose(f);
    return ok;
}

static bool capture_init(af_engine *engine, void *userdata) {
    capture_ctx *ctx = userdata;
    af_mesh_data cube = cube_mesh_data();
    if (!af_engine_upload_mesh(engine, "cube", &cube)) {
        snprintf(ctx->error, sizeof(ctx->error), "Failed to upload built-in cube mesh.");
        return false;
    }

    af_scene_error serr = {0};
    af_asset_error aerr = {0};
    const char *load_scene_path = ctx->scene_path;
    if (ctx->project_path && ctx->project_path[0]) {
        if (!af_project_load(ctx->project_path, &ctx->project, &aerr)) {
            snprintf(ctx->error, sizeof(ctx->error), "Project load failed: %s", aerr.message);
            return false;
        }
        if (!load_scene_path || !load_scene_path[0])
            load_scene_path = ctx->project.startup_scene;
    }

    ctx->scene = af_scene_load(load_scene_path, &serr);
    if (!ctx->scene) {
        snprintf(ctx->error, sizeof(ctx->error), "Scene load failed: %s", serr.message);
        return false;
    }

    if (!af_engine_load_scene_assets(engine, ctx->scene, &aerr)) {
        snprintf(ctx->error, sizeof(ctx->error), "Asset load failed: %s", aerr.message);
        return false;
    }

    if (ctx->run_game && ctx->project_path && ctx->project_path[0]) {
        if (!ed_runtime_set_project(&ctx->runtime, ctx->project_path, load_scene_path) ||
            !ed_runtime_start_file(&ctx->runtime, ctx->scene, engine, ctx->project.game_module)) {
            snprintf(ctx->error, sizeof(ctx->error), "Game runtime start failed: %s", ctx->runtime.status);
            return false;
        }
    } else {
        af_engine_set_scene(engine, ctx->scene);
        af_engine_set_simulation_paused(engine, true);
    }

    if (ctx->has_clear_color)
        af_engine_set_clear_color(engine, ctx->clear_color);

    if (ctx->debug_view != AF_DEBUG_SHADED)
        (void)af_engine_set_debug_view(engine, ctx->debug_view);

    if (ctx->enable_metalfx && af_engine_upscaler_quality_count(engine) > 0) {
        if (ctx->upscaler_quality >= 0 && (uint32_t)ctx->upscaler_quality < af_engine_upscaler_quality_count(engine))
            (void)af_engine_set_upscaler_quality(engine, (uint32_t)ctx->upscaler_quality);
        (void)af_engine_set_upscaler_enabled(engine, true);
    }

    if (ctx->has_camera_override) {
        if (!af_engine_set_camera(engine, &ctx->camera_override)) {
            snprintf(ctx->error, sizeof(ctx->error), "Invalid camera override parameters.");
            return false;
        }
    } else if (!ctx->run_game) {
        af_camera cam = af_camera_default();
        if (!af_scene_game_camera(ctx->scene, &cam))
            cam = camera_from_look_at((af_vec3){0.0f, 4.0f, 10.0f}, (af_vec3){0.0f, 0.5f, 0.0f}, AF_PI / 3.0f, 0.1f,
                                      500.0f);
        (void)af_engine_set_camera(engine, &cam);
    }

    return true;
}

static void capture_fixed(af_engine *engine, double dt, void *userdata) {
    (void)engine;
    capture_ctx *ctx = userdata;
    if (ctx->run_game && ctx->runtime.playing)
        ed_runtime_update(&ctx->runtime, dt, false);
}

static void capture_update(af_engine *engine, double dt, void *userdata) {
    capture_ctx *ctx = userdata;
    if (ctx->run_game && ctx->runtime.playing) {
        ed_runtime_frame_update(&ctx->runtime, dt, false);
        if (ctx->has_camera_override)
            (void)af_engine_set_camera(engine, &ctx->camera_override);
    }

    ++ctx->frame_counter;
    if (ctx->frame_counter < ctx->target_frames)
        return;

    if (!ctx->capture_requested) {
        if (!ctx->rgba_path || !ctx->rgba_path[0]) {
            ctx->final_camera = af_engine_get_camera(engine);
            ctx->final_stats = af_engine_get_stats(engine);
            ctx->captured = true;
            af_engine_request_quit(engine);
            return;
        }
        af_temporal_test_stats t = af_engine_test_temporal(engine);
        uint32_t w = t.width;
        uint32_t h = t.height;
        if (!w || !h) {
            int lw = 800, lh = 600;
            af_engine_get_window_size(engine, &lw, &lh);
            w = (uint32_t)(lw > 0 ? lw : 800);
            h = (uint32_t)(lh > 0 ? lh : 600);
        }
        if (w > 4096u)
            w = 4096u;
        if (h > 4096u)
            h = 4096u;
        if (!af_engine_test_capture_region(engine, (af_vec2){0.0f, 0.0f}, w, h)) {
            snprintf(ctx->error, sizeof(ctx->error), "af_engine_test_capture_region failed (%ux%u).", w, h);
            af_engine_request_quit(engine);
            return;
        }
        ctx->out_width = w;
        ctx->out_height = h;
        ctx->capture_requested = true;
        return;
    }

    size_t cap = (size_t)ctx->out_width * (size_t)ctx->out_height * 4u;
    unsigned char *rgba = malloc(cap);
    if (!rgba) {
        snprintf(ctx->error, sizeof(ctx->error), "Out of memory allocating screenshot readback buffer.");
        af_engine_request_quit(engine);
        return;
    }
    uint32_t actual_w = 0, actual_h = 0;
    bool ok = af_engine_test_read_pixels(engine, rgba, cap, &actual_w, &actual_h);
    if (ok && actual_w > 0 && actual_h > 0) {
        if (!write_rgba_file(ctx->rgba_path, rgba, actual_w, actual_h)) {
            snprintf(ctx->error, sizeof(ctx->error), "Failed to write RGBA output file: %s", ctx->rgba_path);
            ok = false;
        } else {
            ctx->out_width = actual_w;
            ctx->out_height = actual_h;
            ctx->captured = true;
        }
    } else if (!ctx->error[0]) {
        snprintf(ctx->error, sizeof(ctx->error), "af_engine_test_read_pixels failed.");
    }
    free(rgba);
    ctx->final_camera = af_engine_get_camera(engine);
    ctx->final_stats = af_engine_get_stats(engine);
    af_engine_request_quit(engine);
}

static void capture_shutdown(af_engine *engine, void *userdata) {
    capture_ctx *ctx = userdata;
    af_engine_set_scene(engine, NULL);
    if (ctx->runtime.playing || ctx->runtime.scene || ctx->runtime.library)
        ed_runtime_stop(&ctx->runtime);
    af_scene_destroy(ctx->scene);
    ctx->scene = NULL;
}

cJSON *af_agent_cmd_capture(const cJSON *args) {
    if (!args || !cJSON_IsObject(args))
        return af_agent_err("capture requires args object.");

    capture_ctx ctx = {0};
    ctx.args = args;
    ctx.scene_path = af_agent_str(args, "scene_path", NULL);
    ctx.project_path = af_agent_str(args, "project_path", NULL);
    ctx.rgba_path = af_agent_str(args, "rgba_path", NULL);
    if ((!ctx.scene_path || !ctx.scene_path[0]) && (!ctx.project_path || !ctx.project_path[0]))
        return af_agent_err("capture requires 'scene_path' or 'project_path'.");

    ctx.run_game = af_agent_bool(args, "run_game", ctx.project_path != NULL && ctx.project_path[0] != '\0');
    ctx.target_frames = af_agent_u32(args, "frames", 12u);
    if (ctx.target_frames < 2u)
        ctx.target_frames = 2u;
    if (ctx.target_frames > 6000u)
        ctx.target_frames = 6000u;

    ctx.debug_view = parse_debug_view(af_agent_str(args, "debug_view", "shaded"));
    ctx.has_clear_color = af_agent_color(args, "clear_color", &ctx.clear_color);
    ctx.enable_metalfx = af_agent_bool(args, "metalfx", false);
    ctx.upscaler_quality = af_agent_i32(args, "upscaler_quality", -1);

    const cJSON *cam_json = cJSON_GetObjectItemCaseSensitive(args, "camera");
    if (cam_json && cJSON_IsObject(cam_json)) {
        ctx.has_camera_override = true;
        af_vec3 pos = {0.0f, 3.0f, 8.0f};
        (void)af_agent_vec3(cam_json, "position", &pos);
        float fov_rad = AF_PI / 3.0f;
        if (af_agent_has(cam_json, "fov_degrees"))
            fov_rad = af_agent_f32(cam_json, "fov_degrees", 60.0f) * (AF_PI / 180.0f);
        else if (af_agent_has(cam_json, "vertical_fov"))
            fov_rad = af_agent_f32(cam_json, "vertical_fov", AF_PI / 3.0f);
        float near_clip = af_agent_f32(cam_json, "near_clip", 0.1f);
        float far_clip = af_agent_f32(cam_json, "far_clip", 500.0f);
        af_vec3 target = {0};
        if (af_agent_vec3(cam_json, "look_at", &target)) {
            ctx.camera_override = camera_from_look_at(pos, target, fov_rad, near_clip, far_clip);
        } else {
            ctx.camera_override = af_camera_default();
            ctx.camera_override.position = pos;
            ctx.camera_override.yaw = af_agent_f32(cam_json, "yaw", -AF_PI * 0.5f);
            ctx.camera_override.pitch = af_agent_f32(cam_json, "pitch", 0.0f);
            ctx.camera_override.vertical_fov = fov_rad;
            ctx.camera_override.near_clip = near_clip;
            ctx.camera_override.far_clip = far_clip;
        }
    }

    af_engine_config cfg = af_engine_config_default();
    cfg.title = "Afterlight MCP Capture";
    cfg.width = af_agent_i32(args, "width", 800);
    cfg.height = af_agent_i32(args, "height", 600);
    if (cfg.width < 64)
        cfg.width = 64;
    if (cfg.height < 64)
        cfg.height = 64;
    if (cfg.width > 2560)
        cfg.width = 2560;
    if (cfg.height > 1600)
        cfg.height = 1600;
    cfg.enable_validation = af_agent_bool(args, "validation", true);
    cfg.backend = parse_backend(af_agent_str(args, "backend", "auto"));

    af_engine *engine = af_engine_create(&cfg);
    if (!engine)
        return af_agent_err("af_engine_create failed (check graphics backend and display session).");

    af_game game = {
        .init = capture_init,
        .update = capture_update,
        .fixed_update = capture_fixed,
        .shutdown = capture_shutdown,
        .user_data = &ctx,
    };
    int rc = af_engine_run(engine, &game);
    af_engine_destroy(engine);

    if (rc != 0 || !ctx.captured)
        return af_agent_err(ctx.error[0] ? ctx.error : "Capture failed.");

    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "active_backend", backend_name(ctx.final_stats.active_backend));
    cJSON_AddNumberToObject(data, "width", (double)ctx.out_width);
    cJSON_AddNumberToObject(data, "height", (double)ctx.out_height);
    cJSON_AddNumberToObject(data, "rendered_frames", (double)ctx.final_stats.rendered_frames);
    cJSON_AddNumberToObject(data, "gpu_frame_ms", ctx.final_stats.gpu_frame_ms);
    cJSON_AddNumberToObject(data, "cpu_render_ms", ctx.final_stats.cpu_render_ms);
    cJSON_AddNumberToObject(data, "draw_calls", (double)ctx.final_stats.draw_calls);
    cJSON_AddNumberToObject(data, "visible_objects", (double)ctx.final_stats.visible_objects);
    cJSON_AddNumberToObject(data, "culled_objects", (double)ctx.final_stats.culled_objects);
    cJSON_AddNumberToObject(data, "missing_meshes", (double)ctx.final_stats.missing_meshes);
    cJSON_AddNumberToObject(data, "missing_textures", (double)ctx.final_stats.missing_textures);
    cJSON_AddNumberToObject(data, "validation_errors", (double)ctx.final_stats.validation_errors);
    cJSON_AddNumberToObject(data, "tracked_gpu_bytes", (double)ctx.final_stats.tracked_gpu_bytes);
    cJSON_AddBoolToObject(data, "ray_query_supported", ctx.final_stats.ray_query_supported);
    cJSON_AddBoolToObject(data, "ray_traced_shadows_active", ctx.final_stats.ray_traced_shadows_active);
    cJSON_AddBoolToObject(data, "ray_traced_water_reflections_active",
                          ctx.final_stats.ray_traced_water_reflections_active);
    cJSON_AddBoolToObject(data, "ray_traced_gi_active", ctx.final_stats.ray_traced_gi_active);
    cJSON_AddNumberToObject(data, "ray_instances", (double)ctx.final_stats.ray_instances);

    cJSON *cam_out = cJSON_CreateObject();
    cJSON_AddItemToObject(cam_out, "position", af_agent_json_vec3(ctx.final_camera.position));
    cJSON_AddNumberToObject(cam_out, "yaw", ctx.final_camera.yaw);
    cJSON_AddNumberToObject(cam_out, "pitch", ctx.final_camera.pitch);
    cJSON_AddNumberToObject(cam_out, "vertical_fov", ctx.final_camera.vertical_fov);
    cJSON_AddItemToObject(data, "camera", cam_out);
    if (ctx.runtime.status[0])
        cJSON_AddStringToObject(data, "runtime_status", ctx.runtime.status);
    return af_agent_ok(data);
}
