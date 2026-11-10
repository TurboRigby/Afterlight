/* SPDX-License-Identifier: MPL-2.0 */

#include "document/document.h"
#include <math.h>
#include <stdio.h>
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            fprintf(stderr, "Compound editor %d: %s\n", __LINE__, #x);                                                 \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
int main(void) {
    ed_document d;
    CHECK(ed_document_init(&d));
    af_object_id root = af_scene_add_object(d.scene, "Imported root", "__node"),
                 child = af_scene_add_object(d.scene, "Rigid mesh child", "cube");
    af_transform t = af_transform_identity();
    t.position.x = 2;
    t.scale.x = 2;
    CHECK(af_scene_set_parent(d.scene, child, root) && af_scene_set_object_transform(d.scene, child, t));
    d.selection = root;
    af_collider_part part = af_collider_part_default();
    CHECK(ed_document_part(&d, part));
    ed_document_end_edit(&d);
    part = *af_scene_collider_part_at(d.scene, root, 0);
    CHECK(ed_document_fit_part(&d, part.id));
    part = *af_scene_get_collider_part(d.scene, part.id);
    CHECK(fabsf(part.local.position.x - 2) < .001f && fabsf(part.collider.half_extents.x - 1.3f) < .001f);
    part.local.position.x = 3;
    CHECK(ed_document_part(&d, part));
    part.local.position.x = 4;
    CHECK(ed_document_part(&d, part));
    ed_document_end_edit(&d);
    CHECK(ed_document_undo(&d) && fabsf(af_scene_get_collider_part(d.scene, part.id)->local.position.x - 2) < .001f &&
          ed_document_redo(&d));
    CHECK(!ed_document_remove_part(&d, part.id));
    af_collider_part sphere = af_collider_part_default();
    sphere.collider.type = AF_COLLIDER_SPHERE;
    CHECK(ed_document_part(&d, sphere));
    ed_document_end_edit(&d);
    CHECK(ed_document_remove_part(&d, part.id) && ed_document_undo(&d) &&
          af_scene_collider_part_count(d.scene, root) == 2);
    ed_document_destroy(&d);
    puts("Compound editor: imported-child bounds fitting, part transforms and grouped undo/removal "
         "passed.");
    return 0;
}
