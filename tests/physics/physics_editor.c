/* SPDX-License-Identifier: MPL-2.0 */
#include "document/document.h"
#include <math.h>
#include <stdio.h>
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            fprintf(stderr, "Physics editor %d: %s\n", __LINE__, #x);                                                  \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
int main(void) {
    ed_document d;
    CHECK(ed_document_init(&d) && ed_document_create_mesh_object(&d, "cube"));
    af_collider c = af_collider_default();
    c.type = AF_COLLIDER_BOX;
    CHECK(ed_document_collider(&d, c));
    ed_document_end_edit(&d);
    af_rigid_body body = af_rigid_body_default();
    body.enabled = true;
    body.type = AF_BODY_DYNAMIC;
    CHECK(ed_document_body(&d, body));
    ed_document_end_edit(&d);
    CHECK(af_scene_get_object(d.scene, d.selection)->body.enabled);
    CHECK(ed_document_undo(&d) && !af_scene_get_object(d.scene, d.selection)->body.enabled);
    CHECK(ed_document_redo(&d) && af_scene_get_object(d.scene, d.selection)->body.enabled);
    uint64_t revision = d.revision;
    body.mass = -1;
    CHECK(!ed_document_body(&d, body) && d.revision == revision);
    af_transform t = af_scene_get_object(d.scene, d.selection)->transform;
    t.scale.x = -1;
    CHECK(!af_scene_set_object_transform(d.scene, d.selection, t));
    c.type = AF_COLLIDER_CAPSULE;
    CHECK(!af_scene_set_object_collider(d.scene, d.selection, c));
    t = af_transform_identity();
    t.rotation.y = 89 * AF_PI / 180;
    CHECK(af_scene_set_object_transform(d.scene, d.selection, t));
    af_scene *previous = af_scene_clone(d.scene);
    t.rotation = (af_vec3){AF_PI, 89 * AF_PI / 180, AF_PI};
    CHECK(af_scene_set_object_transform(d.scene, d.selection, t));
    af_mat4 middle;
    CHECK(af_scene_interpolated_world_matrix(d.scene, previous, d.selection, .5f, &middle) &&
          fabsf(middle.elements[0]) < .001f && middle.elements[2] < -.999f && middle.elements[5] > .999f);
    af_scene_destroy(previous);
    ed_document_destroy(&d);
    puts("Physics editor: body history, undo/redo and invalid component/transform rejection passed.");
    return 0;
}
