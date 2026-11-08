/* SPDX-License-Identifier: MPL-2.0 */
#include "cube.h"
#include "render_test.h"
#include <afterlight/afterlight.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct state {
    af_scene *scene;
    af_camera camera;
    af_object_id box, ground;
    unsigned phase, ticks;
    bool pending, failed, hardware, benchmark;
    double frame_ms, shadow_ms, scene_ms, build_ms;
    unsigned samples;
    unsigned char shadow[4], lit[4];
    uint64_t blas, tlas;
} state;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            fprintf(stderr, "Ray shadows phase %u line %d: %s\n", s->phase, __LINE__, #x);                             \
            s->failed = true;                                                                                          \
            af_engine_request_quit(e);                                                                                 \
            return;                                                                                                    \
        }                                                                                                              \
    } while (0)
static const af_vertex vertices[] = {{.position = {-6, 0, -6}, .normal = {0, 1, 0}, .color = {1, 1, 1, 1}},
                                     {.position = {6, 0, -6}, .normal = {0, 1, 0}, .color = {1, 1, 1, 1}},
                                     {.position = {6, 0, 6}, .normal = {0, 1, 0}, .color = {1, 1, 1, 1}},
                                     {.position = {-6, 0, 6}, .normal = {0, 1, 0}, .color = {1, 1, 1, 1}}};
