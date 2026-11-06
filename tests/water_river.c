/* SPDX-License-Identifier: MPL-2.0 */

#include <afterlight/physics.h>
#include <afterlight/water.h>
#include <math.h>
#include <stdio.h>
#define CHECK(x)                                                                                        \
    do {                                                                                                \
        if (!(x)) {                                                                                     \
            fprintf(stderr, "River %d: %s\n", __LINE__, #x);                                            \
            return 1;                                                                                   \
        }                                                                                               \
    } while (0)
int main(void) {
    for (unsigned quality = 0; quality < 3; ++quality) {
        af_scene *s = af_scene_create();
        af_physics *p = af_physics_create();
        CHECK(s && p);
        af_object_id id = af_scene_add_object(s, "River", "__node");
        af_water w = af_water_default();
        w.mode = AF_WATER_RIVER;
        w.wind = 0;
        w.half_size.x = 1;
        w.quality = (af_water_quality)quality;
        w.path[0] = (af_vec3){0, .5f, -5};
        w.path[1] = (af_vec3){0, -.5f, 5};
        w.flow_speed = 1;
        w.depth = 2;
        CHECK(af_scene_set_water(s, id, w));
        af_water_sample a = af_physics_sample_water(p, s, (af_vec3){0, 0, -4}),
                        b = af_physics_sample_water(p, s, (af_vec3){0, 0, 4});
        CHECK(a.inside && b.inside && a.height > b.height + .7f && a.velocity.z == 1 &&
              a.velocity.y < 0 && a.normal.z > 0);
        CHECK(!af_physics_sample_water(p, s, (af_vec3){2, 0, 0}).inside &&
              !af_physics_sample_water(p, s, (af_vec3){0, 0, -6}).inside);
        af_object_id wood = af_scene_add_object(s, "Wood", "cube");
        af_transform t = af_transform_identity();
        t.position = (af_vec3){0, .4f, -3};
        CHECK(af_scene_set_object_transform(s, wood, t));
        af_collider c = af_collider_default();
        c.type = AF_COLLIDER_BOX;
        c.half_extents = (af_vec3){.2f, .2f, .2f};
        CHECK(af_scene_set_object_collider(s, wood, c));
        af_rigid_body body = af_rigid_body_default();
        body.enabled = true;
        body.type = AF_BODY_DYNAMIC;
        body.mass = 32;
        CHECK(af_scene_set_body(s, wood, body));
        CHECK(af_physics_disturb_water(p, s, id, (af_vec3){0, 0, -2}, .5f, 1));
        for (unsigned tick = 0; tick < 240; ++tick)
            CHECK(af_physics_step(p, s, 1.f / 60));
        af_physics_state state = af_physics_get_state(p, wood);
        printf("River quality %u drift %.4f speed %.4f height %.4f\n", quality, state.position.z + 3,
               state.velocity.z, state.position.y);
        CHECK(state.position.z > -1 && state.position.z < 2 && state.velocity.z > .6f &&
              fabsf(state.position.x) < .2f && state.position.y > -.3f);
        w.path_count = 3;
        w.path[1] = (af_vec3){0, 0, 0};
        w.path[2] = (af_vec3){3, -.5f, 4};
        CHECK(af_scene_set_water(s, id, w));
        CHECK(af_scene_save(s, "river.json", NULL));
        af_scene *copy = af_scene_load("river.json", NULL);
        CHECK(copy && af_scene_get_water(copy, id)->path_count == 3);
        af_scene_destroy(copy);
        remove("river.json");
        af_physics_reset(p);
        a = af_physics_sample_water(p, s, (af_vec3){1.5f, 0, 2});
        CHECK(a.inside && a.velocity.x > .5f && a.velocity.z > .7f);
        w.path[1] = w.path[0];
        CHECK(!af_scene_set_water(s, id, w));
        af_physics_destroy(p);
        af_scene_destroy(s);
    }
    puts("Rivers: slope, direction, width/end boundaries, downstream buoyant drift at all qualities, "
         "bends and persistence passed.");
    return 0;
}
