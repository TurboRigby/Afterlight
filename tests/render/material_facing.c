/* SPDX-License-Identifier: MPL-2.0 */
#include "render/render_test.h"
#include <afterlight/model.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct state {
    af_scene *scene;
    af_object_id single, two, near, far;
    unsigned phase, frames;
    bool failed;
} state;

static bool set_pose(state *s, af_object_id id, float z, bool back, bool mirrored) {
    af_transform t = af_transform_identity();
    t.position.z = z;
    t.rotation.y = back ? 3.14159265f : 0;
    t.scale.x = mirrored ? -1 : 1;
    return af_scene_set_object_transform(s->scene, id, t);
}

static bool set_color(state *s, af_object_id id, af_color color, af_alpha_mode mode, bool unlit) {
    af_material m = af_material_default();
    m.base_color = color;
    m.alpha_mode = mode;
    m.unlit = unlit;
    return af_scene_set_object_material(s->scene, id, m);
}

static bool init(af_engine *e, void *data) {
    state *s = data;
    af_asset_error error = {0};
    if (!af_engine_load_scene_assets(e, s->scene, &error)) {
        fprintf(stderr, "%s\n", error.message);
        return false;
    }
    af_engine_set_scene(e, s->scene);
    af_engine_set_simulation_paused(e, true);
    af_render_settings r = af_render_settings_default();
    r.environment_enabled = r.skybox = false;
    af_directional_light sun = {.direction = {0, 0, -1}, .color = {1, 1, 1}, .intensity = 1};
    af_shadow_settings shadow = af_shadow_settings_default();
    shadow.mode = AF_SHADOW_SINGLE;
    shadow.resolution = 512;
    af_camera camera = af_camera_default();
    camera.position = (af_vec3){0, 0, 4};
    return af_engine_set_camera(e, &camera) && af_scene_set_light(s->scene, sun) &&
           af_scene_set_shadows(s->scene, shadow) && af_scene_set_render_settings(s->scene, r) &&
           set_pose(s, s->single, 1, true, false) &&
           af_scene_set_object_visible(s->scene, s->two, false) &&
           set_color(s, s->single, (af_color){1, 0, 0, 1}, AF_ALPHA_OPAQUE, true) &&
           set_color(s, s->near, (af_color){0, 1, 0, 1}, AF_ALPHA_BLEND, true) &&
           set_color(s, s->far, (af_color){0, 0, 1, 1}, AF_ALPHA_BLEND, true);
}

#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            fprintf(stderr, "glTF material phase %u line %d: %s\n", s->phase, __LINE__, #c);                              \
            s->failed = true;                                                                                          \
            af_engine_request_quit(e);                                                                                 \
            return;                                                                                                    \
        }                                                                                                              \
    } while (0)

static void tick(af_engine *e, double delta, void *data) {
    (void)delta;
    state *s = data;
    if (++s->frames < 4)
        return;
    if (s->frames == 4) {
        int width, height;
        af_engine_get_window_size(e, &width, &height);
        CHECK(af_engine_test_capture_pixel(e, (af_vec2){(float)width / 2, (float)height / 2}));
        return;
    }
    unsigned char p[4];
    CHECK(af_engine_test_read_pixel(e, p));
    printf("glTF material phase %u: %u %u %u\n", s->phase, p[0], p[1], p[2]);
    switch (s->phase) {
    case 0:
        /* The two blended primitives share a pivot. Mesh depth must win over draw order. */
        CHECK(p[0] < 5 && p[1] > 240 && p[2] < 5);
        CHECK(set_pose(s, s->single, 1, false, true));
        break;
    case 1:
        CHECK(p[0] > 240 && p[1] < 5 && p[2] < 5);
        CHECK(af_scene_set_object_visible(s->scene, s->single, false));
        CHECK(set_color(s, s->near, (af_color){0, 1, 0, .5f}, AF_ALPHA_BLEND, true));
        break;
    case 2:
        CHECK(p[0] < 5 && p[1] > 140 && p[2] > 140 && abs((int)p[1] - (int)p[2]) < 4);
        CHECK(af_scene_set_object_visible(s->scene, s->near, false));
        CHECK(af_scene_set_object_visible(s->scene, s->far, false));
        CHECK(af_scene_set_object_visible(s->scene, s->two, true));
        CHECK(set_color(s, s->two, (af_color){1, 1, 1, 1}, AF_ALPHA_OPAQUE, false));
        CHECK(set_pose(s, s->two, 0, true, false));
        break;
    case 3: {
        CHECK(p[0] > 240);
        af_camera camera = af_camera_default();
        camera.position = (af_vec3){2.5f, 0, 4};
        camera.yaw = atan2f(-4, -2.5f);
        CHECK(af_engine_set_camera(e, &camera));
        CHECK(af_scene_set_object_visible(s->scene, s->single, true));
        CHECK(set_pose(s, s->single, 1.5f, false, true));
        break;
    }
    case 4:
    case 6:
        CHECK(p[0] < 40);
        CHECK(set_pose(s, s->single, 1.5f, true, false));
        break;
    case 5: {
        CHECK(p[0] > 240);
        af_render_settings r = af_scene_get_render_settings(s->scene);
        r.ray_traced_shadows = true;
        CHECK(af_scene_set_render_settings(s->scene, r));
        CHECK(set_pose(s, s->single, 1.5f, false, true));
        break;
    }
    case 7: {
        CHECK(p[0] > 240);
        af_engine_stats stats = af_engine_get_stats(e);
        CHECK(stats.ray_traced_shadows_active == stats.ray_query_supported);
        CHECK(!stats.validation_errors && !stats.missing_meshes && !stats.missing_textures);
        af_engine_request_quit(e);
        break;
    }
    }
    ++s->phase;
    s->frames = 0;
}

int main(int argc, char **argv) {
    if (argc != 2)
        return 1;
    state s = {.scene = af_scene_create()};
    af_asset_error error = {0};
    if (!s.scene || !af_scene_import_model(s.scene, "Material fixture", argv[1], NULL, &error)) {
        fprintf(stderr, "%s\n", error.message);
        af_scene_destroy(s.scene);
        return 1;
    }
    af_object_id primitives[4];
    size_t count = 0;
    for (size_t i = 0; i < af_scene_object_count(s.scene) && count < 4; ++i) {
        const af_scene_object *o = af_scene_object_at(s.scene, i);
        if (strcmp(o->mesh, "__node") != 0)
            primitives[count++] = o->id;
    }
    if (count != 4) {
        af_scene_destroy(s.scene);
        return 1;
    }
    s.single = primitives[0];
    s.two = primitives[1];
    s.near = primitives[2];
    s.far = primitives[3];
    af_engine_config config = af_engine_config_default();
    config.title = "glTF material facing";
    config.width = config.height = 256;
    config.enable_validation = true;
    af_engine *e = af_engine_create(&config);
    af_game game = {.init = init, .update = tick, .user_data = &s};
    int rc = e ? af_engine_run(e, &game) : 1;
    af_engine_destroy(e);
    af_scene_destroy(s.scene);
    return rc || s.failed || s.phase != 8;
}
