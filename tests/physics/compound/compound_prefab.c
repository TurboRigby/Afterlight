/* SPDX-License-Identifier: MPL-2.0 */

#include "document/document.h"
#include <afterlight/physics.h>
#include <afterlight/prefab.h>
#include <stdio.h>
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            fprintf(stderr, "Compound prefab %d: %s\n", __LINE__, #x);                                                 \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
int main(void) {
    ed_document d;
    CHECK(ed_document_init(&d));
    af_object_id id = af_scene_add_object(d.scene, "Compound hierarchy", "__node"),
                 child = af_scene_add_object(d.scene, "Imported visual child", "cube");
    CHECK(af_scene_set_parent(d.scene, child, id));
    d.selection = id;
    af_collider_part part = af_collider_part_default();
    CHECK(ed_document_part(&d, part));
    ed_document_end_edit(&d);
    part.local.position.x = 1;
    CHECK(ed_document_part(&d, part));
    ed_document_end_edit(&d);
    af_rigid_body body = af_rigid_body_default();
    body.enabled = true;
    body.type = AF_BODY_DYNAMIC;
    body.mass = 5;
    CHECK(ed_document_body(&d, body));
    ed_document_end_edit(&d);
    CHECK(ed_document_duplicate(&d));
    af_object_id copy = d.selection;
    CHECK(copy != id && af_scene_collider_part_count(d.scene, copy) == 2 &&
          af_scene_collider_part_at(d.scene, copy, 0)->id != af_scene_collider_part_at(d.scene, id, 0)->id &&
          af_scene_get_object(d.scene, copy)->body.type == AF_BODY_DYNAMIC);
    af_scene_error e;
    CHECK(af_prefab_save(d.scene, copy, NULL, "compound.prefab", &e));
    af_object_id instance;
    af_transform pose = af_transform_identity();
    pose.position.x = 6;
    CHECK(af_prefab_instantiate(d.scene, "compound.prefab", 0, pose, &instance, &e));
    CHECK(af_scene_collider_part_count(d.scene, instance) == 2 &&
          af_scene_collider_part_at(d.scene, instance, 0)->owner == instance);
    part = *af_scene_collider_part_at(d.scene, instance, 0);
    part.collider.enabled = false;
    CHECK(af_scene_set_collider_part(d.scene, part.id, part));
    af_collider_part_id active = af_scene_collider_part_at(d.scene, instance, 1)->id;
    CHECK(!af_scene_remove_collider_part(d.scene, active) && af_scene_collider_part_count(d.scene, instance) == 2);
    af_physics *p = af_physics_create();
    CHECK(p && af_physics_step(p, d.scene, 1.f / 60));
    af_physics_destroy(p);
    CHECK(af_scene_remove_object(d.scene, instance) && af_scene_collider_part_count(d.scene, instance) == 0 &&
          af_scene_collider_part_count(d.scene, id) == 2);
    ed_document_destroy(&d);
    remove("compound.prefab");
    puts("Compound prefabs: hierarchy ownership, duplicate/instance ID remapping, dynamic validation, "
         "atomic removal and cleanup passed.");
    return 0;
}
