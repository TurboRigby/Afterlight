/* SPDX-License-Identifier: MPL-2.0 */

#include "joint_gizmo.h"
#include "document.h"
#include "gizmo.h"
#include <math.h>
#include <stdio.h>
#define CHECK(x)                                                                                        \
    do {                                                                                                \
        if (!(x)) {                                                                                     \
            fprintf(stderr, "Joint gizmo %d: %s\n", __LINE__, #x);                                      \
            return 1;                                                                                   \
        }                                                                                               \
    } while (0)
int main(void) {
    ed_document d;
    CHECK(ed_document_init(&d) && ed_document_add_cube(&d));
    af_object_id id = d.selection;
    af_collider c = af_collider_default();
    c.type = AF_COLLIDER_BOX;
    CHECK(ed_document_collider(&d, c));
    af_rigid_body b = af_rigid_body_default();
    b.enabled = true;
    b.type = AF_BODY_DYNAMIC;
    CHECK(ed_document_body(&d, b));
    af_transform body = af_transform_identity();
    body.position = (af_vec3){2, 3, 4};
    body.rotation.z = .4f;
    body.scale = (af_vec3){2, 1, 1};
    CHECK(ed_document_transform(&d, body));
    ed_document_end_edit(&d);
    af_joint j = af_joint_default();
    j.type = AF_JOINT_HINGE;
    j.a = id;
    j.anchor_a = (af_vec3){.3f, .4f, 0};
    CHECK(ed_document_joint(&d, j));
    ed_document_end_edit(&d);
    j = *af_scene_joint_at(d.scene, 0);
    af_transform pose;
    CHECK(ed_joint_edit_pose(d.scene, j, ED_JOINT_ANCHOR_A, &pose));
    af_vec3 original = pose.position;
    ed_gizmo g = {.mode = ED_GIZMO_MOVE,
                  .snap = true,
                  .axis = 0,
                  .initial = pose,
                  .direction = {100, 0},
                  .units = 2};
    pose = ed_gizmo_drag(&g, (af_vec2){33, 0});
    CHECK(fabsf(pose.position.x - original.x - .5f) < .001f);
    af_joint next;
    CHECK(ed_joint_edit_apply(d.scene, j, ED_JOINT_ANCHOR_A, pose, &next) &&
          ed_document_joint(&d, next));
    pose.position.x += .5f;
    CHECK(ed_joint_edit_apply(d.scene, j, ED_JOINT_ANCHOR_A, pose, &next) &&
          ed_document_joint(&d, next));
    ed_document_end_edit(&d);
    af_joint_frame f;
    CHECK(af_scene_joint_frame(d.scene, next, &f) &&
          af_vec3_length(af_vec3_sub(f.anchor_a, pose.position)) < .001f);
    CHECK(ed_document_undo(&d) &&
          af_vec3_length(af_vec3_sub(af_scene_joint_at(d.scene, 0)->anchor_a, j.anchor_a)) < .001f &&
          ed_document_redo(&d));
    CHECK(ed_joint_edit_pose(d.scene, next, ED_JOINT_AXIS_B, &pose));
    pose.rotation = (af_vec3){AF_PI / 2, 0, 0};
    CHECK(ed_joint_edit_apply(d.scene, next, ED_JOINT_AXIS_B, pose, &next) &&
          af_scene_joint_frame(d.scene, next, &f) && f.axis_b.z > .999f &&
          fabsf(af_vec3_dot(f.axis_b, f.reference_b)) < .001f);
    CHECK(ed_joint_edit_pose(d.scene, next, ED_JOINT_AXIS_A, &pose));
    pose.rotation = (af_vec3){AF_PI / 2, 0, 0};
    CHECK(ed_joint_edit_apply(d.scene, next, ED_JOINT_AXIS_A, pose, &next) &&
          af_scene_joint_frame(d.scene, next, &f) && f.axis_a.z > .999f);
    ed_document_destroy(&d);
    puts("Joint gizmos: rotated/scaled body anchors, world axes, snapped dragging and grouped undo/redo "
         "passed.");
    return 0;
}
