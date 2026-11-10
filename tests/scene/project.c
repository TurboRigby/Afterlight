/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <afterlight/project.h>
#include "document/document.h"
#include "cJSON.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            fprintf(stderr, "Project/components %d: %s\n", __LINE__, #c);                                              \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
static bool write_text(const char *path, const char *text) {
    FILE *f = fopen(path, "wb");
    if (!f)
        return false;
    bool ok = fputs(text, f) >= 0;
    return fclose(f) == 0 && ok;
}
static char *read_text(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    char *s = calloc(65536, 1);
    size_t n = s ? fread(s, 1, 65535, f) : 0;
    fclose(f);
    if (!n) {
        free(s);
        return NULL;
    }
    return s;
}
int main(int argc, char **argv) {
    CHECK(argc == 3);
    af_project generated;
    af_asset_error error;
    CHECK(af_project_load(argv[1], &generated, &error));
    CHECK(strstr(generated.game_module, "playground") && strstr(generated.startup_scene, "obstacle"));
    af_project project = {.name = "Test project"};
    CHECK(af_asset_resolve_path(NULL, "scene.json", project.startup_scene, sizeof(project.startup_scene)));
    CHECK(af_asset_resolve_path(NULL, "game.module", project.game_module, sizeof(project.game_module)));
    CHECK(af_project_save("test.afterlight.project", &project, &error));
    af_project copy;
    CHECK(af_project_load("test.afterlight.project", &copy, &error) && !strcmp(copy.game_module, project.game_module) &&
          !strcmp(copy.startup_scene, project.startup_scene));
    char *text = read_text("test.afterlight.project");
    CHECK(text && strstr(text, "\"scene.json\"") && strstr(text, "\"game.module\""));
    free(text);
    CHECK(write_text("bad.project", "{\"format\":\"afterlight.project\",\"version\":1,\"version\":1,\"name\":\"x\","
                                    "\"startup_scene\":\"x\",\"game_module\":\"x\"}"));
    CHECK(!af_project_load("bad.project", &copy, &error) && !strcmp(copy.name, project.name));
    CHECK(write_text("bad.project", "{\"format\":\"afterlight.project\",\"version\":2,\"name\":\"x\",\"startup_scene\":"
                                    "\"x\",\"game_module\":\"x\"}"));
    CHECK(!af_project_load("bad.project", &copy, &error));
    FILE *f = fopen("bad.project", "wb");
    CHECK(f && fwrite("{}\0{}", 1, 5, f) == 5 && fclose(f) == 0 && !af_project_load("bad.project", &copy, &error));
    af_project invalid = project;
    invalid.name[0] = 0;
    CHECK(!af_project_save("test.afterlight.project", &invalid, &error) &&
          af_project_load("test.afterlight.project", &copy, &error) && !strcmp(copy.name, project.name));
    ed_document d;
    CHECK(ed_document_init(&d) && ed_document_add_cube(&d));
    af_object_id player = d.selection;
    CHECK(ed_document_add_cube(&d));
    af_object_id spawn = d.selection;
    af_gameplay_component g = af_gameplay_component_default();
    g.role = AF_ROLE_SPAWN;
    CHECK(ed_document_gameplay(&d, g));
    CHECK(ed_document_parent(&d, player));
    d.selection = player;
    g = af_gameplay_component_default();
    g.player.enabled = true;
    g.player.spawn = spawn;
    g.player.controller.speed = 6.25f;
    g.camera.enabled = g.camera.primary = true;
    g.camera.vertical_fov = 1.2f;
    ed_document_end_edit(&d);
    CHECK(ed_document_gameplay(&d, g));
    ed_document_end_edit(&d);
    CHECK(ed_document_undo(&d) && !af_scene_get_object(d.scene, player)->gameplay.player.enabled);
    CHECK(ed_document_redo(&d));
    CHECK(af_scene_get_object(d.scene, player)->collider.type == AF_COLLIDER_CAPSULE);
    CHECK(ed_document_duplicate(&d));
    af_object_id duplicated = d.selection;
    const af_scene_object *dup = af_scene_get_object(d.scene, duplicated);
    CHECK(dup->gameplay.player.spawn != spawn &&
          af_scene_get_object(d.scene, dup->gameplay.player.spawn)->parent == duplicated);
    CHECK(ed_document_save(&d, "components.json"));
    af_scene *loaded = af_scene_load("components.json", NULL);
    CHECK(loaded && af_scene_get_object(loaded, player)->gameplay.player.controller.speed == 6.25f &&
          af_scene_get_object(loaded, player)->gameplay.player.spawn == spawn);
    af_scene_destroy(loaded);
    g.player.controller.radius = NAN;
    CHECK(!ed_document_gameplay(&d, g));
    text = read_text("components.json");
    CHECK(text);
    cJSON *root = cJSON_Parse(text);
    free(text);
    CHECK(root);
    cJSON *component = cJSON_GetObjectItemCaseSensitive(
        cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(root, "objects"), 0), "gameplay");
    CHECK(component);
    cJSON_SetValuestring(cJSON_GetObjectItemCaseSensitive(component, "role"), "unknown");
    text = cJSON_Print(root);
    CHECK(text && write_text("bad-scene.json", text) && !af_scene_load("bad-scene.json", NULL));
    cJSON_free(text);
    cJSON_Delete(root);
    af_scene *legacy = af_scene_load(argv[2], NULL);
    CHECK(legacy && !af_scene_object_at(legacy, 0)->gameplay.player.enabled);
    af_scene_destroy(legacy);
    ed_document_destroy(&d);
    puts("Project/components: relative paths, strict fields/version/NUL, failed-save preservation, component "
         "editing/undo/clone/ID remap and scene round trip passed.");
    return 0;
}
