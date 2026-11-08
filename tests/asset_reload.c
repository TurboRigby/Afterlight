/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <afterlight/asset_watch.h>
#include <afterlight/model.h>
#include "render_test.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct state {
    af_scene *scene;
    af_asset_watch *watch;
    const char *fixtures;
    unsigned frame;
    bool failed, saw_asset_error;
    char source[4096];
    unsigned char before[4], green[4];
    size_t meshes, textures;
} state;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            fprintf(stderr, "Reload frame %u line %d: %s\n", s->frame, __LINE__, #c);                                  \
            s->failed = true;                                                                                          \
            af_engine_request_quit(engine);                                                                            \
            return;                                                                                                    \
        }                                                                                                              \
    } while (0)
static const af_vertex vertices[] = {{{-2, -2, 0}, {1, 1, 1, 1}, {0, 0, 1}, {0, 0}},
                                     {{2, -2, 0}, {1, 1, 1, 1}, {0, 0, 1}, {1, 0}},
                                     {{2, 2, 0}, {1, 1, 1, 1}, {0, 0, 1}, {1, 1}},
                                     {{-2, 2, 0}, {1, 1, 1, 1}, {0, 0, 1}, {0, 1}}};
static const uint32_t indices[] = {0, 1, 2, 0, 2, 3};
static bool copy_fixture(state *s, const char *name, const char *target) {
    char from[4096];
    snprintf(from, sizeof(from), "%s/%s", s->fixtures, name);
    return SDL_CopyFile(from, target);
}
static bool init(af_engine *engine, void *memory) {
    state *s = memory;
    const char *names[] = {"rich.gltf", "triangle.bin", "base.png", "normal.png", "surface.png"};
    for (unsigned i = 0; i < 5; ++i)
        if (!copy_fixture(s, names[i], names[i]))
            return false;
    if (!af_asset_resolve_path(NULL, "rich.gltf", s->source, sizeof(s->source)))
        return false;
    s->scene = af_scene_create();
    af_asset_error error;
    if (!s->scene || !af_scene_import_model(s->scene, "rich", s->source, NULL, &error) ||
        !af_engine_load_scene_assets(engine, s->scene, &error))
        return false;
    af_object_id surface = af_scene_add_object(s->scene, "Reload sample", "quad");
    af_mesh_data mesh = {vertices, 4, indices, 6};
    if (!af_engine_upload_mesh(engine, "quad", &mesh))
        return false;
    af_material material = af_material_default();
    material.unlit = true;
    snprintf(material.texture, sizeof(material.texture), "rich_i0_0");
    if (!af_scene_set_object_material(s->scene, surface, material))
        return false;
    af_camera camera = af_camera_default();
    camera.position = (af_vec3){0, 0, 4};
    camera.yaw = -AF_PI / 2;
    camera.pitch = 0;
    af_engine_set_camera(engine, &camera);
    af_shadow_settings shadows = af_shadow_settings_default();
    shadows.enabled = false;
    af_scene_set_shadows(s->scene, shadows);
    af_engine_set_scene(engine, s->scene);
    s->watch = af_asset_watch_create();
    s->meshes = af_engine_get_stats(engine).loaded_meshes;
    s->textures = af_engine_get_stats(engine).loaded_textures;
    return s->watch != NULL;
}
static void capture(af_engine *engine, state *s) {
    CHECK(af_engine_test_capture_pixel(engine, (af_vec2){320, 240}));
}
static void update(af_engine *engine, double delta, void *memory) {
    (void)delta;
    state *s = memory;
    ++s->frame;
    af_asset_error message;
    af_asset_watch_update(s->watch, engine, s->scene, .3, &message);
    if (strstr(message.message, "Asset 'rich_") && strstr(message.message, "rich.gltf"))
        s->saw_asset_error = true;
    unsigned char pixel[4];
    if (s->frame == 3) {
        CHECK(af_asset_watch_get_stats(s->watch).sources == 1);
        capture(engine, s);
    }
    if (s->frame == 4) {
        CHECK(af_engine_test_read_pixel(engine, s->before));
        CHECK(s->before[0] > 200 && s->before[1] < 150);
        CHECK(copy_fixture(s, "green.png", "base.png"));
    }
    if (s->frame == 10) {
        CHECK(af_asset_watch_get_stats(s->watch).reloads == 1);
        capture(engine, s);
    }
    if (s->frame == 11) {
        CHECK(af_engine_test_read_pixel(engine, s->green));
        CHECK(s->green[1] > 220 && s->green[0] < 60);
        FILE *f = fopen("normal.png", "wb");
        CHECK(f && fputs("truncated image", f) >= 0 && fclose(f) == 0);
    }
    if (s->frame == 17) {
        CHECK(af_asset_watch_get_stats(s->watch).rejections == 1 && s->saw_asset_error);
        capture(engine, s);
    }
    if (s->frame == 18) {
        CHECK(af_engine_test_read_pixel(engine, pixel));
        CHECK(abs((int)pixel[1] - (int)s->green[1]) < 3);
        CHECK(copy_fixture(s, "normal.png", "normal.png"));
    }
    if (s->frame == 24) {
        CHECK(af_asset_watch_get_stats(s->watch).reloads == 2);
        CHECK(af_engine_get_stats(engine).loaded_meshes == s->meshes &&
              af_engine_get_stats(engine).loaded_textures == s->textures);
        CHECK(af_engine_get_stats(engine).missing_meshes == 0 && af_engine_get_stats(engine).missing_textures == 0);
        af_engine_request_quit(engine);
    }
}
static void shutdown(af_engine *engine, void *memory) {
    state *s = memory;
    af_engine_set_scene(engine, NULL);
    af_asset_watch_destroy(s->watch);
    af_scene_destroy(s->scene);
}
int main(int argc, char **argv) {
    if (argc != 2)
        return 1;
    state s = {.fixtures = argv[1]};
    af_engine_config config = af_engine_config_default();
    config.width = 640;
    config.height = 480;
    config.enable_validation = true;
    config.title = "Afterlight transactional asset reload";
    af_engine *engine = af_engine_create(&config);
    if (!engine)
        return 1;
    af_game game = {.init = init, .update = update, .shutdown = shutdown, .user_data = &s};
    int result = af_engine_run(engine, &game);
    af_engine_stats stats = af_engine_get_stats(engine);
    af_engine_destroy(engine);
    if (result || s.failed || stats.validation_errors)
        return 1;
    puts("Asset reload: dependency watch, grouped model GPU replacement, real pixel changes, malformed-image last-good "
         "retention, recovery and stable resource counts passed.");
    return 0;
}
