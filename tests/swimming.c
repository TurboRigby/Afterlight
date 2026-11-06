/* SPDX-License-Identifier: MPL-2.0 */
#include <afterlight/swim.h>
#include <math.h>
#include <stdio.h>
#define CHECK(x)                                                                                        \
    do {                                                                                                \
        if (!(x)) {                                                                                     \
            fprintf(stderr, "Swimming %d: %s\n", __LINE__, #x);                                         \
            return 1;                                                                                   \
        }                                                                                               \
    } while (0)
int main(void) {
    float heights[3];
    const unsigned rates[] = {30, 60, 144};
    for (unsigned rate = 0; rate < 3; ++rate) {
        af_scene *s = af_scene_create();
        af_object_id id = af_scene_add_object(s, "Pool", "__node");
        af_water w = af_water_default();
        w.wind = 0;
        w.depth = 4;
        CHECK(af_scene_set_water(s, id, w));
        af_character c = {.position = {0, -1, 0}};
        af_character_config cc = af_character_config_default();
        af_swim_config sc = af_swim_config_default();
        af_swim_state state = {0};
        af_query_filter filter = af_query_filter_default();
        float dt = 1.f / (float)rates[rate];
        for (unsigned i = 0; i < rates[rate] * 6; ++i)
            CHECK(af_character_swim_step(NULL, s, &c, cc, &state, sc, (af_swim_input){0}, dt, filter));
        heights[rate] = c.position.y;
        printf("Rate %u surface %.5f immersion %.5f\n", rates[rate], c.position.y, state.immersion);
        CHECK(state.swimming && !state.head_underwater &&
              fabsf(state.immersion - sc.neutral_fraction) < .025f);
        for (unsigned i = 0; i < rates[rate]; ++i)
            CHECK(af_character_swim_step(NULL, s, &c, cc, &state, sc, (af_swim_input){.vertical = -1},
                                         dt, filter));
        CHECK(c.position.y < -1 && state.head_underwater);
        for (unsigned i = 0; i < rates[rate] * 2; ++i)
            CHECK(af_character_swim_step(NULL, s, &c, cc, &state, sc, (af_swim_input){.vertical = 1}, dt,
                                         filter));
        CHECK(c.position.y > -.5f && !state.head_underwater);
        c = (af_character){.position = {0, -.27f, 0}};
        state = (af_swim_state){0};
        CHECK(af_character_swim_step(NULL, s, &c, cc, &state, sc, (af_swim_input){.jump = true}, dt,
                                     filter));
        CHECK(c.velocity.y > 4 && !state.swimming);
        c = (af_character){.position = {0, -1, 0}};
        state = (af_swim_state){0};
        af_object_id wall = af_scene_add_object(s, "Wall", "cube");
        af_transform t = af_transform_identity();
        t.position.x = 1;
        CHECK(af_scene_set_object_transform(s, wall, t));
        af_collider collider = af_collider_default();
        collider.type = AF_COLLIDER_BOX;
        collider.half_extents = (af_vec3){.1f, 3, 3};
        CHECK(af_scene_set_object_collider(s, wall, collider));
        for (unsigned i = 0; i < rates[rate] * 2; ++i)
            CHECK(af_character_swim_step(NULL, s, &c, cc, &state, sc, (af_swim_input){.move = {1, 0, 0}},
                                         dt, filter));
        CHECK(c.position.x < .56f && c.position.x > .4f);
        CHECK(af_scene_remove_object(s, wall));
        w.mode = AF_WATER_RIVER;
        w.half_size.x = 2;
        w.flow_speed = 1.5f;
        w.path[0] = (af_vec3){0, 0, -10};
        w.path[1] = (af_vec3){0, 0, 10};
        CHECK(af_scene_set_water(s, id, w));
        c = (af_character){.position = {0, -.27f, 0}};
        state = (af_swim_state){0};
        for (unsigned i = 0; i < rates[rate] * 2; ++i)
            CHECK(af_character_swim_step(NULL, s, &c, cc, &state, sc, (af_swim_input){0}, dt, filter));
        CHECK(c.position.z > 2 && c.position.z < 3.2f);
        c.position.x = 3;
        CHECK(af_character_swim_step(NULL, s, &c, cc, &state, sc, (af_swim_input){0}, dt, filter));
        CHECK(!state.swimming && state.immersion == 0 && c.velocity.y < 0);
        af_character before = c;
        af_swim_state saved = state;
        CHECK(!af_character_swim_step(NULL, s, &c, cc, &state, sc, (af_swim_input){.vertical = NAN}, dt,
                                      filter));
        CHECK(c.position.y == before.position.y && state.swimming == saved.swimming);
        af_scene_destroy(s);
    }
    CHECK(fabsf(heights[0] - heights[2]) < .03f);
    puts("Swimming: entry/surface equilibrium, dive/ascend, jump exit, collision walls, currents, dry "
         "exit and rate consistency passed.");
    return 0;
}
