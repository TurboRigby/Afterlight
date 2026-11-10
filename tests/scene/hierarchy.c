/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <afterlight/scene.h>
#include "document/document.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            fprintf(stderr, "Hierarchy check %d: %s\n", __LINE__, #c);                                                 \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
int main(void) {
    af_scene *s = af_scene_create();
    CHECK(s);
    af_object_id root = af_scene_add_object(s, "Parent", "cube"), child = af_scene_add_object(s, "Child", "cube"),
                 leaf = af_scene_add_object(s, "Leaf", "cube");
    af_transform t = af_transform_identity();
    t.position.x = 2;
    t.rotation.z = AF_PI / 2;
    t.scale = (af_vec3){2, 2, 2};
    CHECK(af_scene_set_object_transform(s, root, t));
    t = af_transform_identity();
    t.position.x = 1;
    CHECK(af_scene_set_object_transform(s, child, t));
    CHECK(af_scene_set_parent(s, child, root) && af_scene_set_parent(s, leaf, child));
    CHECK(af_scene_is_descendant(s, leaf, root) && !af_scene_is_descendant(s, root, leaf));
    CHECK(!af_scene_set_parent(s, root, leaf) && !af_scene_set_parent(s, child, child) &&
          !af_scene_set_parent(s, child, 999));
    af_mat4 world;
    CHECK(af_scene_world_matrix(s, child, &world));
    CHECK(fabsf(world.elements[12] - 2) < 0.001f && fabsf(world.elements[13] - 2) < 0.001f);
    CHECK(af_scene_object_visible(s, leaf));
    CHECK(af_scene_set_object_visible(s, root, false));
    CHECK(!af_scene_object_visible(s, leaf));
    CHECK(af_scene_save(s, "hierarchy.json", NULL));
    af_scene *copy = af_scene_load("hierarchy.json", NULL);
    CHECK(copy && af_scene_get_object(copy, leaf)->parent == child);
    af_scene_destroy(copy);
    /* Corrupt a valid v2 file into a self-cycle. */
    FILE *f = fopen("hierarchy.json", "rb");
    CHECK(f);
    CHECK(!fseek(f, 0, SEEK_END));
    long n = ftell(f);
    CHECK(n > 0 && !fseek(f, 0, SEEK_SET));
    char *text = calloc((size_t)n + 1, 1);
    CHECK(text && fread(text, 1, (size_t)n, f) == (size_t)n && fclose(f) == 0);
    char *parent = strstr(text, "\"parent\":\t\"0\"");
    if (!parent)
        parent = strstr(text, "\"parent\": \"0\"");
    CHECK(parent);
    char *zero = strchr(parent, ':');
    zero = strchr(zero, '0');
    CHECK(zero);
    *zero = '1';
    f = fopen("cycle.json", "wb");
    CHECK(f && fwrite(text, 1, (size_t)n, f) == (size_t)n && fclose(f) == 0);
    free(text);
    CHECK(!af_scene_load("cycle.json", NULL));
    ed_document d;
    CHECK(ed_document_init(&d));
    af_scene_destroy(d.scene);
    d.scene = af_scene_clone(s);
    d.selection = root;
    CHECK(ed_document_duplicate(&d) && af_scene_object_count(d.scene) == 6);
    af_object_id duplicated = d.selection;
    CHECK(af_scene_get_object(d.scene, duplicated)->parent == 0);
    CHECK(ed_document_delete(&d) && af_scene_object_count(d.scene) == 3);
    CHECK(ed_document_undo(&d) && af_scene_object_count(d.scene) == 6);
    CHECK(ed_document_undo(&d) && af_scene_object_count(d.scene) == 3);
    ed_document_destroy(&d);
    CHECK(af_scene_remove_object(s, root) && af_scene_object_count(s) == 0);
    CHECK(af_scene_add_object(s, "New", "cube") == 4);
    af_scene_destroy(s);
    puts("Hierarchy: local/world composition, cycles, inherited visibility, IO, subtree duplication/deletion and undo "
         "passed.");
    return 0;
}
