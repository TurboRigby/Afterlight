/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <afterlight/landscape.h>
#include <afterlight/physics.h>
#include <afterlight/scene.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            fprintf(stderr, "CHECK failed %s:%d: %s\n", __FILE__, __LINE__, #c);                                       \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)

static int test_dynamic_physics_pools(void) {
    af_scene *scene = af_scene_create();
    af_physics *physics = af_physics_create();
    CHECK(scene && physics);

    /* Large ground floor */
    af_object_id floor = af_scene_add_object(scene, "Floor", "cube");
    CHECK(floor != AF_OBJECT_NONE);
    af_transform ft = af_transform_identity();
    ft.position = (af_vec3){0, -0.5f, 0};
    af_scene_set_object_transform(scene, floor, ft);
    af_collider fc = af_collider_default();
    fc.enabled = true;
    fc.type = AF_COLLIDER_BOX;
    fc.half_extents = (af_vec3){200.0f, 0.5f, 200.0f};
    CHECK(af_scene_set_object_collider(scene, floor, fc));

    /* Spawn 1,200 dynamic bodies (> legacy 1,024 inline limit) on a 40x30 grid. */
    const unsigned cols = 40;
    const unsigned rows = 30;
    const unsigned total = cols * rows;
    af_object_id sample_id = AF_OBJECT_NONE;
    for (unsigned r = 0; r < rows; ++r) {
        for (unsigned c = 0; c < cols; ++c) {
            char name[32];
            snprintf(name, sizeof(name), "Body_%u_%u", r, c);
            af_object_id id = af_scene_add_object(scene, name, "cube");
            CHECK(id != AF_OBJECT_NONE);
            af_transform t = af_transform_identity();
            t.position = (af_vec3){((float)c - 20.0f) * 2.5f, 0.6f, ((float)r - 15.0f) * 2.5f};
            CHECK(af_scene_set_object_transform(scene, id, t));
            af_collider col = af_collider_default();
            col.enabled = true;
            col.type = AF_COLLIDER_BOX;
            col.half_extents = (af_vec3){0.45f, 0.45f, 0.45f};
            CHECK(af_scene_set_object_collider(scene, id, col));
            af_rigid_body rb = af_rigid_body_default();
            rb.enabled = true;
            rb.type = AF_BODY_DYNAMIC;
            rb.mass = 1.0f;
            CHECK(af_scene_set_body(scene, id, rb));
            if (r == rows - 1 && c == cols - 1)
                sample_id = id;
        }
    }

    for (unsigned step = 0; step < 12; ++step)
        CHECK(af_physics_step(physics, scene, 1.0f / 60.0f));

    af_physics_stats stats = af_physics_get_stats(physics);
    CHECK(stats.bodies == total + 1);
    CHECK(stats.dynamic_bodies == total);
    CHECK(stats.contacts >= total);

    af_physics_state st = af_physics_get_state(physics, sample_id);
    CHECK(st.available && st.position.y > 0.35f && st.position.y < 0.65f);

    af_physics_reset(physics);
    CHECK(af_physics_step(physics, scene, 1.0f / 60.0f));
    CHECK(af_physics_get_stats(physics).bodies == total + 1);

    af_physics_destroy(physics);
    af_scene_destroy(scene);
    return 0;
}

static int test_landscape_cow_cloning(void) {
    af_scene *scene = af_scene_create();
    CHECK(scene != NULL);
    af_landscape *ls = af_landscape_create(4, 4, 2.0f);
    CHECK(ls != NULL);
    CHECK(af_landscape_generate_procedural_hills(ls, 0.05f, 4.0f, 12345u));
    int fl = af_landscape_add_foliage_layer(ls, "Grass", "grass_blade");
    CHECK(fl >= 0);
    CHECK(af_landscape_paint_foliage(ls, (size_t)fl, (af_vec3){0, 0, 0}, 8.0f, 1.0f, 77u));
    CHECK(af_scene_set_landscape(scene, ls));

    float orig_h = af_landscape_get_height(af_scene_get_landscape(scene), 0.0f, 0.0f);
    size_t orig_foliage = af_scene_get_landscape(scene)->foliage_layers[0].total_instances;
    const af_terrain_chunk *orig_chunks = af_scene_get_landscape(scene)->chunks;
    CHECK(orig_foliage > 0);

    af_scene *clone1 = af_scene_clone(scene);
    af_scene *clone2 = af_scene_clone(scene);
    CHECK(clone1 && clone2);

    /* COW shares heavy chunk storage while keeping distinct af_landscape wrappers. */
    CHECK(af_scene_get_landscape(clone1) != af_scene_get_landscape(scene));
    CHECK(af_scene_get_landscape(clone1)->chunks == orig_chunks);
    CHECK(af_scene_get_landscape(clone2)->chunks == orig_chunks);

    /* Mutable access detaches clone1 without mutating scene or clone2. */
    af_landscape *mut1 = af_scene_get_landscape_mut(clone1);
    CHECK(mut1 != NULL);
    CHECK(mut1->chunks != orig_chunks);
    CHECK(af_scene_get_landscape(scene)->chunks == orig_chunks);
    CHECK(af_scene_get_landscape(clone2)->chunks == orig_chunks);

    CHECK(af_landscape_sculpt_raise_lower(mut1, (af_vec3){0, 0, 0}, 6.0f, 5.0f, AF_FALLOFF_SMOOTH));
    float mod_h = af_landscape_get_height(af_scene_get_landscape(clone1), 0.0f, 0.0f);
    CHECK(mod_h > orig_h + 2.0f);
    CHECK(fabsf(af_landscape_get_height(af_scene_get_landscape(scene), 0.0f, 0.0f) - orig_h) < 1e-5f);
    CHECK(fabsf(af_landscape_get_height(af_scene_get_landscape(clone2), 0.0f, 0.0f) - orig_h) < 1e-5f);

    /* Destroy original scene while clone2 still holds a COW reference to orig_chunks. */
    af_scene_destroy(scene);
    CHECK(af_scene_get_landscape(clone2)->chunks == orig_chunks);
    CHECK(fabsf(af_landscape_get_height(af_scene_get_landscape(clone2), 0.0f, 0.0f) - orig_h) < 1e-5f);
    CHECK(af_scene_get_landscape(clone2)->foliage_layers[0].total_instances == orig_foliage);

    af_scene_destroy(clone1);
    af_scene_destroy(clone2);
    return 0;
}

int main(void) {
    if (test_dynamic_physics_pools() != 0)
        return 1;
    if (test_landscape_cow_cloning() != 0)
        return 1;
    puts("Dynamic physics pools (1200+ bodies) and COW landscape cloning passed.");
    return 0;
}
