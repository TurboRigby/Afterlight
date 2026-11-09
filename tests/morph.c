/* SPDX-License-Identifier: MPL-2.0 */
#include "render_test.h"
#include <afterlight/model.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct fixture {
    af_scene *scene;
    af_object_id root;
    unsigned phase, frames;
    bool failed;
} fixture;

static bool sample_weights(const char *path) {
    af_animation_file animation = {0};
    af_animation_pose pose[2];
    if (!af_animation_file_load(path, &animation, NULL))
        return false;
    bool ok = animation.node_count == 2 && animation.clip_count == 3 && animation.bind_pose[0].morph_count == 1 &&
              animation.bind_pose[0].morph_weights[0] == .25f && animation.bind_pose[1].morph_weights[0] == 1 &&
              af_animation_sample(&animation, 0, .5, pose, 2) && pose[0].morph_weights[0] == .5f &&
              pose[1].morph_weights[0] == 1 && af_animation_sample(&animation, 1, .5, pose, 2) &&
              pose[0].morph_weights[0] == 0 && af_animation_sample(&animation, 1, 1, pose, 2) &&
              pose[0].morph_weights[0] == 1 && af_animation_sample(&animation, 2, .5, pose, 2) &&
              pose[0].morph_weights[0] == .75f;
    af_animation_player player = af_animation_player_default();
    player.loop = false;
    ok = ok && af_animation_player_play(&player, &animation, 0, 0) &&
         af_animation_player_seek(&player, &animation, 1) && af_animation_player_play(&player, &animation, 1, 1) &&
         af_animation_player_update(&player, &animation, .5) &&
         af_animation_player_sample(&player, &animation, pose, 2) && pose[0].morph_weights[0] == .5f &&
         pose[1].morph_weights[0] == 1;
    af_animation_player_destroy(&player);
    af_animation_file_free(&animation);
    af_mesh_file flat = {0};
    ok = ok && af_mesh_file_load_gltf(path, &flat, NULL) && flat.data.vertex_count == 6 &&
         fabsf(flat.data.vertices[0].position.x + .75f) < 1e-5f && fabsf(flat.data.vertices[3].position.x) < 1e-5f;
    af_mesh_file_free(&flat);
    return ok;
}

static bool init(af_engine *engine, void *data) {
    fixture *f = data;
    af_asset_error error = {0};
    if (!af_engine_load_scene_assets(engine, f->scene, &error)) {
        fprintf(stderr, "%s\n", error.message);
        return false;
    }
    af_engine_set_scene(engine, f->scene);
    af_engine_set_simulation_paused(engine, true);
    af_engine_set_animation_playback(engine, false, false);
    af_engine_set_clear_color(engine, (af_color){0, 0, 0, 1});
    af_camera camera = af_camera_default();
    return af_engine_set_camera(engine, &camera);
}

#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            fprintf(stderr, "Morph phase %u: %s\n", f->phase, #x);                                                     \
            f->failed = true;                                                                                          \
            af_engine_request_quit(engine);                                                                            \
            return;                                                                                                    \
        }                                                                                                              \
    } while (0)
static void update(af_engine *engine, double delta, void *data) {
    (void)delta;
    fixture *f = data;
    if (++f->frames < 4)
        return;
    static const af_vec2 points[] = {{-.4f, .55f}, {-.4f, .55f}, {.35f, .55f}, {.35f, -.65f}};
    int w, h;
    af_engine_get_window_size(engine, &w, &h);
    float scale = (float)h / (6 * tanf(AF_PI / 6));
    af_vec2 point = {(float)w * .5f + points[f->phase].x * scale, (float)h * .5f - points[f->phase].y * scale};
    if (f->frames == 4) {
        CHECK(af_engine_test_capture_pixel(engine, point));
        return;
    }
    unsigned char pixel[4];
    CHECK(af_engine_test_read_pixel(engine, pixel));
    CHECK(f->phase == 1 ? pixel[0] < 5 : pixel[0] > 200);
    af_engine_stats stats = af_engine_get_stats(engine);
    if (stats.ray_query_supported)
        CHECK(stats.ray_traced_shadows_active && stats.ray_raster_casters == 2 && !stats.ray_instances);
    if (f->phase == 0) {
        af_engine_set_animation_playback(engine, true, false);
        CHECK(af_engine_animation_play(engine, f->root, 0, 0));
        CHECK(af_engine_animation_seek(engine, f->root, 1));
        CHECK(af_engine_animation_pause(engine, f->root, true));
    } else {
        CHECK((af_engine_pick_object(engine, point) != AF_OBJECT_NONE) == (f->phase != 1));
    }
    f->frames = 0;
    if (++f->phase == 4)
        af_engine_request_quit(engine);
}

int main(int argc, char **argv) {
    bool cpu_only = argc == 3 && !strcmp(argv[2], "--cpu");
    if ((!cpu_only && argc != 2) || !sample_weights(argv[1]))
        return 1;
    fixture f = {.scene = af_scene_create()};
    af_asset_error error = {0};
    if (!af_scene_import_model(f.scene, "morph", argv[1], &f.root, &error)) {
        fprintf(stderr, "%s\n", error.message);
        af_scene_destroy(f.scene);
        return 1;
    }
    if (cpu_only) {
        af_scene_destroy(f.scene);
        return 0;
    }
    af_render_settings settings = af_scene_get_render_settings(f.scene);
    settings.ray_traced_shadows = true;
    if (!af_scene_set_render_settings(f.scene, settings)) {
        af_scene_destroy(f.scene);
        return 1;
    }
    af_engine_config config = af_engine_config_default();
    config.title = "glTF morph regression";
    config.width = 320;
    config.height = 240;
    config.enable_validation = true;
    af_engine *engine = af_engine_create(&config);
    af_game game = {.init = init, .update = update, .user_data = &f};
    int result = engine ? af_engine_run(engine, &game) : 1;
    af_engine_stats stats = af_engine_get_stats(engine);
    af_engine_destroy(engine);
    af_scene_destroy(f.scene);
    return result || f.failed || f.phase != 4 || stats.validation_errors;
}
