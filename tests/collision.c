/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <afterlight/afterlight.h>
#include <math.h>
#include <stdio.h>
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            fprintf(stderr, "Collision %d: %s\n", __LINE__, #c);                                                       \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
static af_object_id box(af_scene *s, af_vec3 p, af_vec3 half) {
    af_object_id id = af_scene_add_object(s, "Box", "cube");
    af_transform t = af_transform_identity();
    t.position = p;
    af_scene_set_object_transform(s, id, t);
    af_collider c = af_collider_default();
    c.type = AF_COLLIDER_BOX;
    c.half_extents = half;
    af_scene_set_object_collider(s, id, c);
    return id;
}
int main(void) {
    af_scene *scene = af_scene_create();
    af_query_filter filter = af_query_filter_default();
    af_collision_hit hit, hits[8];
    af_object_id floor = box(scene, (af_vec3){0, -0.5f, 0}, (af_vec3){20, 0.5f, 20});
    CHECK(af_scene_raycast(scene, (af_vec3){0, 5, 0}, (af_vec3){0, -2, 0}, 10, filter, &hit) && hit.object == floor &&
          fabsf(hit.distance - 5) < 1e-5f && hit.normal.y > 0.99f);
    CHECK(af_scene_overlap_capsule(scene, (af_vec3){0, 0.8f, 0}, 0.35f, 0.55f, filter, hits, 8) == 1 &&
          hits[0].penetration > 0.09f);
    CHECK(
        af_scene_sweep_capsule(scene, (af_vec3){0, 10, 0}, 0.35f, 0.55f, (af_vec3){0, -20, 0}, 0.002f, filter, &hit) &&
        hit.object == floor && hit.fraction > 0.45f && hit.fraction < 0.46f);
    af_object_id wall = box(scene, (af_vec3){2, 2, 0}, (af_vec3){0.05f, 2, 10});
    CHECK(af_scene_sweep_capsule(scene, (af_vec3){0, 2, 0}, 0.35f, 0.55f, (af_vec3){100, 0, 0}, 0, filter, &hit) &&
          hit.object == wall && hit.fraction < 0.02f);
    af_collider c = af_collider_default();
    c.type = AF_COLLIDER_SPHERE;
    c.radius = 1;
    c.trigger = true;
    c.layer = 2;
    af_object_id sphere = af_scene_add_object(scene, "Sphere", "cube");
    CHECK(af_scene_set_object_collider(scene, sphere, c));
    filter.mask = 2;
    CHECK(!af_scene_raycast(scene, (af_vec3){0, 0, 5}, (af_vec3){0, 0, -1}, 10, filter, &hit));
    filter.include_triggers = true;
    CHECK(af_scene_raycast(scene, (af_vec3){0, 0, 5}, (af_vec3){0, 0, -1}, 10, filter, &hit) && hit.object == sphere &&
          hit.trigger && fabsf(hit.distance - 4) < 1e-5f);
    c.type = AF_COLLIDER_CAPSULE;
    c.half_height = 2;
    CHECK(af_scene_set_object_collider(scene, sphere, c));
    CHECK(af_scene_raycast(scene, (af_vec3){0, 5, 0}, (af_vec3){0, -1, 0}, 10, filter, &hit) &&
          fabsf(hit.distance - 2) < 1e-5f);
    CHECK(af_scene_overlap_sphere(scene, (af_vec3){0, 3.5f, 0}, 0.6f, filter, hits, 8) == 1);
    filter = af_query_filter_default();
    af_character_config config = af_character_config_default();
    af_character character = {.position = {0, 4, 0}};
    for (unsigned i = 0; i < 120; ++i)
        CHECK(af_character_step(scene, &character, config, (af_character_input){0}, (float)AF_FIXED_DELTA, filter));
    CHECK(character.grounded && character.ground == floor && fabsf(character.position.y - 0.902f) < 0.01f);
    float initial = character.position.y;
    CHECK(af_character_step(scene, &character, config, (af_character_input){.jump = true}, (float)AF_FIXED_DELTA,
                            filter));
    CHECK(!character.grounded && character.position.y > initial);
    float peak = character.position.y;
    for (unsigned i = 0; i < 120; ++i) {
        CHECK(af_character_step(scene, &character, config, (af_character_input){.move = {1, 0, 0.5f}},
                                (float)AF_FIXED_DELTA, filter));
        peak = fmaxf(peak, character.position.y);
    }
    CHECK(peak > 1.7f && character.grounded && character.position.x < 1.61f && character.position.z > 1);
    CHECK(af_scene_remove_object(scene, sphere));
    af_transform platform = af_scene_get_object(scene, floor)->transform;
    platform.position.x = 1;
    CHECK(af_scene_set_object_transform(scene, floor, platform));
    float previous = character.position.x;
    CHECK(af_character_step(scene, &character, config, (af_character_input){0}, (float)AF_FIXED_DELTA, filter));
    CHECK(character.position.x < 1.61f && character.position.x >= previous - 0.01f); /* Wall can constrain carry. */
    c = af_collider_default();
    c.type = AF_COLLIDER_SPHERE;
    c.radius = -1;
    CHECK(!af_scene_set_object_collider(scene, wall, c));
    CHECK(!af_scene_raycast(scene, (af_vec3){0}, (af_vec3){0}, 10, filter, &hit));
    af_scene_destroy(scene);
    puts("Collision: OBB rays, sphere/capsule caps, triggers/layers, overlap, continuous sweep, grounded jump, wall "
         "slide and carry passed.");
    return 0;
}
