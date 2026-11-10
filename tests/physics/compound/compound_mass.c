/* SPDX-License-Identifier: MPL-2.0 */

#include <afterlight/compound.h>
#include <math.h>
#include <stdio.h>
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            fprintf(stderr, "Compound mass %d: %s\n", __LINE__, #x);                                                   \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
int main(void) {
    af_scene *s = af_scene_create();
    af_object_id id = af_scene_add_object(s, "Weighted body", "__node");
    af_collider_part p = af_collider_part_default();
    p.local.position.x = -1;
    CHECK(af_scene_add_collider_part(s, id, p));
    p.local.position.x = 1;
    p.density = 3000;
    af_collider_part_id right = af_scene_add_collider_part(s, id, p);
    CHECK(right);
    af_mass_properties m;
    CHECK(af_scene_compound_mass(s, id, &m) && fabsf(m.mass - 4000) < .1f && fabsf(m.volume - 2) < .0001f &&
          fabsf(m.center.x - .5f) < .0001f && fabsf(m.inertia.elements[0] - 666.6667f) < .2f &&
          fabsf(m.inertia.elements[5] - 3666.6667f) < .3f);
    af_transform t = af_transform_identity();
    t.scale = (af_vec3){2, 2, 2};
    CHECK(af_scene_set_object_transform(s, id, t) && af_scene_compound_mass(s, id, &m));
    CHECK(fabsf(m.mass - 32000) < 1 && fabsf(m.center.x - 1) < .0001f && fabsf(m.inertia.elements[5] - 117333.33f) < 8);
    af_scene_destroy(s);
    s = af_scene_create();
    id = af_scene_add_object(s, "Rotated plank", "cube");
    p = af_collider_part_default();
    p.density = 1;
    p.collider.half_extents = (af_vec3){1, .1f, .1f};
    p.local.rotation.z = AF_PI / 4;
    CHECK(af_scene_add_collider_part(s, id, p) && af_scene_compound_mass(s, id, &m));
    CHECK(fabsf(m.inertia.elements[1]) > .01f && fabsf(m.inertia.elements[1] - m.inertia.elements[4]) < 1e-6f);
    af_scene_destroy(s);
    puts("Compound mass: volume/density weights, shifted COM, parallel-axis theorem, world scaling and "
         "off-diagonal inertia passed.");
    return 0;
}
