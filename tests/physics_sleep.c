/* SPDX-License-Identifier: MPL-2.0 */
#include <afterlight/physics.h>
#include <math.h>
#include <stdio.h>
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            fprintf(stderr, "Sleep/CCD %d: %s\n", __LINE__, #x);                                                       \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
static af_object_id shape(af_scene *s, af_collider_type type, af_vec3 pos, af_vec3 half, bool dynamic) {
    af_object_id id = af_scene_add_object(s, "Shape", "cube");
    af_collider c = af_collider_default();
    c.type = type;
    c.half_extents = half;
    c.radius = .1f;
    af_scene_set_object_collider(s, id, c);
    af_transform t = af_transform_identity();
    t.position = pos;
    af_scene_set_object_transform(s, id, t);
    if (dynamic) {
        af_rigid_body b = af_rigid_body_default();
        b.enabled = true;
        b.type = AF_BODY_DYNAMIC;
        b.continuous = true;
        b.restitution = 0;
        af_scene_set_body(s, id, b);
    }
    return id;
}
int main(void) {
    af_scene *s = af_scene_create();
    af_physics *p = af_physics_create();
    af_object_id ground = shape(s, AF_COLLIDER_BOX, (af_vec3){0, -.5f, 0}, (af_vec3){10, .5f, 10}, false);
    af_object_id box = shape(s, AF_COLLIDER_BOX, (af_vec3){0, 1, 0}, (af_vec3){.5f, .5f, .5f}, true);
    for (unsigned i = 0; i < 240; ++i)
        CHECK(af_physics_step(p, s, 1.f / 60));
    CHECK(af_physics_get_state(p, box).sleeping);
    af_vec3 old_position = af_physics_get_state(p, box).position;
    CHECK(af_physics_apply_impulse(p, s, box, (af_vec3){2, 2, 0}, old_position) &&
          !af_physics_get_state(p, box).sleeping);
    CHECK(af_physics_step(p, s, 1.f / 60) && af_physics_get_state(p, box).position.x > old_position.x + .01f &&
          af_physics_get_state(p, box).position.y > old_position.y);
    for (unsigned i = 0; i < 240; ++i)
        CHECK(af_physics_step(p, s, 1.f / 60));
    CHECK(af_scene_remove_object(s, ground));
    CHECK(af_physics_step(p, s, 1.f / 60) && !af_physics_get_state(p, box).sleeping);
    CHECK(af_physics_step(p, s, 1.f / 60) && af_physics_get_state(p, box).velocity.y < 0);
    af_scene_destroy(s);
    af_physics_reset(p);
    s = af_scene_create();
    shape(s, AF_COLLIDER_BOX, (af_vec3){0}, (af_vec3){.01f, 3, 3}, false);
    af_object_id ball = shape(s, AF_COLLIDER_SPHERE, (af_vec3){-1, 0, 0}, (af_vec3){0}, true);
    af_rigid_body b = af_scene_get_object(s, ball)->body;
    b.velocity.x = 180;
    b.gravity_scale = 0;
    CHECK(af_scene_set_body(s, ball, b));
    CHECK(af_physics_step(p, s, 1.f / 60));
    CHECK(af_physics_get_state(p, ball).position.x < -.09f && af_physics_get_stats(p).ccd_hits > 0);
    af_scene_destroy(s);
    af_physics_reset(p);
    s = af_scene_create();
    shape(s, AF_COLLIDER_BOX, (af_vec3){0, -.5f, 0}, (af_vec3){5, .5f, 5}, false);
    ball = shape(s, AF_COLLIDER_SPHERE, (af_vec3){0, .098f, 0}, (af_vec3){0}, true);
    b = af_scene_get_object(s, ball)->body;
    b.gravity_scale = 0;
    b.velocity = (af_vec3){2, 2, 0};
    CHECK(af_scene_set_body(s, ball, b) && af_physics_step(p, s, 1.f / 60));
    CHECK(af_physics_get_state(p, ball).position.x > .02f && af_physics_get_state(p, ball).position.y > .12f);
    af_physics_destroy(p);
    af_scene_destroy(s);
    puts("Sleep/CCD: rest, impulse wake, support removal and a fast sphere against a thin wall passed.");
    return 0;
}
