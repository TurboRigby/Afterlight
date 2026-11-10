/* SPDX-License-Identifier: MPL-2.0 */
#include <afterlight/physics.h>
#include <afterlight/water.h>
#include <math.h>
#include <stdio.h>
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            fprintf(stderr, "Channel %d: %s\n", __LINE__, #x);                                                         \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
int main(int argc, char **argv) {
    CHECK(argc == 3);
    for (unsigned scene = 0; scene < 2; ++scene) {
        af_scene *s = af_scene_load(argv[scene + 1], NULL);
        af_physics *p = af_physics_create();
        CHECK(s && p);
        af_object_id first = 0;
        af_vec3 start = {0};
        for (size_t i = 0; i < af_scene_object_count(s); ++i) {
            const af_scene_object *o = af_scene_object_at(s, i);
            if (o->body.enabled) {
                first = o->id;
                start = o->transform.position;
                break;
            }
        }
        for (unsigned tick = 0; tick < 360; ++tick) {
            CHECK(af_physics_step(p, s, 1.f / 60));
            for (size_t i = 0; i < af_scene_object_count(s); ++i) {
                const af_scene_object *o = af_scene_object_at(s, i);
                if (!o->body.enabled)
                    continue;
                af_physics_state b = af_physics_get_state(p, o->id);
                CHECK(isfinite(b.position.x) && b.position.y > -3 && b.position.y < 5 && fabsf(b.position.x) < 15 &&
                      fabsf(b.position.z) < 15);
            }
        }
        af_vec3 end = af_physics_get_state(p, first).position;
        CHECK(end.z - start.z > 2);
        printf("Channel %u downstream travel %.3f.\n", scene, end.z - start.z);
        af_physics_destroy(p);
        af_scene_destroy(s);
    }
    return 0;
}
