/* SPDX-License-Identifier: MPL-2.0 */

#include "tools/collider_view.h"
#include "document/document.h"
#include <stdio.h>
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            fprintf(stderr, "Joint editor %d: %s\n", __LINE__, #x);                                                    \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
int main(void) {
    ed_document d;
    CHECK(ed_document_init(&d) && ed_document_add_cube(&d));
    af_object_id id = d.selection;
    af_collider c = af_collider_default();
    c.type = AF_COLLIDER_BOX;
    CHECK(ed_document_collider(&d, c));
    ed_document_end_edit(&d);
    af_rigid_body b = af_rigid_body_default();
    b.enabled = true;
    b.type = AF_BODY_DYNAMIC;
    CHECK(ed_document_body(&d, b));
    ed_document_end_edit(&d);
    af_joint j = af_joint_default();
    j.type = AF_JOINT_HINGE;
    j.a = id;
    j.limits_enabled = true;
    j.lower_limit = -.5f;
    j.upper_limit = .5f;
    CHECK(ed_document_joint(&d, j));
    ed_document_end_edit(&d);
    CHECK(af_scene_joint_count(d.scene) == 1);
    j = *af_scene_joint_at(d.scene, 0);
    CHECK(ed_document_undo(&d) && af_scene_joint_count(d.scene) == 0 && ed_document_redo(&d));
    ed_line3 lines[64];
    CHECK(ed_joint_lines(d.scene, j, lines, 64) == 42);
    CHECK(ed_document_duplicate(&d));
    CHECK(af_scene_joint_count(d.scene) == 2 &&
          af_scene_get_object(d.scene, d.selection)->body.type == AF_BODY_DYNAMIC);
    CHECK(af_scene_joint_at(d.scene, 1)->a == d.selection && af_scene_joint_at(d.scene, 1)->anchor_b.x == 1.5f);
    ed_document_end_edit(&d);
    CHECK(ed_document_remove_joint(&d, j.id) && af_scene_joint_count(d.scene) == 1);
    CHECK(ed_document_undo(&d) && af_scene_joint_count(d.scene) == 2);
    ed_document_destroy(&d);
    puts("Joint editor: authoring, debug frame/arc, duplicate references and undo/redo passed.");
    return 0;
}
