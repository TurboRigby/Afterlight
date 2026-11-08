/* SPDX-License-Identifier: MPL-2.0 */
#include "animation_runtime.h"
#include <afterlight/model.h>
#include <stdio.h>
#include <math.h>
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            fprintf(stderr, "Driven runtime %d: %s\n", __LINE__, #x);                                                  \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
int main(int argc, char **argv) {
    CHECK(argc == 2);
    af_scene *scene = af_scene_create();
    af_object_id root;
    af_asset_error e;
    CHECK(af_scene_import_model(scene, "actor", argv[1], &root, &e));
    const af_scene_object *o = af_scene_get_object(scene, root);
    af_transform authored = o->transform;
    af_animation_runtime *r = af_animation_runtime_create(NULL);
    CHECK(r && af_animation_runtime_load(r, o->animation.asset, argv[1], &e));
    af_animation_runtime_scene(r, scene);
    CHECK(af_animation_runtime_root_motion(r, root, 10, true));
    for (unsigned i = 0; i < 90; ++i)
        CHECK(af_animation_runtime_locomotion(r, root, (af_locomotion_input){.speed = 3.5f, .grounded = true}, 1.f / 60,
                                              NULL));
    CHECK(af_animation_runtime_update(r, NULL, 1, 1.f / 60, true));
    af_animation_debug d = af_animation_runtime_debug(r, root);
    CHECK(d.driven && d.root_motion && d.motion.state == AF_LOCOMOTION_WALK && d.emitted_events >= 2 &&
          d.pending_events >= 2 && fabsf(d.weights[2] - .5f) < 1e-5f);
    af_animation_bone bones[9];
    CHECK(af_animation_runtime_bones(r, root, bones, 9) == 9);
    for (size_t i = 0; i < 9; ++i)
        CHECK(isfinite(bones[i].position.x) && bones[i].has_parent);
    af_animation_events events;
    CHECK(af_animation_runtime_events(r, root, &events) && events.count >= 2);
    CHECK(af_animation_runtime_events(r, root, &events) && events.count == 0);
    CHECK(af_animation_runtime_debug(r, root).emitted_events == d.emitted_events);
    CHECK(af_scene_get_object(scene, root)->transform.position.z == authored.position.z);
    CHECK(af_animation_runtime_seek(r, root, 0) && !af_animation_runtime_debug(r, root).driven);
    CHECK(af_animation_runtime_load(r, o->animation.asset, argv[1], &e) && !af_animation_runtime_debug(r, root).driven);
    af_animation_runtime_scene(r, NULL);
    CHECK(!af_animation_runtime_debug(r, root).available);
    af_animation_runtime_destroy(r);
    af_scene_destroy(scene);
    puts("Driven runtime: weights, pose bones, events without draining diagnostics, seek/reload and authored isolation "
         "passed.");
    return 0;
}
