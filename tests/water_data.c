/* SPDX-License-Identifier: MPL-2.0 */

#include "document.h"
#include <afterlight/physics.h>
#include <afterlight/prefab.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#define CHECK(x)                                                                                        \
    do {                                                                                                \
        if (!(x)) {                                                                                     \
            fprintf(stderr, "Water data %d: %s\n", __LINE__, #x);                                       \
            return 1;                                                                                   \
        }                                                                                               \
    } while (0)
int main(void) {
    ed_document d;
    CHECK(ed_document_init(&d) && ed_document_new(&d));
    CHECK(ed_document_add_water(&d));
    af_object_id id = d.selection;
    af_water w = *af_scene_get_water(d.scene, id);
    CHECK(w.density == 1000);
    w.quality = AF_WATER_HIGH;
    w.absorption.x = 2;
    CHECK(ed_document_water(&d, w));
    ed_document_end_edit(&d);
    CHECK(ed_document_undo(&d) && af_scene_get_water(d.scene, id)->quality == AF_WATER_MEDIUM);
    CHECK(ed_document_redo(&d));
    af_transform t = af_transform_identity();
    t.rotation.x = .1f;
    CHECK(!af_scene_set_object_transform(d.scene, id, t));
    t.rotation.x = 0;
    t.scale.x = 2;
    CHECK(!af_scene_set_object_transform(d.scene, id, t));
    af_object_id parent = af_scene_add_object(d.scene, "Other", "__node");
    CHECK(!af_scene_set_parent(d.scene, id, parent));
    CHECK(!af_scene_set_object_mesh(d.scene, id, "cube"));
    CHECK(af_scene_save(d.scene, "water.json", NULL));
    af_scene_error e;
    af_scene *copy = af_scene_load("water.json", &e);
    if (!copy)
        fprintf(stderr, "%s\n", e.message);
    CHECK(copy && af_scene_water_count(copy) == 1 && af_scene_get_water(copy, id)->absorption.x == 2);
    af_scene_destroy(copy);
    CHECK(af_prefab_save(d.scene, id, "water.json", "water.afterlight.prefab", &e));
    af_object_id instance;
    CHECK(af_prefab_instantiate(d.scene, "water.afterlight.prefab", 0, af_transform_identity(),
                                &instance, &e));
    CHECK(instance != id && af_scene_get_water(d.scene, instance));
    CHECK(ed_document_duplicate(&d) && af_scene_get_water(d.scene, d.selection));
    CHECK(af_scene_water_count(d.scene) == 3);
    CHECK(ed_document_delete(&d) && af_scene_water_count(d.scene) == 2);
    w.depth = NAN;
    CHECK(!af_scene_set_water(d.scene, id, w));
    w = af_water_default();
    w.quality = (af_water_quality)9;
    CHECK(!af_water_valid(w));
    d.selection = parent;
    CHECK(ed_document_remove_water(&d) == false);
    d.selection = id;
    CHECK(ed_document_remove_water(&d));
    CHECK(ed_document_undo(&d) && af_scene_get_water(d.scene, id));
    CHECK(af_scene_remove_object(d.scene, id) && !af_scene_get_water(d.scene, id));
    ed_document_destroy(&d);
    remove("water.json");
    remove("water.afterlight.prefab");
    puts("Water data: persistence, constraints, clone/history, duplication, prefab owner remapping and "
         "deletion passed.");
    return 0;
}