static const uint32_t indices[] = {0, 2, 1, 0, 3, 2};
static bool init(af_engine *e, void *memory) {
    state *s = memory;
    s->scene = af_scene_create();
    if (!s->scene)
        return false;
    af_mesh_data cube = cube_mesh_data(), plane = {vertices, 4, indices, 6};
    if (!af_engine_upload_mesh(e, "cube", &cube) || !af_engine_upload_mesh(e, "plane", &plane))
        return false;
    s->box = af_scene_add_object(s->scene, "Opaque caster", "cube");
    s->ground = af_scene_add_object(s->scene, "Receiver", "plane");
    af_transform t = af_transform_identity();
    t.position.y = 1;
    af_scene_set_object_transform(s->scene, s->box, t);
    s->camera = af_camera_default();
    s->camera.position = (af_vec3){-5, 8, 10};
    s->camera.yaw = atan2f(-10, 5);
    s->camera.pitch = atan2f(-8, sqrtf(125));
    af_shadow_settings sh = af_shadow_settings_default();
    sh.mode = AF_SHADOW_SINGLE;
    sh.resolution = 512;
    sh.filter_radius = 0;
    af_scene_set_shadows(s->scene, sh);
    af_directional_light light = af_directional_light_default();
    light.ambient = .12f;
    af_scene_set_light(s->scene, light);
    af_engine_set_scene(e, s->scene);
    af_engine_set_camera(e, &s->camera);
    af_engine_set_simulation_paused(e, true);
    return s->box && s->ground;
}
static af_vec2 sample(af_engine *e, state *s) {
    int w, h;
    af_engine_get_window_size(e, &w, &h);
    af_mat4 vp = af_mat4_mul(
        af_mat4_perspective(s->camera.vertical_fov, (float)w / (float)h, s->camera.near_clip, s->camera.far_clip),
        af_camera_view(&s->camera));
    af_vec4 p = af_mat4_transform(vp, (af_vec4){-1, 0, -.6f, 1});
    return (af_vec2){(p.x / p.w + 1) * (float)w / 2, (p.y / p.w + 1) * (float)h / 2};
}
static void update(af_engine *e, double dt, void *memory) {
    (void)dt;
    state *s = memory;
    ++s->ticks;
    if (s->benchmark && s->phase <= 1 && s->ticks > 20 && s->ticks < 90) {
        af_engine_stats timing = af_engine_get_stats(e);
        s->frame_ms += timing.gpu_frame_ms;
        s->shadow_ms += timing.gpu_shadow_ms;
        s->scene_ms += timing.gpu_scene_ms;
        s->build_ms += timing.gpu_ray_build_ms;
        ++s->samples;
    }
    if (s->ticks < (s->benchmark && s->phase <= 1 ? 90u : 8u))
        return;
    if (!s->pending) {
        CHECK(af_engine_test_capture_pixel(e, sample(e, s)));
        s->pending = true;
        return;
    }
    unsigned char p[4];
    CHECK(af_engine_test_read_pixel(e, p));
    af_engine_stats stats = af_engine_get_stats(e);
    bool expected = stats.ray_query_supported && !getenv("AFTERLIGHT_TEST_RAY_FAIL_ALLOC") &&
                    !getenv("AFTERLIGHT_TEST_RAY_PIPELINE_FAIL");
    printf("Ray phase %u: pixel %u/%u/%u active %u AS %llu instances %zu BLAS "
           "%llu TLAS %llu build %.6f ms\n",
           s->phase, p[0], p[1], p[2], stats.ray_traced_shadows_active, (unsigned long long)stats.ray_tracing_bytes,
           stats.ray_instances, (unsigned long long)stats.ray_blas_builds, (unsigned long long)stats.ray_tlas_builds,
           stats.gpu_ray_build_ms);
    if (s->benchmark && s->phase <= 1) {
        printf("RT_BENCH,%s,%u,%.6f,%.6f,%.6f,%.6f,%llu,%llu\n", s->phase ? "ray_query" : "raster", s->samples,
               s->frame_ms / s->samples, s->shadow_ms / s->samples, s->scene_ms / s->samples, s->build_ms / s->samples,
               (unsigned long long)stats.tracked_gpu_bytes, (unsigned long long)stats.ray_tracing_bytes);
        s->frame_ms = s->shadow_ms = s->scene_ms = s->build_ms = 0;
        s->samples = 0;
    }
    if (s->phase == 0) {
        memcpy(s->shadow, p, 4);
        CHECK(!stats.ray_traced_shadows_active && stats.ray_tracing_bytes == 0);
    } else if (s->phase == 1) {
        CHECK(stats.ray_traced_shadows_active == expected);
        if (getenv("AFTERLIGHT_REQUIRE_RAY_QUERY"))
            CHECK(stats.ray_query_supported);
        if (expected) {
            CHECK(stats.ray_instances == 2 && stats.ray_meshes == 2 && stats.ray_tracing_bytes > 0 &&
                  stats.ray_blas_builds == 2 && stats.ray_tlas_builds > 0);
            s->blas = stats.ray_blas_builds;
            s->tlas = stats.ray_tlas_builds;
        }
        if (stats.ray_query_supported &&
            (getenv("AFTERLIGHT_TEST_RAY_FAIL_ALLOC") || getenv("AFTERLIGHT_TEST_RAY_PIPELINE_FAIL")))
            CHECK(stats.ray_tracing_failed);
        CHECK(p[0] < 150);
        s->hardware = expected;
    } else if (s->phase == 2) {
        memcpy(s->lit, p, 4);
        CHECK(p[0] > 180 && p[0] > s->shadow[0] + 30);
        if (s->hardware)
            CHECK(stats.ray_blas_builds == s->blas && stats.ray_tlas_builds > s->tlas);
    } else if (s->phase == 4 || s->phase == 5 || s->phase == 8 || s->phase == 9 || s->phase == 13 || s->phase == 16) {
        CHECK(p[0] + 5 >= s->lit[0]);
        if (s->phase == 13 && s->hardware)
            CHECK(stats.ray_instances == 0);
    } else
        CHECK(p[0] + 30 < s->lit[0]);
    if ((s->phase == 5 || s->phase == 7) && s->hardware)
        CHECK(stats.ray_raster_casters == 1 && stats.ray_instances == 1);
    if (s->phase == 11 || s->phase == 14 || s->phase == 16)
        CHECK(stats.ray_tracing_bytes == 0 && !stats.ray_traced_shadows_active);
    if (s->phase == 16) {
        af_engine_request_quit(e);
        s->phase = 17;
        return;
    }
    ++s->phase;
    af_render_settings r = af_scene_get_render_settings(s->scene);
    af_material m = af_material_default();
    af_transform t = af_transform_identity();
    t.position.y = 1;
    switch (s->phase) {
    case 1:
        r.ray_traced_shadows = true;
        CHECK(af_scene_set_render_settings(s->scene, r));
        if (expected && !s->benchmark) {
            af_shadow_settings sh = af_scene_get_shadows(s->scene);
            sh.distance = 1;
            CHECK(af_scene_set_shadows(s->scene, sh));
        }
        break;
    case 2:
        t.position.x = 3;
        CHECK(af_scene_set_object_transform(s->scene, s->box, t));
        break;
    case 3:
        CHECK(af_scene_set_object_transform(s->scene, s->box, t));
        break;
    case 4:
        CHECK(af_scene_set_object_visible(s->scene, s->box, false));
        break;
    case 5: {
        af_shadow_settings sh = af_scene_get_shadows(s->scene);
        sh.distance = 40;
        CHECK(af_scene_set_shadows(s->scene, sh));
        const unsigned char image[] = {255, 255, 255, 0};
        CHECK(af_scene_set_object_visible(s->scene, s->box, true) &&
              af_engine_upload_texture(e, "cutout", image, 1, 1));
        m.alpha_mode = AF_ALPHA_MASK;
        strcpy(m.texture, "cutout");
        CHECK(af_scene_set_object_material(s->scene, s->box, m));
        break;
    }
    case 6: {
        const unsigned char image[] = {255, 255, 255, 255};
        CHECK(af_engine_upload_texture(e, "cutout", image, 1, 1));
        break;
    }
    case 7: {
        CHECK(af_scene_set_object_material(s->scene, s->box, m));
        af_vertex skinned[24];
        memcpy(skinned, cube_vertices, sizeof(skinned));
        for (unsigned i = 0; i < 24; ++i)
            skinned[i].weights.x = 1;
        af_mesh_data mesh = cube_mesh_data();
        mesh.vertices = skinned;
        af_mat4 identity = af_mat4_identity();
        CHECK(af_engine_upload_mesh(e, "cube", &mesh) && af_engine_set_skin_palette(e, s->box, &identity, 1));
        break;
    }
    case 8:
        m.cast_shadows = false;
        CHECK(af_scene_set_object_material(s->scene, s->box, m));
        break;
    case 9:
        CHECK(af_scene_set_object_material(s->scene, s->box, m));
        m.receive_shadows = false;
        CHECK(af_scene_set_object_material(s->scene, s->ground, m));
        break;
    case 10: {
        CHECK(af_scene_set_object_material(s->scene, s->ground, m));
        af_mesh_data cube = cube_mesh_data();
        CHECK(af_engine_upload_mesh(e, "cube", &cube));
        break;
    }
    case 11:
        r.ray_traced_shadows = false;
        CHECK(af_scene_set_render_settings(s->scene, r));
        break;
    case 12:
        r.ray_traced_shadows = true;
        CHECK(af_scene_set_render_settings(s->scene, r) && af_engine_set_window_size(e, 800, 500));
        break;
    case 13:
        m.cast_shadows = false;
        CHECK(af_scene_set_object_material(s->scene, s->box, m) &&
              af_scene_set_object_material(s->scene, s->ground, m));
        break;
    case 14:
        CHECK(af_scene_set_object_material(s->scene, s->box, m) &&
              af_scene_set_object_material(s->scene, s->ground, m));
        af_engine_set_scene(e, NULL);
        break;
    case 15: {
        af_object_id parent = af_scene_add_object(s->scene, "Parent transform", "__node");
        af_transform root = af_transform_identity();
        root.position.x = 2;
        CHECK(parent && af_scene_set_object_transform(s->scene, parent, root) &&
              af_scene_set_parent(s->scene, s->box, parent));
        t.position.x = -2;
        t.scale.x = -1;
        CHECK(af_scene_set_object_transform(s->scene, s->box, t));
        af_engine_set_scene(e, s->scene);
        r.anti_aliasing = AF_AA_TAA;
        CHECK(af_scene_set_render_settings(s->scene, r));
        break;
    }
    case 16: {
        af_shadow_settings sh = af_scene_get_shadows(s->scene);
        sh.enabled = false;
        CHECK(af_scene_set_shadows(s->scene, sh));
        break;
    }
    }
    s->ticks = 0;
    s->pending = false;
}
static void shutdown(af_engine *e, void *memory) {
    state *s = memory;
    af_engine_set_scene(e, NULL);
    af_scene_destroy(s->scene);
}
int main(int argc, char **argv) {
    bool benchmark = argc == 2 && !strcmp(argv[1], "--benchmark");
    if (argc > 1 && !benchmark)
        return 1;
    af_engine_config c = af_engine_config_default();
    c.title = "Afterlight hardware ray-query shadows";
    c.width = 640;
    c.height = 480;
    c.enable_validation = !benchmark;
    af_engine *e = af_engine_create(&c);
    if (!e)
        return 1;
    state s = {.benchmark = benchmark};
    af_game game = {.init = init, .update = update, .shutdown = shutdown, .user_data = &s};
    int result = af_engine_run(e, &game);
    af_engine_stats stats = af_engine_get_stats(e);
    af_engine_destroy(e);
    if (result || s.failed || s.phase != 17 || stats.validation_errors)
        return 1;
    puts("Ray shadows: real/fallback pixels, rigid movement, cutout maps, "
         "visibility, receive/cast flags, reupload, resize, empty TLAS, AA and "
         "resource release passed.");
    return 0;
}
