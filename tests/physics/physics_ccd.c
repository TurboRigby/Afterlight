/* SPDX-License-Identifier: MPL-2.0 */
#include <afterlight/physics.h>
#include <math.h>
#include <stdio.h>
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            fprintf(stderr, "CCD %d: %s\n", __LINE__, #x);                                                             \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
static af_object_id shape(af_scene *s, af_collider_type type, af_vec3 pos, af_vec3 half, af_vec3 velocity, af_vec3 spin,
                          bool dynamic) {
    af_object_id id = af_scene_add_object(s, "CCD shape", "cube");
    af_collider c = af_collider_default();
    c.type = type;
    c.radius = .12f;
    c.half_extents = half;
    af_transform t = af_transform_identity();
    t.position = pos;
    af_scene_set_object_transform(s, id, t);
    af_scene_set_object_collider(s, id, c);
    if (dynamic) {
        af_rigid_body b = af_rigid_body_default();
        b.enabled = true;
        b.type = AF_BODY_DYNAMIC;
        b.gravity_scale = 0;
        b.continuous = true;
        b.restitution = 0;
        b.velocity = velocity;
        b.angular_velocity = spin;
        af_scene_set_body(s, id, b);
    }
    return id;
}
int main(void) {
    af_scene *s = af_scene_create();
    af_physics *p = af_physics_create();
    shape(s, AF_COLLIDER_BOX, (af_vec3){0}, (af_vec3){.01f, 4, 4}, (af_vec3){0}, (af_vec3){0}, false);
    af_object_id box = shape(s, AF_COLLIDER_BOX, (af_vec3){-1, 0, 0}, (af_vec3){.5f, .1f, .1f}, (af_vec3){180, 0, 0},
                             (af_vec3){0}, true);
    CHECK(af_physics_step(p, s, 1.f / 60));
    CHECK(af_physics_get_state(p, box).position.x < -.49f && af_physics_get_stats(p).ccd_hits > 0);
    af_scene_destroy(s);
    af_physics_reset(p);
    s = af_scene_create();
    af_object_id a = shape(s, AF_COLLIDER_SPHERE, (af_vec3){-.5f, 0, 0}, (af_vec3){0}, (af_vec3){150, 0, 0},
                           (af_vec3){0}, true),
                 b = shape(s, AF_COLLIDER_SPHERE, (af_vec3){.5f, 0, 0}, (af_vec3){0}, (af_vec3){-150, 0, 0},
                           (af_vec3){0}, true);
    CHECK(af_physics_step(p, s, 1.f / 60));
    CHECK(af_physics_get_state(p, a).position.x < af_physics_get_state(p, b).position.x &&
          fabsf(af_physics_get_state(p, a).velocity.x) < 1);
    af_scene_destroy(s);
    af_physics_reset(p);
    s = af_scene_create();
    shape(s, AF_COLLIDER_BOX, (af_vec3){0, 1, 0}, (af_vec3){4, .01f, 4}, (af_vec3){0}, (af_vec3){0}, false);
    box = shape(s, AF_COLLIDER_BOX, (af_vec3){0, 0, 0}, (af_vec3){1.2f, .1f, .1f}, (af_vec3){0}, (af_vec3){0, 0, 100},
                true);
    CHECK(af_physics_step(p, s, 1.f / 60));
    CHECK(af_physics_get_stats(p).ccd_hits > 0);
    af_physics_state state = af_physics_get_state(p, box);
    CHECK(isfinite(state.angular_velocity.z) && state.position.y < .1f);
    af_scene_destroy(s);
    af_physics_destroy(p);
    puts("CCD: full-box thin-wall sweep, fast moving-body pair and rotating-corner contact passed.");
    return 0;
}
