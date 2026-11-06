/* SPDX-License-Identifier: MPL-2.0 */
#include <afterlight/physics.h>
#include <afterlight/water.h>
#include <math.h>
#include <stdio.h>
#define CHECK(x)                                                                                        \
    do {                                                                                                \
        if (!(x)) {                                                                                     \
            fprintf(stderr, "Fluid boundary %d: %s (%s)\n", __LINE__, #x, af_physics_error(p));         \
            return 1;                                                                                   \
        }                                                                                               \
    } while (0)
static af_object_id fluid(af_scene *s, af_vec3 center, af_vec3 velocity, unsigned count) {
    af_object_id id = af_scene_add_object(s, "Fluid", "__node");
    af_transform t = af_transform_identity();
    t.position = center;
    if (!af_scene_set_object_transform(s, id, t))
        return 0;
    af_water w = af_water_default();
    w.mode = AF_WATER_PARTICLES;
    w.half_size = (af_vec2){.25f, .25f};
    w.initial_volume = (float)count * .16f * .16f * .16f;
    w.emission_velocity = velocity;
    return af_scene_set_water(s, id, w) ? id : 0;
}
int main(void) {
    af_physics *p = af_physics_create();
    CHECK(p);
    const float masses[] = {.1f, 10, 1000, 10000};
    for (unsigned test = 0; test < 4; ++test) {
        af_scene *s = af_scene_create();
        af_object_id wall = af_scene_add_object(s, "Thin receiver", "cube");
        af_collider c = af_collider_default();
        c.type = AF_COLLIDER_BOX;
        c.half_extents = (af_vec3){.01f, 1, 1};
        CHECK(af_scene_set_object_collider(s, wall, c));
        af_rigid_body b = af_rigid_body_default();
        b.enabled = true;
        b.type = AF_BODY_DYNAMIC;
        b.mass = masses[test];
        b.gravity_scale = 0;
        b.linear_damping = 0;
        b.angular_damping = .5f;
        CHECK(af_scene_set_body(s, wall, b));
        CHECK(fluid(s, (af_vec3){-1, 0, 0}, (af_vec3){5, 0, 0}, 9));
        af_physics_reset(p);
        for (unsigned tick = 0; tick < 30; ++tick)
            CHECK(af_physics_step(p, s, 1.f / 60));
        af_physics_state result = af_physics_get_state(p, wall);
        printf("Mass %.2f receiver speed %.4f\n", masses[test], result.velocity.x);
        CHECK(result.velocity.x > 0 && result.velocity.x < 6 && isfinite(result.angular_velocity.y));
        af_scene_destroy(s);
    }
    for (unsigned rotation = 0; rotation < 2; ++rotation) {
        af_scene *s = af_scene_create();
        af_object_id wall = af_scene_add_object(s, "Sweeping panel", "cube");
        af_collider c = af_collider_default();
        c.type = AF_COLLIDER_BOX;
        c.half_extents = (af_vec3){.01f, 1, 1.5f};
        CHECK(af_scene_set_object_collider(s, wall, c));
        af_transform t = af_transform_identity();
        if (!rotation)
            t.position.x = -.5f;
        CHECK(af_scene_set_object_transform(s, wall, t));
        af_rigid_body b = af_rigid_body_default();
        b.enabled = true;
        b.type = AF_BODY_KINEMATIC;
        CHECK(af_scene_set_body(s, wall, b));
        af_object_id owner =
            fluid(s, rotation ? (af_vec3){1.16f, 0, .16f} : (af_vec3){.16f, 0, .16f}, (af_vec3){0}, 1);
        CHECK(owner);
        af_physics_reset(p);
        CHECK(af_physics_step(p, s, 1.f / 60));
        if (rotation)
            t.rotation.y = AF_PI * .5f;
        else
            t.position.x = .5f;
        CHECK(af_scene_set_object_transform(s, wall, t) && af_physics_step(p, s, 1.f / 30));
        af_water_particle particle;
        CHECK(af_physics_water_particles(p, owner, &particle, 1) == 1);
        printf("Panel %u particle %.4f %.4f %.4f\n", rotation, particle.position.x, particle.position.y,
               particle.position.z);
        CHECK(rotation ? particle.position.z < -.05f : particle.position.x > .55f);
        CHECK(af_vec3_length(particle.velocity) <= 30.01f);
        af_scene_destroy(s);
    }
    af_physics_destroy(p);
    puts("Fluid boundaries: light/heavy thin receivers, effective mass and translating/rotating "
         "kinematic sweeps passed.");
    return 0;
}
