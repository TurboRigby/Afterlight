/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <afterlight/afterlight.h>
#include "cube.h"
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct sandbox {
    double elapsed;
    uint64_t frame_limit;
    bool paused;
    af_camera camera;
    af_scene *scene;
    af_object_id cubes[3];
    bool failed;
    bool ready;
    bool animate_demo;
    const char *load_path;
    const char *save_path;
    af_transform base_transforms[3];
} sandbox;

static void reset_view(sandbox *game)
{
    game->elapsed = 0;
    game->camera = af_camera_default();
    game->camera.position = (af_vec3){6.0f, 4.0f, 8.0f};
    game->camera.yaw = atan2f(-game->camera.position.z, -game->camera.position.x);
    game->camera.pitch = atan2f(-game->camera.position.y,
        hypotf(game->camera.position.x, game->camera.position.z));
}

static bool update_objects(sandbox *game)
{
    if (!game->animate_demo) return true;
    float angle = (float)fmod(game->elapsed * 0.35, 2.0 * AF_PI);
    for (unsigned i = 0; i < 3; ++i) {
        af_transform transform = game->base_transforms[i];
        if (i == 0) transform.rotation.x += angle * 0.8f;
        else transform.rotation.y += i == 1 ? angle : -angle;
        if (!af_scene_set_object_transform(game->scene, game->cubes[i], transform)) return false;
    }
    return true;
}

static bool game_init(af_engine *engine, void *user_data)
{
    sandbox *game = user_data;
    reset_view(game);
    const char *path = game->load_path;
    char *default_path = NULL;
    if (!path) {
        const char *base = af_engine_get_base_path(engine);
        if (!base) { fputs("Cannot locate application data directory.\n", stderr); return false; }
        const char *relative = "scenes/demo.afterlight.json";
        size_t capacity = strlen(base) + strlen(relative) + 1;
        default_path = malloc(capacity);
        if (!default_path) return false;
        snprintf(default_path, capacity, "%s%s", base, relative);
        path = default_path;
    }
    af_scene_error error;
    game->scene = af_scene_load(path, &error);
    if (game->scene) printf("Loaded scene: %s\n", path);
    free(default_path);
    if (!game->scene) { fprintf(stderr, "Scene load failed: %s\n", error.message); return false; }
    game->animate_demo = game->load_path == NULL;
    if (game->animate_demo) {
        for (unsigned i = 0; i < 3; ++i) {
            game->cubes[i] = (af_object_id)i + 1;
            const af_scene_object *object = af_scene_get_object(game->scene, game->cubes[i]);
            if (!object) { fputs("Demo scene is missing an animated object.\n", stderr); return false; }
            game->base_transforms[i] = object->transform;
        }
    }
    af_mesh_data mesh = cube_mesh_data();
    if (!af_engine_upload_mesh(engine, "cube", &mesh) || !af_engine_set_camera(engine, &game->camera)) return false;
    af_asset_error asset_error;
    if(!af_engine_load_scene_assets(engine,game->scene,&asset_error)){fprintf(stderr,"Asset load: %s\n",asset_error.message);return false;}
    af_engine_set_scene(engine, game->scene);
    game->ready = true;
    puts("Afterlight 0.5.0 — File-backed scene | C + Vulkan");
    printf("%zu scene objects; cube mesh available\n", af_scene_object_count(game->scene));
    puts("Click: mouse look | WASD: fly | Q/E: down/up | Shift: faster");
    puts("Space: pause | R: reset view | Escape: release mouse, then quit");
    return true;
}

