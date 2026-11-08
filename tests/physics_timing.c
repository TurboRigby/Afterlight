/* SPDX-License-Identifier: MPL-2.0 */
#include <afterlight/physics.h>
#include <afterlight/simulation.h>
#include <math.h>
#include <stdio.h>
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            fprintf(stderr, "Physics timing %d: %s\n", __LINE__, #x);                                                  \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
static af_scene *scene(af_object_id *ball) {
    af_scene *s = af_scene_create();
    af_object_id ground = af_scene_add_object(s, "Floor", "cube");
    af_collider c = af_collider_default();
    c.type = AF_COLLIDER_BOX;
    c.half_extents = (af_vec3){10, .5f, 10};
    af_scene_set_object_collider(s, ground, c);
    af_transform t = af_transform_identity();
    t.position.y = -.5f;
    af_scene_set_object_transform(s, ground, t);
    *ball = af_scene_add_object(s, "Ball", "cube");
    c.type = AF_COLLIDER_SPHERE;
    c.radius = .3f;
    af_scene_set_object_collider(s, *ball, c);
    t.position.y = 3;
    af_scene_set_object_transform(s, *ball, t);
    af_rigid_body body = af_rigid_body_default();
    body.enabled = true;
    body.type = AF_BODY_DYNAMIC;
    body.restitution = .8f;
    body.continuous = true;
    af_scene_set_body(s, *ball, body);
    return s;
}
int main(void) {
    unsigned rates[] = {30, 144};
    af_vec3 results[2];
    for (unsigned r = 0; r < 2; ++r) {
        af_object_id ball;
        af_scene *s = scene(&ball);
        af_physics *p = af_physics_create();
        af_fixed_clock clock = {0};
        bool bounced = false;
        for (unsigned frame = 0; frame < rates[r] * 3; ++frame) {
            unsigned ticks = af_fixed_clock_advance(&clock, 1.0 / rates[r], false, false);
            for (unsigned i = 0; i < ticks; ++i) {
                CHECK(af_physics_step(p, s, (float)AF_FIXED_DELTA));
                bounced |= af_physics_get_state(p, ball).velocity.y > 2;
            }
        }
        CHECK(bounced);
        results[r] = af_physics_get_state(p, ball).position;
        af_physics_destroy(p);
        af_scene_destroy(s);
    }
    CHECK(af_vec3_length(af_vec3_sub(results[0], results[1])) < .0001f);
    af_scene *s = af_scene_create();
    af_physics *p = af_physics_create();
    af_object_id platform = af_scene_add_object(s, "Kinematic support", "cube"),
                 box = af_scene_add_object(s, "Rider", "cube");
    af_collider collider = af_collider_default();
    collider.type = AF_COLLIDER_BOX;
    collider.half_extents = (af_vec3){4, .5f, 4};
    CHECK(af_scene_set_object_collider(s, platform, collider));
    collider.half_extents = (af_vec3){.5f, .5f, .5f};
    CHECK(af_scene_set_object_collider(s, box, collider));
    af_transform t = af_transform_identity();
    t.position.y = 1;
    CHECK(af_scene_set_object_transform(s, box, t));
    af_rigid_body body = af_rigid_body_default();
    body.enabled = true;
    body.type = AF_BODY_KINEMATIC;
    CHECK(af_scene_set_body(s, platform, body));
    body.type = AF_BODY_DYNAMIC;
    CHECK(af_scene_set_body(s, box, body));
    for (unsigned i = 0; i < 120; ++i)
        CHECK(af_physics_step(p, s, 1.f / 60));
    CHECK(af_physics_get_state(p, box).sleeping);
    for (unsigned i = 0; i < 60; ++i) {
        t = af_transform_identity();
        t.position.x = (float)(i + 1) * .5f / 60;
        CHECK(af_scene_set_object_transform(s, platform, t) && af_physics_step(p, s, 1.f / 60));
    }
    CHECK(af_physics_get_state(p, box).position.x > .25f && af_physics_get_state(p, box).position.y > .95f &&
          af_physics_get_stats(p).kinematic_bodies == 1);
    af_physics_destroy(p);
    af_scene_destroy(s);
    puts("Physics timing: 30/144 Hz render independence and restitution bounce passed.");
    return 0;
}
