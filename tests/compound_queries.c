/* SPDX-License-Identifier: MPL-2.0 */

#include <afterlight/character.h>
#include <afterlight/compound.h>
#include <math.h>
#include <stdio.h>
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            fprintf(stderr, "Compound query %d: %s\n", __LINE__, #x);                                                  \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
int main(void) {
    af_scene *s = af_scene_create();
    af_object_id id = af_scene_add_object(s, "L prop", "cube");
    af_collider_part p = af_collider_part_default();
    p.collider.half_extents = (af_vec3){.2f, 1, .2f};
    p.local.position.x = -1;
    af_collider_part_id a = af_scene_add_collider_part(s, id, p);
    p.local.position = (af_vec3){0, -.8f, 0};
    p.collider.half_extents = (af_vec3){1, .2f, .2f};
    af_collider_part_id b = af_scene_add_collider_part(s, id, p);
    af_collider c = af_collider_default();
    c.type = AF_COLLIDER_COMPOUND;
    CHECK(a && b && af_scene_set_object_collider(s, id, c));
    af_collision_hit hit;
    af_query_filter f = {.mask = UINT32_MAX};
    CHECK(af_scene_raycast(s, (af_vec3){-1, 0, 3}, (af_vec3){0, 0, -1}, 10, f, &hit) && hit.object == id &&
          hit.part == a);
    CHECK(!af_scene_raycast(s, (af_vec3){.5f, .5f, 3}, (af_vec3){0, 0, -1}, 10, f, &hit));
    CHECK(af_scene_sweep_capsule(s, (af_vec3){0, 2, 0}, .2f, .2f, (af_vec3){0, -4, 0}, .001f, f, &hit) &&
          hit.part == b);
    af_collision_hit hits[8];
    CHECK(af_scene_overlap_sphere(s, (af_vec3){0, -.7f, 0}, .2f, f, hits, 8) == 1 && hits[0].part == b);
    f.ignore = id;
    CHECK(!af_scene_raycast(s, (af_vec3){-1, 0, 3}, (af_vec3){0, 0, -1}, 10, f, &hit));
    af_transform t = af_transform_identity();
    t.rotation.y = .6f;
    t.position = (af_vec3){2, 1, 0};
    CHECK(af_scene_set_object_transform(s, id, t) && af_scene_collider_shape_count(s, id) == 2);
    af_world_collider w;
    CHECK(af_scene_collider_shape_at(s, id, 0, &w) && w.part == a && w.object == id);
    af_scene_destroy(s);
    puts("Compound queries: individual-part rays/overlaps/sweeps, empty-space rejection, owner "
         "filtering and transformed poses passed.");
    return 0;
}
