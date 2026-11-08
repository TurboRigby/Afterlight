/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <afterlight/scene.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                                                               \
    do {                                                                                                               \
        if (!(condition)) {                                                                                            \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #condition);                            \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)

int main(void) {
    af_scene *scene = af_scene_create();
    CHECK(scene && af_scene_object_count(scene) == 0);
    char name[] = "First cube";
    char mesh[] = "cube";
    af_object_id first = af_scene_add_object(scene, name, mesh);
    CHECK(first != AF_OBJECT_NONE);
    name[0] = 'X';
    mesh[0] = 'X';
    const af_scene_object *object = af_scene_get_object(scene, first);
    CHECK(object && strcmp(object->name, "First cube") == 0 && strcmp(object->mesh, "cube") == 0);
    CHECK(object->visible && object->transform.scale.x == 1 && object->transform.position.x == 0);
    af_object_id second = af_scene_add_object(scene, object->name, object->mesh);
    CHECK(second != AF_OBJECT_NONE && second != first);
    for (unsigned i = 0; i < 14; ++i)
        CHECK(af_scene_add_object(scene, "Fill", "cube"));
    object = af_scene_get_object(scene, first);
    af_object_id alias = af_scene_add_object(scene, object->name, object->mesh);
    CHECK(alias && strcmp(af_scene_get_object(scene, alias)->name, "First cube") == 0);
    af_object_id last = 0;
    for (unsigned i = 0; i < 1024; ++i) {
        last = af_scene_add_object(scene, "Instance", "cube");
        CHECK(last != AF_OBJECT_NONE);
    }
    CHECK(af_scene_object_count(scene) == 1041);
    CHECK(af_scene_get_object(scene, first) && af_scene_get_object(scene, last));
    CHECK(af_scene_remove_object(scene, first));
    CHECK(!af_scene_get_object(scene, first) && !af_scene_remove_object(scene, first));
    CHECK(af_scene_object_at(scene, 0)->id == second);
    af_object_id newer = af_scene_add_object(scene, "Instance", "cube");
    CHECK(newer > last && newer != first);
    CHECK(af_scene_set_object_name(scene, second, "Renamed"));
    CHECK(af_scene_set_object_mesh(scene, second, "unloaded/asset"));
    CHECK(af_scene_set_object_visible(scene, second, false));
    af_transform transform = af_transform_identity();
    transform.position = (af_vec3){2, 3, 4};
    transform.rotation.z = AF_PI / 2;
    transform.scale = (af_vec3){2, 3, 4};
    CHECK(af_scene_set_object_transform(scene, second, transform));
    af_vec4 point =
        af_mat4_transform(af_transform_matrix(af_scene_get_object(scene, second)->transform), (af_vec4){1, 0, 0, 1});
    CHECK(fabsf(point.x - 2) < 0.00001f && fabsf(point.y - 5) < 0.00001f && fabsf(point.z - 4) < 0.00001f);
    transform.rotation = (af_vec3){AF_PI / 2, AF_PI / 2, AF_PI / 2};
    point = af_mat4_transform(af_transform_matrix(transform), (af_vec4){1, 2, 3, 1});
    CHECK(fabsf(point.x - 14) < 0.00001f && fabsf(point.y - 9) < 0.00001f && fabsf(point.z - 2) < 0.00001f);
    transform.position.x = NAN;
    CHECK(!af_scene_set_object_transform(scene, second, transform));
    CHECK(af_scene_get_object(scene, second)->transform.position.x == 2);
    CHECK(!af_scene_get_object(scene, second)->visible);
    CHECK(strcmp(af_scene_get_object(scene, second)->mesh, "unloaded/asset") == 0);
    char too_long[AF_OBJECT_NAME_CAPACITY + 1];
    memset(too_long, 'x', sizeof(too_long) - 1);
    too_long[sizeof(too_long) - 1] = 0;
    size_t count = af_scene_object_count(scene);
    CHECK(af_scene_add_object(scene, too_long, "cube") == AF_OBJECT_NONE);
    CHECK(af_scene_add_object(scene, "Object", "") == AF_OBJECT_NONE);
    CHECK(af_scene_add_object(scene, NULL, "cube") == AF_OBJECT_NONE);
    CHECK(!af_scene_set_object_name(scene, second, too_long));
    CHECK(strcmp(af_scene_get_object(scene, second)->name, "Renamed") == 0);
    CHECK(af_scene_object_count(scene) == count);
    CHECK(!af_scene_object_at(scene, count) && !af_scene_get_object(scene, AF_OBJECT_NONE));
    CHECK(!af_scene_set_object_visible(scene, first, true));
    af_scene_destroy(scene);
    af_scene_destroy(NULL);
    CHECK(!af_scene_object_count(NULL) && !af_scene_get_object(NULL, first));
    puts("Scenes: CPU-only objects, copied names, growth, stable IDs, transforms, visibility, validation passed.");
    return 0;
}
