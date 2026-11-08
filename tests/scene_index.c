/* SPDX-License-Identifier: MPL-2.0 */
#include "scene_internal.h"
#include <stdio.h>
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            fprintf(stderr, "Scene index %d: %s\n", __LINE__, #x);                                                     \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
int main(void) {
    af_scene *s = af_scene_create();
    CHECK(s);
    for (unsigned i = 0; i < 10000; ++i) {
        af_object_id id = af_scene_add_object(s, "Object", "cube");
        CHECK(id == (af_object_id)i + 1);
        af_transform t = af_transform_identity();
        t.position.x = (float)i;
        CHECK(af_scene_set_object_transform(s, id, t));
    }
    for (unsigned i = 0; i < 10000; ++i)
        CHECK(af_scene_get_object(s, (af_object_id)i + 1)->transform.position.x == (float)i);
    CHECK(!af_scene_get_object(s, UINT64_MAX));
    CHECK(af_scene_set_parent(s, 9999, 5000));
    CHECK(af_scene_remove_object(s, 5000) && !af_scene_get_object(s, 5000) && !af_scene_get_object(s, 9999) &&
          af_scene_get_object(s, 10000));
    af_scene *copy = af_scene_clone(s);
    CHECK(copy && af_scene_get_object(copy, 10000));
    af_scene_destroy(s);
    CHECK(af_scene_get_object(copy, 1)->transform.position.x == 0);
    af_object_id id = af_scene_add_object(copy, "New", "cube");
    CHECK(id == 10001 && af_scene_get_object(copy, id));
    /* Loader/prefab input may be out of ID order. Reindex preserves authored
     * order. */
    af_scene_object first = copy->objects[0];
    copy->objects[0] = copy->objects[copy->count - 1];
    copy->objects[copy->count - 1] = first;
    CHECK(af_scene_reindex(copy) && af_scene_object_at(copy, 0)->id == 10001 &&
          af_scene_get_object(copy, 1)->transform.position.x == 0);
    af_scene_destroy(copy);
    puts("Scene IDs: 10k inserts/lookups, reallocation, absent IDs, subtree "
         "removal, clone ownership and unordered input passed.");
    return 0;
}
