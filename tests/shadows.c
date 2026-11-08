/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <afterlight/afterlight.h>
#include "render_test.h"
#include "shadow_math.h"
#include "cube.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct test_state {
    af_scene *scene;
    af_camera camera;
    af_object_id cube, ground;
    unsigned step;
    bool failed;
    unsigned char shadow[4], lit[4], seam[4];
    af_object_id parent;
} test_state;
#define VERIFY(c)                                                                                                      \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            fprintf(stderr, "Shadow pixel step %u line %d: %s\n", s->step, __LINE__, #c);                              \
            s->failed = true;                                                                                          \
            af_engine_request_quit(engine);                                                                            \
            return;                                                                                                    \
        }                                                                                                              \
    } while (0)
static const af_vertex plane_vertices[] = {{.position = {-6, 0, -6}, .color = {1, 1, 1, 1}, .normal = {0, 1, 0}},
                                           {.position = {6, 0, -6}, .color = {1, 1, 1, 1}, .normal = {0, 1, 0}},
                                           {.position = {6, 0, 6}, .color = {1, 1, 1, 1}, .normal = {0, 1, 0}},
                                           {.position = {-6, 0, 6}, .color = {1, 1, 1, 1}, .normal = {0, 1, 0}}};
static const uint32_t indices[] = {0, 2, 1, 0, 3, 2};
static bool init(af_engine *engine, void *data) {
    test_state *s = data;
    s->scene = af_scene_create();
    if (!s->scene)
        return false;
    s->cube = af_scene_add_object(s->scene, "Occluder", "cube");
    s->ground = af_scene_add_object(s->scene, "Receiver", "plane");
    af_transform transform = af_transform_identity();
    transform.position.y = 1;
    af_scene_set_object_transform(s->scene, s->cube, transform);
    af_mesh_data cube = cube_mesh_data(), plane = {plane_vertices, 4, indices, 6};
    if (!af_engine_upload_mesh(engine, "cube", &cube) || !af_engine_upload_mesh(engine, "plane", &plane))
        return false;
    s->camera = af_camera_default();
    s->camera.far_clip = 500;
    s->camera.position = (af_vec3){-5, 8, 10};
    s->camera.yaw = atan2f(-10, 5);
    s->camera.pitch = atan2f(-8, sqrtf(125));
    af_directional_light light = af_directional_light_default();
    light.ambient = 0.12f;
    af_scene_set_light(s->scene, light);
    af_shadow_settings settings = af_shadow_settings_default();
    settings.mode = AF_SHADOW_SINGLE;
    af_scene_set_shadows(s->scene, settings);
    af_engine_set_scene(engine, s->scene);
    return af_engine_set_camera(engine, &s->camera);
}
static af_vec2 project(af_engine *engine, test_state *s, af_vec3 point) {
    int w, h;
    af_engine_get_window_size(engine, &w, &h);
    af_mat4 vp = af_mat4_mul(
        af_mat4_perspective(s->camera.vertical_fov, (float)w / (float)h, s->camera.near_clip, s->camera.far_clip),
        af_camera_view(&s->camera));
    af_vec4 p = af_mat4_transform(vp, (af_vec4){point.x, point.y, point.z, 1});
    return (af_vec2){(p.x / p.w + 1) * (float)w / 2, (p.y / p.w + 1) * (float)h / 2};
}
static void request(af_engine *engine, test_state *s, af_vec3 point) {
    if (!af_engine_test_capture_pixel(engine, project(engine, s, point))) {
        s->failed = true;
        af_engine_request_quit(engine);
    }
}
static void update(af_engine *engine, double delta, void *data) {
    (void)delta;
    test_state *s = data;
    ++s->step;
    unsigned char pixel[4];
    af_vec3 sample = {-1, 0, -0.6f};
    if (s->step == 3)
        request(engine, s, sample);
    if (s->step == 4) {
        VERIFY(af_engine_test_read_pixel(engine, s->shadow));
        af_shadow_settings settings = af_scene_get_shadows(s->scene);
        settings.enabled = false;
        VERIFY(af_scene_set_shadows(s->scene, settings));
        request(engine, s, sample);
    }
    if (s->step == 5) {
        VERIFY(af_engine_test_read_pixel(engine, s->lit));
        printf("Single-map pixel: shadow %u/%u/%u, lit %u/%u/%u\n", s->shadow[0], s->shadow[1], s->shadow[2], s->lit[0],
               s->lit[1], s->lit[2]);
        VERIFY(s->shadow[0] + 30 < s->lit[0] && s->lit[0] > 180);
        af_shadow_settings settings = af_scene_get_shadows(s->scene);
        settings.enabled = true;
        VERIFY(af_scene_set_shadows(s->scene, settings));
        af_material material = af_material_default();
        material.receive_shadows = false;
        VERIFY(af_scene_set_object_material(s->scene, s->ground, material));
        request(engine, s, sample);
    }
    if (s->step == 6) {
        VERIFY(af_engine_test_read_pixel(engine, pixel));
        VERIFY(abs((int)pixel[0] - (int)s->lit[0]) < 4);
        af_material material = af_material_default();
        VERIFY(af_scene_set_object_material(s->scene, s->ground, material));
        material.cast_shadows = false;
        VERIFY(af_scene_set_object_material(s->scene, s->cube, material));
        request(engine, s, sample);
    }
    if (s->step == 7) {
        VERIFY(af_engine_test_read_pixel(engine, pixel));
        VERIFY(abs((int)pixel[0] - (int)s->lit[0]) < 4);
        af_material material = af_material_default();
        VERIFY(af_scene_set_object_material(s->scene, s->cube, material));
        VERIFY(af_scene_set_object_visible(s->scene, s->cube, false));
        request(engine, s, sample);
    }
    if (s->step == 8) {
        VERIFY(af_engine_test_read_pixel(engine, pixel));
        VERIFY(abs((int)pixel[0] - (int)s->lit[0]) < 4);
        VERIFY(af_scene_set_object_visible(s->scene, s->cube, true));
        af_material material = af_material_default();
        material.unlit = true;
        VERIFY(af_scene_set_object_material(s->scene, s->ground, material));
        request(engine, s, sample);
    }
    if (s->step == 9) {
        VERIFY(af_engine_test_read_pixel(engine, pixel));
        VERIFY(pixel[0] > 245);
        VERIFY(af_scene_set_object_material(s->scene, s->ground, af_material_default()));
        af_transform moved = af_transform_identity();
        moved.position = (af_vec3){3, 1, 0};
        VERIFY(af_scene_set_object_transform(s->scene, s->cube, moved));
        request(engine, s, sample);
    }
    if (s->step == 10) {
        VERIFY(af_engine_test_read_pixel(engine, pixel));
        VERIFY(abs((int)pixel[0] - (int)s->lit[0]) < 4);
        af_shadow_settings settings = af_scene_get_shadows(s->scene);
        settings.resolution = 512;
        settings.filter_radius = 2;
        VERIFY(af_scene_set_shadows(s->scene, settings));
        VERIFY(af_engine_set_window_size(engine, 800, 500));
    }
    if (s->step == 11) {
        const unsigned char cut[] = {255, 255, 255, 0};
        VERIFY(af_engine_upload_texture(engine, "cutout", cut, 1, 1));
        af_transform t = af_transform_identity();
        t.position.y = 1;
        VERIFY(af_scene_set_object_transform(s->scene, s->cube, t));
        af_material m = af_material_default();
        m.alpha_mode = AF_ALPHA_MASK;
        snprintf(m.texture, sizeof(m.texture), "cutout");
        VERIFY(af_scene_set_object_material(s->scene, s->cube, m));
        request(engine, s, sample);
    }
    if (s->step == 12) {
        VERIFY(af_engine_test_read_pixel(engine, pixel) && abs((int)pixel[0] - (int)s->lit[0]) < 4);
        const unsigned char solid[] = {255, 255, 255, 255};
        VERIFY(af_engine_upload_texture(engine, "cutout", solid, 1, 1));
        request(engine, s, sample);
    }
    if (s->step == 13) {
        VERIFY(af_engine_test_read_pixel(engine, pixel) && pixel[0] + 25 < s->lit[0]);
        af_material m = af_scene_get_object(s->scene, s->cube)->material;
        m.base_color.a = .25f;
        VERIFY(af_scene_set_object_material(s->scene, s->cube, m));
        request(engine, s, sample);
    }
    if (s->step == 14) {
        VERIFY(af_engine_test_read_pixel(engine, pixel) && abs((int)pixel[0] - (int)s->lit[0]) < 4);
        af_material m = af_scene_get_object(s->scene, s->cube)->material;
        m.alpha_mode = AF_ALPHA_BLEND;
        VERIFY(af_scene_set_object_material(s->scene, s->cube, m));
        request(engine, s, sample);
    }
    if (s->step == 15) {
        VERIFY(af_engine_test_read_pixel(engine, pixel) && abs((int)pixel[0] - (int)s->lit[0]) < 4);
        VERIFY(af_scene_set_object_material(s->scene, s->cube, af_material_default()));
        af_transform t = af_transform_identity();
        t.position = (af_vec3){3, 1, 0};
        VERIFY(af_scene_set_object_transform(s->scene, s->cube, t));
    }
    if (s->step == 20) {
        VERIFY(af_engine_get_stats(engine).shadow_resolution == 512 &&
               af_engine_get_stats(engine).shadow_cached_passes > 0);
        af_shadow_settings settings = af_scene_get_shadows(s->scene);
        settings.mode = AF_SHADOW_CASCADED;
        settings.resolution = 2048;
        settings.filter_radius = 1;
        VERIFY(af_scene_set_shadows(s->scene, settings));
        af_transform transform = af_transform_identity();
        transform.position.y = 1;
        VERIFY(af_scene_set_object_transform(s->scene, s->cube, transform));
        request(engine, s, sample);
    }
    if (s->step == 21) {
        VERIFY(af_engine_test_read_pixel(engine, pixel));
        VERIFY(pixel[0] + 30 < s->lit[0]);
        VERIFY(af_engine_get_stats(engine).shadow_cascades == 4);
        af_shadow_settings settings = af_scene_get_shadows(s->scene);
        settings.resolution = 512;
        settings.filter_radius = 2;
        VERIFY(af_scene_set_shadows(s->scene, settings));
        request(engine, s, (af_vec3){-1.64f, 0, -0.6f});
    }
    if (s->step == 22) {
        VERIFY(af_engine_test_read_pixel(engine, pixel));
        printf("Filtered edge pixel: %u/%u/%u\n", pixel[0], pixel[1], pixel[2]);
        VERIFY(pixel[0] > 110 && pixel[0] < 245);
        af_shadow_settings settings = af_scene_get_shadows(s->scene);
        settings.resolution = 2048;
        settings.filter_radius = 1;
        VERIFY(af_scene_set_shadows(s->scene, settings));
    }
    if (s->step >= 23 && s->step <= 27) {
        if (s->step >= 24) {
            VERIFY(af_engine_test_read_pixel(engine, pixel));
            unsigned layer = s->step - 24;
            printf("Cascade %u debug pixel: %u/%u/%u\n", layer, pixel[0], pixel[1], pixel[2]);
            if (layer == 0)
                VERIFY(pixel[0] > pixel[1] + 10);
            if (layer == 1)
                VERIFY(pixel[1] > pixel[0] + 10);
            if (layer == 2)
                VERIFY(pixel[2] > pixel[0] + 10);
            if (layer == 3)
                VERIFY(pixel[0] > pixel[2] + 10 && pixel[1] > pixel[2] + 10);
        }
        if (s->step <= 26) {
            static const float distances[] = {300, 120, 40, 20};
            af_shadow_settings settings = af_scene_get_shadows(s->scene);
            settings.debug_cascades = true;
            settings.distance = distances[s->step - 23];
            VERIFY(af_scene_set_shadows(s->scene, settings));
            request(engine, s, sample);
        }
    }
    if (s->step == 28) {
        af_shadow_settings settings = af_scene_get_shadows(s->scene);
        settings.debug_cascades = false;
        settings.blend_fraction = 0.2f;
        float depth = af_vec3_dot(af_vec3_sub(sample, s->camera.position), af_camera_forward(&s->camera));
        float lo = 30, hi = 150;
        af_shadow_view view;
        int w, h;
        af_engine_get_window_size(engine, &w, &h);
        for (unsigned i = 0; i < 30; ++i) {
            settings.distance = (lo + hi) / 2;
            VERIFY(af_shadow_build(&s->camera, (float)w / (float)h, af_scene_get_light(s->scene).direction, settings,
                                   NULL, 0, &view));
            if (view.splits[1] < depth)
                lo = settings.distance;
            else
                hi = settings.distance;
        }
        settings.distance = (lo + hi) / 2;
        VERIFY(af_scene_set_shadows(s->scene, settings));
        s->camera.position = af_vec3_sub(s->camera.position, af_vec3_scale(af_camera_forward(&s->camera), 0.025f));
        VERIFY(af_engine_set_camera(engine, &s->camera));
        request(engine, s, sample);
    }
    if (s->step == 29) {
        VERIFY(af_engine_test_read_pixel(engine, s->seam));
        s->camera.position = af_vec3_add(s->camera.position, af_vec3_scale(af_camera_forward(&s->camera), 0.05f));
        VERIFY(af_engine_set_camera(engine, &s->camera));
        request(engine, s, sample);
    }
    if (s->step == 30) {
        VERIFY(af_engine_test_read_pixel(engine, pixel));
        VERIFY(pixel[0] + 30 < s->lit[0] && abs((int)pixel[0] - (int)s->seam[0]) < 8);
        s->camera.position = af_vec3_sub(s->camera.position, af_vec3_scale(af_camera_forward(&s->camera), 0.025f));
        VERIFY(af_engine_set_camera(engine, &s->camera));
        af_shadow_settings settings = af_scene_get_shadows(s->scene);
        settings.distance = 40;
        settings.blend_fraction = 0.1f;
        VERIFY(af_scene_set_shadows(s->scene, settings));
        s->parent = af_scene_add_object(s->scene, "Moving parent", "unloaded-group");
        VERIFY(s->parent && af_scene_set_parent(s->scene, s->cube, s->parent));
        af_transform transform = af_transform_identity();
        transform.position = (af_vec3){11, 19, 7.4f};
        VERIFY(af_scene_set_object_transform(s->scene, s->parent, transform));
        request(engine, s, sample);
    }
    if (s->step == 31) {
        VERIFY(af_engine_test_read_pixel(engine, pixel));
        printf("Offscreen parented caster: %u/%u/%u\n", pixel[0], pixel[1], pixel[2]);
        VERIFY(pixel[0] + 30 < s->lit[0]);
        VERIFY(af_scene_set_object_visible(s->scene, s->parent, false));
        request(engine, s, sample);
    }
    if (s->step == 32) {
        VERIFY(af_engine_test_read_pixel(engine, pixel));
        VERIFY(abs((int)pixel[0] - (int)s->lit[0]) < 4);
        VERIFY(af_scene_set_object_visible(s->scene, s->parent, true));
        af_transform transform = af_transform_identity();
        transform.position.x = 3;
        VERIFY(af_scene_set_object_transform(s->scene, s->parent, transform));
        request(engine, s, sample);
    }
    if (s->step == 33) {
        VERIFY(af_engine_test_read_pixel(engine, pixel));
        VERIFY(abs((int)pixel[0] - (int)s->lit[0]) < 4);
        af_directional_light light = af_scene_get_light(s->scene);
        light.direction = (af_vec3){0, -1, 0};
        VERIFY(af_scene_set_light(s->scene, light));
        request(engine, s, sample);
    }
    if (s->step == 34) {
        VERIFY(af_engine_test_read_pixel(engine, pixel));
        VERIFY(pixel[0] > 180);
        af_shadow_settings settings = af_scene_get_shadows(s->scene);
        settings.resolution = 1024;
        settings.filter_radius = 0;
        VERIFY(af_scene_set_shadows(s->scene, settings));
    }
    if (s->step == 35) {
        af_shadow_settings settings = af_scene_get_shadows(s->scene);
        settings.resolution = 512;
        settings.filter_radius = 2;
        VERIFY(af_scene_set_shadows(s->scene, settings));
    }
    if (s->step == 36) {
        af_shadow_settings settings = af_scene_get_shadows(s->scene);
        settings.mode = AF_SHADOW_SINGLE;
        settings.resolution = 2048;
        VERIFY(af_scene_set_shadows(s->scene, settings));
    }
    if (s->step == 37) {
        af_shadow_settings settings = af_scene_get_shadows(s->scene);
        settings.mode = AF_SHADOW_CASCADED;
        VERIFY(af_scene_set_shadows(s->scene, settings));
    }
    if (s->step == 38)
        af_engine_set_scene(engine, NULL);
    if (s->step == 40) {
        af_engine_set_scene(engine, s->scene);
        af_shadow_settings settings = af_scene_get_shadows(s->scene);
        settings.distance = 160;
        settings.filter_radius = 1;
        VERIFY(af_scene_set_shadows(s->scene, settings));
        af_transform ground = af_transform_identity();
        ground.scale = (af_vec3){20, 1, 20};
        VERIFY(af_scene_set_object_transform(s->scene, s->ground, ground));
        af_object_id distant = af_scene_add_object(s->scene, "Distant occluder", "cube");
        af_transform transform = af_transform_identity();
        transform.position = (af_vec3){30, 1, -60};
        VERIFY(distant && af_scene_set_object_transform(s->scene, distant, transform));
        af_directional_light light = af_directional_light_default();
        light.ambient = 0.12f;
        VERIFY(af_scene_set_light(s->scene, light));
        s->camera.position = (af_vec3){8, 18, 20};
        s->camera.yaw = -AF_PI / 2;
        s->camera.pitch = atan2f(-18, 65);
        VERIFY(af_engine_set_camera(engine, &s->camera));
        request(engine, s, (af_vec3){29, 0, -60.6f});
    }
    if (s->step == 41) {
        VERIFY(af_engine_test_read_pixel(engine, pixel));
        printf("Distant shadow pixel: %u/%u/%u\n", pixel[0], pixel[1], pixel[2]);
        VERIFY(pixel[0] + 25 < s->lit[0]);
        af_shadow_settings settings = af_scene_get_shadows(s->scene);
        settings.enabled = false;
        VERIFY(af_scene_set_shadows(s->scene, settings));
        request(engine, s, (af_vec3){29, 0, -60.6f});
    }
    if (s->step == 42) {
        VERIFY(af_engine_test_read_pixel(engine, pixel));
        VERIFY(pixel[0] > 180 && abs((int)pixel[0] - (int)s->lit[0]) < 4);
        af_engine_request_quit(engine);
    }
}
static void shutdown(af_engine *engine, void *data) {
    test_state *s = data;
    af_engine_set_scene(engine, NULL);
    af_scene_destroy(s->scene);
}
int main(void) {
    af_engine_config config = af_engine_config_default();
    config.title = "Afterlight shadow regression";
    config.width = 640;
    config.height = 480;
    config.enable_validation = true;
    af_engine *engine = af_engine_create(&config);
    if (!engine)
        return 1;
    test_state state = {0};
    af_game game = {.init = init, .update = update, .shutdown = shutdown, .user_data = &state};
    int result = af_engine_run(engine, &game);
    af_engine_stats stats = af_engine_get_stats(engine);
    af_engine_destroy(engine);
    const char *required = getenv("AFTERLIGHT_REQUIRE_VALIDATION");
    if (required && !strcmp(required, "1") && !stats.validation_enabled)
        return 1;
    if (result || state.failed || stats.validation_errors)
        return 1;
    puts("Shadows: single-map pixels; four cascade selections, seam blending, offscreen/parented/moving/distant "
         "casters, visibility, resize, quality changes and scene detach passed.");
    return 0;
}
