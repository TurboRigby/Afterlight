/* SPDX-License-Identifier: MPL-2.0 */
#include "runtime/runtime.h"
#include <stdio.h>
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            fprintf(stderr, "Water progress %d: %s (%s)\n", __LINE__, #x, r.status);                                   \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
int main(int argc, char **argv) {
    ed_runtime r = {0};
    CHECK(argc == 3);
    af_scene *s = af_scene_load(argv[1], NULL);
    CHECK(s && af_scene_save(s, "course.afterlight.json", NULL) &&
          ed_runtime_set_project(&r, "game.afterlight.project", "course.afterlight.json") &&
          ed_runtime_start_file(&r, s, NULL, argv[2]));
    CHECK(ed_runtime_save_game(&r, "course.save") && ed_runtime_load_game(&r, "course.save"));
    af_object_id player = 0;
    for (size_t i = 0; i < af_scene_object_count(s); ++i)
        if (af_scene_object_at(s, i)->gameplay.player.enabled)
            player = af_scene_object_at(s, i)->id;
    CHECK(player);
    af_transform t = af_scene_get_object(s, player)->transform;
    t.position.x += 1;
    CHECK(af_scene_set_object_transform(s, player, t) && af_scene_save(s, "course.afterlight.json", NULL));
    CHECK(!ed_runtime_load_game(&r, "course.save"));
    CHECK(af_scene_get_object(r.scene, player)->transform.position.x == t.position.x - 1);
    ed_runtime_stop(&r);
    af_scene_destroy(s);
    remove("course.save");
    remove("course.afterlight.json");
    puts(
        "Water progress: normal restore succeeds; changed authored scene rejects stale state and preserves the running "
        "course.");
    return 0;
}
