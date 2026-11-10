/* SPDX-License-Identifier: MPL-2.0 */
#include <afterlight/character.h>
#include <afterlight/scene.h>
#include <math.h>
#include <stdio.h>
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            fprintf(stderr, "Mesh queries %d: %s\n", __LINE__, #x);                                                    \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
int main(void) {
    af_scene *s = af_scene_create();
    af_vertex v[4] = {0};
    v[0].position = (af_vec3){-10, 0, -10};
    v[1].position = (af_vec3){10, 0, -10};
    v[2].position = (af_vec3){10, 0, 10};
    v[3].position = (af_vec3){-10, 0, 10};
    uint32_t indices[] = {0, 2, 1, 0, 3, 2};
    af_mesh_data mesh = {v, 4, indices, 6};
    af_asset_error e;
    CHECK(af_scene_set_collision_mesh(s, "floor", &mesh, &e));
    af_object_id id = af_scene_add_object(s, "Imported floor", "floor");
    af_collider c = af_collider_default();
    c.type = AF_COLLIDER_MESH;
    CHECK(af_scene_set_object_collider(s, id, c));
    af_query_filter filter = af_query_filter_default();
    af_collision_hit hit;
    CHECK(af_scene_raycast(s, (af_vec3){0, 3, 0}, (af_vec3){0, -1, 0}, 5, filter, &hit) &&
          fabsf(hit.distance - 3) < .0001f && hit.normal.y > .99f);
    CHECK(af_scene_raycast(s, (af_vec3){0, -3, 0}, (af_vec3){0, 1, 0}, 5, filter, &hit) && hit.normal.y < -.99f);
    CHECK(af_scene_overlap_sphere(s, (af_vec3){0, .2f, 0}, .3f, filter, &hit, 1) == 1 && hit.penetration > .09f);
    CHECK(af_scene_sweep_capsule(s, (af_vec3){0, 4, 0}, .35f, .55f, (af_vec3){0, -8, 0}, .002f, filter, &hit) &&
          fabsf(hit.fraction - .38725f) < .001f);
    af_character character = {.position = {0, 2, 0}};
    for (unsigned i = 0; i < 120; ++i)
        CHECK(af_character_step(s, &character, af_character_config_default(),
                                (af_character_input){.move = {0, 0, -.6f}}, 1.f / 60, filter));
    CHECK(character.grounded && fabsf(character.position.y - .902f) < .01f && character.position.z < -4);
    filter.ignore = id;
    CHECK(!af_scene_raycast(s, (af_vec3){0, 1, 0}, (af_vec3){0, -1, 0}, 5, filter, &hit));
    filter.ignore = 0;
    c.trigger = true;
    CHECK(af_scene_set_object_collider(s, id, c) &&
          !af_scene_raycast(s, (af_vec3){0, 1, 0}, (af_vec3){0, -1, 0}, 5, filter, &hit));
    af_transform t = af_transform_identity();
    t.scale = (af_vec3){-2, 1, .5f};
    t.position.y = 2;
    CHECK(af_scene_set_object_transform(s, id, t));
    filter.include_triggers = true;
    CHECK(af_scene_raycast(s, (af_vec3){0, 4, 0}, (af_vec3){0, -1, 0}, 5, filter, &hit) &&
          fabsf(hit.distance - 2) < .0001f);
    af_scene_destroy(s);
    puts("Mesh queries: two-sided ray/overlap/sweep, grounded walking, filters and mirrored transforms "
         "passed.");
    return 0;
}
