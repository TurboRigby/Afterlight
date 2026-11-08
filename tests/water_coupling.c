/* SPDX-License-Identifier: MPL-2.0 */
#include <afterlight/physics.h>
#include <afterlight/water.h>
#include <math.h>
#include <stdio.h>
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            fprintf(stderr, "Fluid coupling %d: %s\n", __LINE__, #x);                                                  \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
int main(void) {
    af_physics *p = af_physics_create();
    CHECK(p);
    af_scene *s = af_scene_create();
    CHECK(s);
    af_object_id wall = af_scene_add_object(s, "Movable receiver", "cube");
    af_collider c = af_collider_default();
    c.type = AF_COLLIDER_BOX;
    c.half_extents = (af_vec3){.06f, 1, 1};
    CHECK(af_scene_set_object_collider(s, wall, c));
    af_rigid_body b = af_rigid_body_default();
    b.enabled = true;
    b.type = AF_BODY_DYNAMIC;
    b.mass = 100;
    b.gravity_scale = 0;
    b.linear_damping = 0;
    b.angular_damping = 0;
    CHECK(af_scene_set_body(s, wall, b));
    af_object_id owner = af_scene_add_object(s, "Fluid jet", "__node");
    af_transform t = af_transform_identity();
    t.position.x = -1;
    CHECK(af_scene_set_object_transform(s, owner, t));
    af_water w = af_water_default();
    w.mode = AF_WATER_PARTICLES;
    w.half_size = (af_vec2){.25f, .25f};
    w.initial_volume = 9 * .16f * .16f * .16f;
    w.emission_velocity = (af_vec3){5, 0, 0};
    CHECK(af_scene_set_water(s, owner, w));
    for (unsigned i = 0; i < 30; ++i)
        CHECK(af_physics_step(p, s, 1.f / 60));
    af_physics_state receiver = af_physics_get_state(p, wall);
    printf("Fluid receiver velocity %.5f, position %.5f\n", receiver.velocity.x, receiver.position.x);
    CHECK(receiver.velocity.x > .1f && receiver.velocity.x < 3 && fabsf(receiver.velocity.z) < .5f &&
          isfinite(receiver.position.y));
    af_water_stats stats = af_physics_water_stats(p);
    CHECK(fabsf(stats.emitted_volume - stats.particle_volume - stats.escaped_volume) < .00001f);
    af_physics_destroy(p);
    af_scene_destroy(s);
    puts("Fluid contact transfers bounded momentum to a dynamic thin receiver.");
    return 0;
}
