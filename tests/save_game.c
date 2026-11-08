/* SPDX-License-Identifier: MPL-2.0 */
#include "runtime.h"
#include <SDL3/SDL_filesystem.h>
#include <stdio.h>
#include <string.h>
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            fprintf(stderr, "Save game %d: %s (%s)\n", __LINE__, #x, r.status);                                        \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
int main(int argc, char **argv) {
    ed_runtime r = {0};
    CHECK(argc == 3);
    af_scene *scene = af_scene_create();
    CHECK(scene);
    af_object_id id = af_scene_add_object(scene, "Counter", "cube");
    CHECK(af_scene_save(scene, "progress.afterlight.json", NULL));
    CHECK(ed_runtime_set_project(&r, "progress.afterlight.project", "progress.afterlight.json"));
    CHECK(SDL_CopyFile(argv[1], "state.module") && ed_runtime_start_file(&r, scene, NULL, "state.module"));
    ed_runtime_update(&r, .01, false);
    ed_runtime_update(&r, .01, false);
    CHECK(ed_runtime_save_game(&r, "progress.save"));
    ed_runtime_update(&r, .01, false);
    r.paused = true;
    CHECK(ed_runtime_load_game(&r, "progress.save") && r.paused && !strcmp(r.current_scene, "progress"));
    r.paused = false;
    ed_runtime_update(&r, .01, false);
    CHECK(af_scene_get_object(r.scene, id)->transform.position.x == 3);
    FILE *f = fopen("progress.save", "r+b");
    CHECK(f && fseek(f, 224, SEEK_SET) == 0 && fputc(255, f) != EOF && fclose(f) == 0);
    CHECK(!ed_runtime_load_game(&r, "progress.save") && af_scene_get_object(r.scene, id)->transform.position.x == 3);
    CHECK(ed_runtime_save_game(&r, "progress.save"));
    CHECK(!ed_runtime_save_game(&r, "missing/folder/save"));
    CHECK(SDL_CopyFile(argv[2], "state.module") && !ed_runtime_load_game(&r, "progress.save"));
    ed_runtime_update(&r, .01, false);
    CHECK(af_scene_get_object(r.scene, id)->transform.position.x == 4);
    ed_runtime_stop(&r);
    af_scene_destroy(scene);
    remove("state.module");
    remove("progress.save");
    remove("progress.afterlight.json");
    puts("Progress saves: scene/module/version, paused restore, corruption "
         "rejection, incompatible module rollback and last-good continuation "
         "passed.");
    return 0;
}
