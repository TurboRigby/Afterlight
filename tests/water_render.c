/* SPDX-License-Identifier: MPL-2.0 */

#include "render_test.h"
#include <afterlight/afterlight.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct state {
    af_scene *scene;
    af_object_id water;
    unsigned ticks, phase;
    bool failed;
    unsigned char green[4], red[4], clear[4];
} state;
#define CHECK(x)                                                                                        \
    do {                                                                                                \
        if (!(x)) {                                                                                     \
            fprintf(stderr, "Water render phase %u line %d: %s pixel %u,%u,%u\n", s->phase, __LINE__,   \
                    #x, p[0], p[1], p[2]);                                                              \
            s->failed = true;                                                                           \
            af_engine_request_quit(e);                                                                  \
            return;                                                                                     \
        }                                                                                               \
    } while (0)
static bool init(af_engine *e, void *data) {
    state *s = data;
    s->scene = af_scene_create();
    if (!s->scene)
        return false;
    s->water = af_scene_add_object(s->scene, "Water", "__node");
    af_water w = af_water_default();
    w.wind = 0;
    w.scattering = (af_vec3){.4f, .4f, .4f};
    w.absorption = (af_vec3){.5f, .5f, .5f};
    if (!af_scene_set_water(s->scene, s->water, w))
        return false;
    af_directional_light l = af_directional_light_default();
    l.direction = (af_vec3){0, -1, 0};
    l.intensity = 0;
    l.ambient = 0;
    af_scene_set_light(s->scene, l);
    af_shadow_settings shadows = af_shadow_settings_default();
    shadows.resolution = 512;
    af_scene_set_shadows(s->scene, shadows);
    af_camera c = af_camera_default();
    c.position = (af_vec3){0, 4, 5};
    c.yaw = -AF_PI / 2;
    c.pitch = -atan2f(4, 5);
    af_engine_set_camera(e, &c);
    af_engine_set_scene(e, s->scene);
    af_engine_set_clear_color(e, (af_color){0, 0, 0, 1});
    return true;
}
static void update(af_engine *e, double dt, void *data) {
    (void)dt;
    state *s = data;
    if (++s->ticks < 4)
        return;
    unsigned char p[4] = {0};
    if (s->phase)
        CHECK(af_engine_test_read_pixel(e, p));
    af_directional_light l = af_scene_get_light(s->scene);
    af_water w = *af_scene_get_water(s->scene, s->water);
    switch (s->phase) {
    case 0:
        break;
    case 1:
        CHECK(p[0] < 3 && p[1] < 3 && p[2] < 3);
        l.intensity = 3;
        l.color = (af_vec3){0, 1, 0};
        break;
    case 2:
        CHECK(p[1] > 35 && p[1] > p[0] + 30);
        memcpy(s->green, p, 4);
        l.intensity = 0;
        {
            af_local_light light = af_local_light_default(AF_LIGHT_POINT);
            light.position = (af_vec3){0, 2, 0};
            light.color = (af_vec3){1, 0, 0};
            light.intensity = 15;
            light.range = 10;
            CHECK(af_scene_add_local_light(s->scene, light));
        }
        break;
    case 3:
        CHECK(p[0] > 35 && p[0] > p[1] + 30);
        memcpy(s->red, p, 4);
        {
            af_local_light light = *af_scene_local_light_at(s->scene, 0);
            light.intensity = 1;
            CHECK(af_scene_set_local_light(s->scene, 0, light));
        }
        break;
    case 4:
        CHECK(p[0] + 20 < s->red[0]);
        CHECK(af_scene_remove_local_light(s->scene, 0));
        af_engine_set_clear_color(e, (af_color){1, 1, 1, 1});
        w.absorption = (af_vec3){0, 0, 0};
        break;
    case 5:
        CHECK(p[0] > 200 && abs((int)p[0] - p[2]) < 3);
        memcpy(s->clear, p, 4);
        w.absorption = (af_vec3){2, .2f, .02f};
        break;
    case 6:
        CHECK(p[0] + 70 < p[2] && p[2] > 100 && p[0] + 70 < s->clear[0]);
        w.enabled = false;
        break;
    case 7:
        CHECK(p[0] > 200 && abs((int)p[0] - p[2]) < 3);
        w.enabled = true;
        w.quality = AF_WATER_HIGH;
        break;
    case 8:
        CHECK(p[0] + 70 < p[2]);
        CHECK(af_engine_disturb_water(e, s->water, (af_vec3){0}, 1, 1));
        break;
    case 9:
        CHECK(af_engine_water_stats(e).cells == 4096 && af_engine_water_stats(e).max_wave > 0);
        puts("Water rendering: dark/green sun/red point light, intensity, channel absorption, disable, "
             "high quality and waves passed.");
        af_engine_request_quit(e);
        return;
    default:
        CHECK(false);
    }
    CHECK(af_scene_set_light(s->scene, l));
    CHECK(af_scene_set_water(s->scene, s->water, w));
    CHECK(af_engine_test_capture_pixel(e, (af_vec2){200, 150}));
    ++s->phase;
}
static void shutdown(af_engine *e, void *data) {
    state *s = data;
    af_engine_set_scene(e, NULL);
    af_scene_destroy(s->scene);
}
int main(void) {
    state s = {0};
    af_engine_config c = af_engine_config_default();
    c.width = 400;
    c.height = 300;
    c.title = "Afterlight water optics regression";
    c.enable_validation = true;
    af_engine *e = af_engine_create(&c);
    if (!e)
        return 1;
    af_game game = {.init = init, .update = update, .shutdown = shutdown, .user_data = &s};
    int r = af_engine_run(e, &game);
    af_engine_stats stats = af_engine_get_stats(e);
    af_engine_destroy(e);
    return r || s.failed || s.phase != 9 || stats.validation_errors ? 1 : 0;
}
