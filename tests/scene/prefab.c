/* SPDX-License-Identifier: MPL-2.0 */
#include <afterlight/prefab.h>
#include <stdio.h>
#include <string.h>
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            fprintf(stderr, "Prefab %d: %s\n", __LINE__, #x);                                                          \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
int main(void) {
    af_scene *s = af_scene_create();
    af_object_id root = af_scene_add_object(s, "Root", "__node"), child = af_scene_add_object(s, "Child", "cube");
    CHECK(af_scene_set_parent(s, child, root));
    af_gameplay_component g = af_gameplay_component_default();
    g.player.spawn = child;
    CHECK(af_scene_set_object_gameplay(s, root, g));
    af_scene_error e;
    CHECK(af_prefab_save(s, root, NULL, "test.afterlight.prefab", &e));
    af_transform t = af_transform_identity();
    t.position.x = 5;
    af_object_id first, second;
    CHECK(af_prefab_instantiate(s, "test.afterlight.prefab", 0, t, &first, &e) &&
          af_prefab_instantiate(s, "test.afterlight.prefab", 0, t, &second, &e));
    CHECK(first != second && af_scene_object_count(s) == 6);
    const af_scene_object *o = af_scene_get_object(s, first);
    CHECK(o->gameplay.player.spawn != child && af_scene_get_object(s, o->gameplay.player.spawn)->parent == first);
    CHECK(af_scene_set_object_visible(s, first, false) && af_scene_get_object(s, second)->visible);
    size_t count = af_scene_object_count(s);
    CHECK(!af_prefab_instantiate(s, "missing.prefab", 0, t, &first, &e) && af_scene_object_count(s) == count);
    remove("test.afterlight.prefab");
    af_scene_destroy(s);
    puts("Prefabs: hierarchy, ID/spawn remapping, independent overrides and failure atomicity passed.");
    return 0;
}
