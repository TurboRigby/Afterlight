/* SPDX-License-Identifier: MPL-2.0 */
#include "runtime.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            fprintf(stderr, "Transition %d: %s (%s)\n", __LINE__, #x, r.status);                                       \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
int main(int argc, char **argv) {
    ed_runtime r = {0};
    CHECK(argc == 2);
    af_scene *s = af_scene_create(), *next = af_scene_create();
    af_scene_add_object(s, "First", "cube");
    af_scene_add_object(next, "Second", "cube");
    af_scene_error e;
    CHECK(af_scene_save(next, "second.afterlight.json", &e));
    CHECK(ed_runtime_set_project(&r, "game.afterlight.project", "first.afterlight.json"));
    CHECK(ed_runtime_start_file(&r, s, NULL, argv[1]));
    CHECK(!ed_runtime_request_scene(&r, "../escape"));
    CHECK(ed_runtime_request_scene(&r, "missing") && !ed_runtime_transition(&r) &&
          !strcmp(af_scene_object_at(r.scene, 0)->name, "First"));
    CHECK(ed_runtime_request_scene(&r, "second"));
    CHECK(!strcmp(af_scene_object_at(r.scene, 0)->name, "First"));
    ed_runtime_update(&r, .01, false);
    CHECK(r.transition_count == 1 && !strcmp(af_scene_object_at(r.scene, 0)->name, "Second"));
    CHECK(r.host.runtime_context == &r && !strcmp(af_scene_object_at(s, 0)->name, "First"));
    ed_runtime_stop(&r);
    af_scene_destroy(s);
    af_scene_destroy(next);
    remove("second.afterlight.json");
    puts("Scene transitions: deferred swap, named local scenes, invalid paths, failed-load retention and source "
         "isolation passed.");
    return 0;
}
