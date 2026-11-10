/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "runtime/runtime.h"
#include <SDL3/SDL_filesystem.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            fprintf(stderr, "Hot reload check %d: %s (%s)\n", __LINE__, #c, r.status);                                 \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
static void poll_change(ed_runtime *r) {
    ed_runtime_poll_reload(r, 0);
    ed_runtime_poll_reload(r, 0.4);
}
int main(int argc, char **argv) {
    ed_runtime r = {0};
    CHECK(argc == 5);
    af_scene *source = af_scene_create();
    CHECK(source);
    af_object_id id = af_scene_add_object(source, "Cube", "cube");
    CHECK(id);
    CHECK(SDL_CopyFile(argv[1], "watched.module"));
    CHECK(ed_runtime_start_file(&r, source, NULL, "watched.module"));
    char first_shadow[AF_ASSET_PATH_CAPACITY];
    snprintf(first_shadow, sizeof(first_shadow), "%s", r.shadow_path);
    ed_runtime_update(&r, 0.1, false);
    CHECK(fabsf(af_scene_get_object(r.scene, id)->transform.rotation.y - 0.07f) < 0.001f);
    CHECK(SDL_CopyFile(argv[2], "watched.module"));
    poll_change(&r);
    CHECK(r.reload_count == 1 && r.playing);
    SDL_PathInfo info;
    CHECK(!SDL_GetPathInfo(first_shadow, &info));
    ed_runtime_update(&r, 0.1, false);
    CHECK(fabsf(af_scene_get_object(r.scene, id)->transform.rotation.y - 0.27f) < 0.001f);
    CHECK(af_scene_get_object(source, id)->transform.rotation.y == 0);
    CHECK(SDL_CopyFile(argv[3], "watched.module"));
    poll_change(&r);
    CHECK(r.playing && r.reload_count == 1 && strstr(r.status, "Reload rejected"));
    ed_runtime_update(&r, 0.1, false);
    CHECK(fabsf(af_scene_get_object(r.scene, id)->transform.rotation.y - 0.47f) < 0.001f);
    CHECK(SDL_CopyFile(argv[4], "watched.module"));
    poll_change(&r);
    CHECK(r.playing && r.reload_count == 1 && af_scene_object_count(r.scene) == 1);
    FILE *file = fopen("watched.module", "wb");
    CHECK(file && fputs("truncated build", file) >= 0 && fclose(file) == 0);
    CHECK(!ed_runtime_reload(&r) && r.playing && r.reload_count == 1);
    CHECK(SDL_CopyFile(argv[1], "watched.module"));
    poll_change(&r);
    CHECK(r.reload_count == 2 && r.playing);
    CHECK(ed_runtime_reload(&r) && r.reload_count == 3);
    snprintf(first_shadow, sizeof(first_shadow), "%s", r.shadow_path);
    ed_runtime_stop(&r);
    CHECK(!r.library && !r.playing && !SDL_GetPathInfo(first_shadow, &info));
    CHECK(!ed_runtime_start_file(&r, source, NULL, "missing.module"));
    CHECK(af_scene_object_count(source) == 1);
    af_scene_destroy(source);
    puts("Hot reload: watched rebuild, changed C behavior, ABI/init/truncated rejection, last-good continuation, "
         "recovery and shadow cleanup passed.");
    return 0;
}
