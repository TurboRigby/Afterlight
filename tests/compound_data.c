/* SPDX-License-Identifier: MPL-2.0 */

#include <afterlight/compound.h>
#include <stdio.h>
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            fprintf(stderr, "Compound data %d: %s\n", __LINE__, #x);                                                   \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
int main(void) {
    af_scene *s = af_scene_create();
    af_object_id id = af_scene_add_object(s, "Compound owner", "cube");
    af_collider_part p = af_collider_part_default();
    af_collider_part_id first = af_scene_add_collider_part(s, id, p);
    CHECK(first);
    p.local.position.x = 1;
    p.local.rotation.z = .5f;
    CHECK(af_scene_add_collider_part(s, id, p));
    p.local.scale.x = 0;
    CHECK(!af_scene_set_collider_part(s, first, p));
    af_scene_error e;
    CHECK(af_scene_save(s, "compound-data.scene", &e));
    af_scene *copy = af_scene_load("compound-data.scene", &e);
    CHECK(copy && af_scene_collider_part_count(copy, id) == 2 && af_scene_get_collider_part(copy, first));
    af_scene *clone = af_scene_clone(s);
    CHECK(clone && af_scene_remove_object(clone, id) && af_scene_collider_part_count(clone, id) == 0 &&
          af_scene_collider_part_count(s, id) == 2);
    for (unsigned i = 2; i < AF_COMPOUND_MAX_PARTS; ++i) {
        p = af_collider_part_default();
        CHECK(af_scene_add_collider_part(s, id, p));
    }
    CHECK(!af_scene_add_collider_part(s, id, p));
    af_scene_destroy(clone);
    af_scene_destroy(copy);
    af_scene_destroy(s);
    remove("compound-data.scene");
    puts("Compound parts: bounded ownership, poses, clone/removal, stable IDs, validation and "
         "persistence passed.");
    return 0;
}
