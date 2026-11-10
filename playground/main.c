/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <afterlight/game_module.h>
#include "cube.h"
#include "ui/gui.h"
#include "ui/play_hud.h"
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct playground {
    ed_gui gui;
    af_scene *scene;
    af_game_host host;
    const af_game_module *api;
    void *state;
    uint64_t frames;
    const char *path;
    bool failed;
} playground;
static bool init(af_engine *engine, void *memory) {
    playground *app = memory;
    const char *required = getenv("AFTERLIGHT_REQUIRE_VALIDATION");
    if (required && !strcmp(required, "1") && !af_engine_get_stats(engine).validation_enabled)
        return false;
    char path[AF_ASSET_PATH_CAPACITY];
    const char *base = af_engine_get_base_path(engine);
    if (!base)
        return false;
    if (snprintf(path, sizeof(path), "%sscenes/obstacle.afterlight.json", base) >= (int)sizeof(path))
        return false;
    af_scene_error error;
    app->scene = af_scene_load(app->path ? app->path : path, &error);
    if (!app->scene) {
        fprintf(stderr, "Playground scene: %s\n", error.message);
        return false;
    }
    af_mesh_data cube = cube_mesh_data();
    if (!af_engine_upload_mesh(engine, "cube", &cube))
        return false;
    char font[AF_ASSET_PATH_CAPACITY];
    if (snprintf(font, sizeof(font), "%sfonts/FiraSans-Regular.ttf", base) >= (int)sizeof(font) ||
        !ed_gui_init(&app->gui, engine, font))
        return false;
    af_asset_error asset_error;
    if (!af_engine_load_scene_assets(engine, app->scene, &asset_error)) {
        fprintf(stderr, "Playground assets: %s\n", asset_error.message);
        return false;
    }
    app->host = af_game_host_create(app->scene, engine);
    app->host.input_enabled = true;
    app->api = af_game_get_api(AF_GAME_ABI_VERSION);
    if (!app->api)
        return false;
    app->state = calloc(1, app->api->state_size);
    if (!app->state)
        return false;
    af_engine_set_scene(engine, app->scene);
    af_engine_set_clear_color(engine, (af_color){0.025f, 0.04f, 0.07f, 1});
    return app->api->init(app->state, &app->host);
}
static void update(af_engine *engine, double delta, void *memory) {
    playground *app = memory;
    if (app->frames && af_engine_get_stats(engine).rendered_frames >= app->frames) {
        af_engine_request_quit(engine);
        return;
    }
    bool captured = af_engine_mouse_captured(engine);
    if (app->api->frame_update)
        app->api->frame_update(app->state, &app->host, delta);
    if (af_engine_key_pressed(engine, AF_KEY_ESCAPE) && !captured)
        af_engine_request_quit(engine);
    int w, h;
    af_engine_get_window_size(engine, &w, &h);
    ed_gui_input(&app->gui, engine);
    ed_play_hud(&app->gui.context, engine, (af_rect){0, 0, (float)w, (float)h}, false, false, false, true);
    if (!ed_gui_render(&app->gui, engine, (float)w, (float)h)) {
        app->failed = true;
        af_engine_request_quit(engine);
    }
}
static void fixed(af_engine *engine, double delta, void *memory) {
    (void)engine;
    playground *app = memory;
    app->api->update(app->state, &app->host, delta);
}
static void shutdown(af_engine *engine, void *memory) {
    playground *app = memory;
    if (app->api && app->state)
        app->api->shutdown(app->state, &app->host);
    free(app->state);
    ed_gui_destroy(&app->gui);
    af_engine_set_scene(engine, NULL);
    af_scene_destroy(app->scene);
    af_engine_stats stats = af_engine_get_stats(engine);
    printf("Playground: %" PRIu64 " frames, %" PRIu64 " fixed ticks, %u validation errors.\n", stats.rendered_frames,
           stats.simulation_ticks, stats.validation_errors);
}
int main(int argc, char **argv) {
    playground app = {0};
    af_engine_config config = af_engine_config_default();
    config.title = "Afterlight Playground | WASD walk, Space jump, R restart, Escape release";
    config.show_fps = true;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--frames") && i + 1 < argc) {
            char *end;
            const char *value = argv[++i];
            errno = 0;
            unsigned long long frames = strtoull(value, &end, 10);
            if (errno || !value[0] || value[0] < '0' || value[0] > '9' || *end || !frames)
                return 1;
            app.frames = (uint64_t)frames;
        } else if (!strcmp(argv[i], "--scene") && i + 1 < argc)
            app.path = argv[++i];
        else if (!strcmp(argv[i], "--validation"))
            config.enable_validation = true;
        else if (!strcmp(argv[i], "--no-validation"))
            config.enable_validation = false;
        else if (!strcmp(argv[i], "--help")) {
            puts("afterlight_playground [--frames N] [--scene PATH] [--validation | --no-validation]");
            return 0;
        } else
            return 1;
    }
    af_engine *engine = af_engine_create(&config);
    if (!engine)
        return 1;
    af_game game = {.init = init, .update = update, .fixed_update = fixed, .shutdown = shutdown, .user_data = &app};
    int result = af_engine_run(engine, &game);
    af_engine_destroy(engine);
    return app.failed ? 1 : result;
}
