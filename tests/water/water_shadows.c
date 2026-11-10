/* SPDX-License-Identifier: MPL-2.0 */
#include "cube.h"
#include "render/render_test.h"
#include <afterlight/afterlight.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct fixture {
    af_scene *scene;
    af_camera camera;
    af_object_id bed;
    unsigned phase, ticks;
    unsigned char surface[4], floor[4];
    bool failed, ray;
} fixture;

static bool init(af_engine *e, void *data) {
    fixture *f = data;
    f->scene = af_scene_create();
    if (!f->scene)
        return false;

    af_mesh_data cube = cube_mesh_data();
    if (!af_engine_upload_mesh(e, "cube", &cube))
        return false;
    af_object_id caster = af_scene_add_object(f->scene, "Floating caster", "cube");
    f->bed = af_scene_add_object(f->scene, "Pool bed", "cube");
    af_object_id pool = af_scene_add_object(f->scene, "Pool", "__node");
    af_transform t = af_transform_identity();
    t.position = (af_vec3){0, .85f, 0};
    t.scale = (af_vec3){.7f, .7f, .7f};
    if (!af_scene_set_object_transform(f->scene, caster, t))
        return false;
    t.position = (af_vec3){0, -2.13f, 0};
    t.scale = (af_vec3){9, .2f, 9};
    if (!af_scene_set_object_transform(f->scene, f->bed, t))
        return false;
    af_material bed = af_material_default();
    bed.unlit = true;
    bed.cast_shadows = false;
    if (!af_scene_set_object_material(f->scene, f->bed, bed))
        return false;

    af_water water = af_water_default();
    water.half_size = (af_vec2){5, 5};
    water.depth = 2;
    water.wind = water.ripple_strength = 0;
    water.roughness = .04f;
    water.absorption = (af_vec3){.35f, .3f, .25f};
    water.scattering = (af_vec3){.1f, .55f, .65f};
    if (!af_scene_set_water(f->scene, pool, water))
        return false;

    af_directional_light sun = af_directional_light_default();
    sun.direction = (af_vec3){.7f, -1, 0};
    sun.intensity = 2;
    sun.ambient = .15f;
    if (!af_scene_set_light(f->scene, sun))
        return false;
    af_shadow_settings shadows = af_shadow_settings_default();
    shadows.resolution = 512;
    if (!af_scene_set_shadows(f->scene, shadows))
        return false;
    af_render_settings r = af_render_settings_default();
    r.ray_traced_shadows = f->ray;
    r.refraction = false;
    r.tone_mapping = true;
    if (!af_scene_set_render_settings(f->scene, r))
        return false;

    f->camera = af_camera_default();
    f->camera.position = (af_vec3){0, 4, 7};
    f->camera.yaw = -AF_PI / 2;
    f->camera.pitch = -atan2f(4, 7);
    af_engine_set_camera(e, &f->camera);
    af_engine_set_scene(e, f->scene);
    af_engine_set_simulation_paused(e, true);
    return true;
}

static af_vec2 project(af_engine *e, const fixture *f, af_vec3 p) {
    int w, h;
    af_engine_get_window_size(e, &w, &h);
    const af_camera *c = &f->camera;
    af_mat4 vp = af_mat4_mul(af_mat4_perspective(c->vertical_fov, (float)w / (float)h, c->near_clip, c->far_clip),
                             af_camera_view(c));
    af_vec4 clip = af_mat4_transform(vp, (af_vec4){p.x, p.y, p.z, 1});
    return (af_vec2){(clip.x / clip.w + 1) * (float)w / 2, (clip.y / clip.w + 1) * (float)h / 2};
}

#define CHECK(expr)                                                                                                    \
    do {                                                                                                               \
        if (!(expr)) {                                                                                                 \
            fprintf(stderr, "Water shadow phase %u: %s (pixel %u/%u/%u)\n", f->phase, #expr, pixel[0], pixel[1],       \
                    pixel[2]);                                                                                         \
            f->failed = true;                                                                                          \
            af_engine_request_quit(e);                                                                                 \
            return;                                                                                                    \
        }                                                                                                              \
    } while (0)

static void update(af_engine *e, double delta, void *data) {
    (void)delta;
    fixture *f = data;
    unsigned char pixel[4] = {0};
    if (++f->ticks < 4)
        return;
    if (f->ticks == 4) {
        // Sun travel is (+.7,-1,0). These points sit inside the caster's
        // projected silhouettes on the interface and the pool bed.
        af_vec3 point = f->phase < 2 ? (af_vec3){.595f, 0, 0} : (af_vec3){1.995f, -2, 0};
        CHECK(af_engine_test_capture_pixel(e, project(e, f, point)));
        return;
    }
    CHECK(af_engine_test_read_pixel(e, pixel));
    af_engine_stats stats = af_engine_get_stats(e);
    if (f->ray && getenv("AFTERLIGHT_REQUIRE_RAY_QUERY"))
        CHECK(stats.ray_query_supported);
    printf("Water shadow sample %u: %u/%u/%u\n", f->phase, pixel[0], pixel[1], pixel[2]);
    af_shadow_settings shadows = af_scene_get_shadows(f->scene);
    af_material bed = af_scene_get_object(f->scene, f->bed)->material;
    switch (f->phase) {
    case 0:
        for (unsigned i = 0; i < 4; ++i)
            f->surface[i] = pixel[i];
        shadows.enabled = false;
        CHECK(af_scene_set_shadows(f->scene, shadows));
        break;
    case 1:
        // A shadow on the unlit bed is impossible here. Darkening can only
        // come from incorrectly shadowing the whole volume at its interface.
        for (unsigned i = 0; i < 3; ++i)
            CHECK(abs(pixel[i] - f->surface[i]) <= 3);
        bed.unlit = false;
        CHECK(af_scene_set_object_material(f->scene, f->bed, bed));
        shadows.enabled = true;
        CHECK(af_scene_set_shadows(f->scene, shadows));
        break;
    case 2:
        for (unsigned i = 0; i < 4; ++i)
            f->floor[i] = pixel[i];
        shadows.enabled = false;
        CHECK(af_scene_set_shadows(f->scene, shadows));
        break;
    case 3:
        CHECK(pixel[1] > f->floor[1] + 15);
        af_engine_request_quit(e);
        break;
    }
    ++f->phase;
    f->ticks = 0;
}

static void shutdown(af_engine *e, void *data) {
    fixture *f = data;
    af_engine_set_scene(e, NULL);
    af_scene_destroy(f->scene);
}

int main(void) {
    af_engine_config c = af_engine_config_default();
    c.title = "Afterlight water shadow regression";
    c.width = 480;
    c.height = 360;
    c.enable_validation = true;
    af_engine *e = af_engine_create(&c);
    if (!e)
        return 1;
    fixture f = {.ray = getenv("AFTERLIGHT_TEST_RAY_WATER") != NULL};
    af_game game = {.init = init, .update = update, .shutdown = shutdown, .user_data = &f};
    int result = af_engine_run(e, &game);
    af_engine_stats stats = af_engine_get_stats(e);
    af_engine_destroy(e);
    return result || f.failed || f.phase != 4 || stats.validation_errors;
}