static void game_update(af_engine *engine, double delta, void *user_data)
{
    sandbox *game = user_data;
    if (game->frame_limit && af_engine_get_stats(engine).rendered_frames >= game->frame_limit) {
        af_engine_request_quit(engine);
        return;
    }
    if (af_engine_key_pressed(engine, AF_KEY_ESCAPE)) {
        if (af_engine_mouse_captured(engine)) (void)af_engine_set_mouse_capture(engine, false);
        else af_engine_request_quit(engine);
        return;
    }
    if (af_engine_mouse_pressed(engine, AF_MOUSE_LEFT) && !af_engine_mouse_captured(engine))
        (void)af_engine_set_mouse_capture(engine, true);
    if (af_engine_key_pressed(engine, AF_KEY_SPACE)) game->paused = !game->paused;
    if (af_engine_key_pressed(engine, AF_KEY_R)) reset_view(game);
    if (af_engine_mouse_captured(engine)) {
        af_vec2 motion = af_engine_mouse_delta(engine);
        af_camera_rotate(&game->camera, motion.x * 0.0025f, -motion.y * 0.0025f);
    }
    af_vec3 movement = {
        (float)af_engine_key_down(engine, AF_KEY_D) - (float)af_engine_key_down(engine, AF_KEY_A),
        (float)af_engine_key_down(engine, AF_KEY_E) - (float)af_engine_key_down(engine, AF_KEY_Q),
        (float)af_engine_key_down(engine, AF_KEY_W) - (float)af_engine_key_down(engine, AF_KEY_S)
    };
    if (af_vec3_length(movement) > 0) {
        float speed = af_engine_key_down(engine, AF_KEY_LEFT_SHIFT) ? 9.0f : 3.0f;
        af_vec3 forward = af_camera_forward(&game->camera);
        af_vec3 right = af_vec3_normalize(af_vec3_cross(forward, (af_vec3){0,1,0}));
        af_vec3 direction = af_vec3_add(af_vec3_scale(forward, movement.z), af_vec3_scale(right, movement.x));
        direction.y += movement.y;
        float length = af_vec3_length(direction);
        if (length > 0)
            af_camera_move(&game->camera, af_vec3_scale(movement, speed * (float)delta / length));
    }
    (void)af_engine_set_camera(engine, &game->camera);
    if (!game->paused) game->elapsed += delta;
    if (!update_objects(game)) {
        game->failed = true;
        af_engine_request_quit(engine);
        return;
    }
    float dusk = (float)(0.5 + 0.5 * sin(game->elapsed * 0.3));
    af_engine_set_clear_color(engine, (af_color){
        .r = 0.025f + dusk * 0.14f, .g = 0.018f + dusk * 0.035f,
        .b = 0.055f + dusk * 0.065f, .a = 1.0f
    });
}

static void game_shutdown(af_engine *engine, void *user_data)
{
    sandbox *game = user_data;
    if (game->ready && !game->failed && game->save_path) {
        af_scene_error error;
        if (!af_scene_save(game->scene, game->save_path, &error)) {
            fprintf(stderr, "Scene save failed: %s\n", error.message);
            game->failed = true;
        } else printf("Saved scene: %s\n", game->save_path);
    }
    af_engine_set_scene(engine, NULL);
    af_scene_destroy(game->scene);
    game->scene = NULL;
    af_engine_stats stats = af_engine_get_stats(engine);
    printf("Rendered %" PRIu64 " frames; %u validation errors.\n",
        stats.rendered_frames, stats.validation_errors);
}

int main(int argc, char **argv)
{
    sandbox state = {0};
    af_engine_config config = af_engine_config_default();
    config.title = "Afterlight | Sandbox";
    config.show_fps = true;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0) {
            puts("Usage: afterlight_sandbox [--scene PATH] [--save-scene PATH] [--frames N] [--validation | --no-validation]");
            return 0;
        }
        if (strcmp(argv[i], "--scene") == 0 && i + 1 < argc) {
            state.load_path = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--save-scene") == 0 && i + 1 < argc) {
            state.save_path = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--validation") == 0) config.enable_validation = true;
        else if (strcmp(argv[i], "--no-validation") == 0) config.enable_validation = false;
        else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            char *end = NULL;
            const char *value = argv[++i];
            errno = 0;
            unsigned long long frames = strtoull(value, &end, 10);
            if (errno || *value < '0' || *value > '9' || *end || frames == 0) {
                fputs("--frames requires a positive integer.\n", stderr);
                return 1;
            }
            state.frame_limit = (uint64_t)frames;
        } else {
            fprintf(stderr, "Unknown or incomplete argument: %s\n", argv[i]);
            return 1;
        }
    }
    af_engine *engine = af_engine_create(&config);
    if (!engine) return 1;
    af_game game = {
        .init = game_init, .update = game_update, .shutdown = game_shutdown, .user_data = &state
    };
    int result = af_engine_run(engine, &game);
    af_engine_destroy(engine);
    return state.failed ? 1 : result;
}
