/* SPDX-License-Identifier: MPL-2.0 */
#include <afterlight/physics.h>
#include <afterlight/water.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            fprintf(stderr, "Water scene %u tick %u line %d: %s (%s)\n", scene, tick, __LINE__, #x,                    \
                    af_physics_error(p));                                                                              \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
int main(int argc, char **argv) {
    if (argc < 3)
        return 2;
    af_physics *p = af_physics_create();
    unsigned tick = 0;
    for (unsigned scene = 0; scene < (unsigned)argc - 1; ++scene) {
        af_scene_error error;
        af_scene *s = af_scene_load(argv[scene + 1], &error);
        if (!s)
            fprintf(stderr, "%s\n", error.message);
        CHECK(s);
        af_physics_reset(p);
        for (tick = 0; tick < 600; ++tick) {
            CHECK(af_physics_step(p, s, 1.f / 60));
            for (size_t i = 0; i < af_scene_object_count(s); ++i) {
                const af_scene_object *o = af_scene_object_at(s, i);
                if (!o->body.enabled)
                    continue;
                af_physics_state b = af_physics_get_state(p, o->id);
                CHECK(b.available && isfinite(b.position.y) && b.position.y > -2.5f && b.position.y < 10);
            }
        }
        size_t floating = 0, sunk = 0;
        for (size_t i = 0; i < af_scene_object_count(s); ++i) {
            const af_scene_object *o = af_scene_object_at(s, i);
            if (!o->body.enabled)
                continue;
            if (strstr(o->name, "Stone")) {
                CHECK(o->transform.position.y < -1.5f);
                ++sunk;
            } else {
                CHECK(o->transform.position.y > -.5f && o->transform.position.y < .6f);
                ++floating;
            }
        }
        CHECK(floating >= 2 && sunk >= 1 && af_physics_water_stats(p).max_wave < .5f);
        printf("Water scene %u: %zu floating, %zu sunk, %.3f ms last tick.\n", scene, floating, sunk,
               af_physics_water_stats(p).milliseconds);
        af_scene_destroy(s);
    }
    af_physics_destroy(p);
    return 0;
}
