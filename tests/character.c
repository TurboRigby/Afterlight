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
            fprintf(stderr, "Character %d: %s\n", __LINE__, #c);                                                       \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
static af_object_id add(af_scene *s, af_vec3 position, af_vec3 half, float rotation) {
    af_object_id id = af_scene_add_object(s, "Surface", "cube");
    af_transform t = af_transform_identity();
    t.position = position;
    t.rotation.x = rotation;
    af_scene_set_object_transform(s, id, t);
    af_collider c = af_collider_default();
    c.type = AF_COLLIDER_BOX;
    c.half_extents = half;
    af_scene_set_object_collider(s, id, c);
    return id;
}
int main(void) {
    af_character_config config = af_character_config_default();
    af_query_filter filter = af_query_filter_default();
    af_scene *scene = af_scene_create();
    add(scene, (af_vec3){0, -0.5f, 0}, (af_vec3){20, 0.5f, 20}, 0);
    add(scene, (af_vec3){0, 0.8f, -3}, (af_vec3){2, 0.2f, 3}, 20 * AF_PI / 180);
    af_character c = {.position = {0, 0.902f, 1}};
    float high = 0;
    unsigned grounded = 0;
    for (unsigned i = 0; i < 100; ++i) {
        CHECK(af_character_step(scene, &c, config, (af_character_input){.move = {0, 0, -1}}, (float)AF_FIXED_DELTA,
                                filter));
        high = fmaxf(high, c.position.y);
        grounded += (unsigned)c.grounded;
    }
    printf("Walkable ramp: peak %.3f, grounded %u, z %.3f\n", high, grounded, c.position.z);
    CHECK(high > 2 && grounded > 80 && c.position.z < -5);
    af_scene_destroy(scene);
    scene = af_scene_create();
    add(scene, (af_vec3){0, -0.5f, 0}, (af_vec3){20, 0.5f, 20}, 0);
    add(scene, (af_vec3){0, 1.5f, -3}, (af_vec3){2, 0.2f, 2}, 65 * AF_PI / 180);
    c = (af_character){.position = {0, 0.902f, 0}};
    high = 0;
    for (unsigned i = 0; i < 120; ++i) {
        CHECK(af_character_step(scene, &c, config, (af_character_input){.move = {0, 0, -1}}, (float)AF_FIXED_DELTA,
                                filter));
        high = fmaxf(high, c.position.y);
    }
    printf("Steep ramp: peak %.3f, z %.3f\n", high, c.position.z);
    CHECK(high < 1.2f && c.position.z > -2);
    c = (af_character){.position = {0, 3.5f, -3}};
    float start = c.position.z;
    for (unsigned i = 0; i < 60; ++i)
        CHECK(af_character_step(scene, &c, config, (af_character_input){0}, (float)AF_FIXED_DELTA, filter));
    CHECK(c.position.z > start + 0.3f);
    af_scene_destroy(scene);
    scene = af_scene_create();
    af_object_id platform = add(scene, (af_vec3){0, 0, 0}, (af_vec3){3, 0.5f, 3}, 0);
    c = (af_character){.position = {0, 2, 0}};
    for (unsigned i = 0; i < 60; ++i)
        CHECK(af_character_step(scene, &c, config, (af_character_input){0}, (float)AF_FIXED_DELTA, filter));
    CHECK(c.grounded && c.ground == platform);
    af_transform t = af_scene_get_object(scene, platform)->transform;
    t.position = (af_vec3){1, 0.25f, 0};
    CHECK(af_scene_set_object_transform(scene, platform, t));
    CHECK(af_character_step(scene, &c, config, (af_character_input){0}, (float)AF_FIXED_DELTA, filter));
    CHECK(c.grounded && fabsf(c.position.x - 1) < 0.01f && c.position.y > 1.64f);
    CHECK(af_scene_remove_object(scene, platform));
    CHECK(af_character_step(scene, &c, config, (af_character_input){0}, (float)AF_FIXED_DELTA, filter) && !c.grounded);
    af_scene_destroy(scene);
    scene = af_scene_create();
    add(scene, (af_vec3){0, -0.5f, 0}, (af_vec3){100, 0.5f, 100}, 0);
    af_vec3 results[2];
    unsigned rates[] = {30, 144};
    for (unsigned r = 0; r < 2; ++r) {
        af_fixed_clock clock = {0};
        c = (af_character){.position = {0, 1, 0}};
        for (unsigned frame = 0; frame < rates[r] * 3; ++frame) {
            unsigned steps = af_fixed_clock_advance(&clock, 1.0 / rates[r], false, false);
            for (unsigned tick = 0; tick < steps; ++tick)
                CHECK(af_character_step(
                    scene, &c, config,
                    (af_character_input){.move = {0, 0, -1}, .jump = clock.ticks - steps + tick == 60},
                    (float)AF_FIXED_DELTA, filter));
        }
        results[r] = c.position;
    }
    CHECK(af_vec3_length(af_vec3_sub(results[0], results[1])) < 0.0001f);
    af_scene_destroy(scene);
    puts("Character: walkable/steep slopes, gravity sliding, moving/deleted platforms and render-rate independence "
         "passed.");
    return 0;
}
