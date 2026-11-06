/* SPDX-License-Identifier: MPL-2.0 */
#include "water_internal.h"
#include <afterlight/physics.h>
#include <math.h>
#include <stdio.h>
#define CHECK(x)                                                                                        \
    do {                                                                                                \
        if (!(x)) {                                                                                     \
            fprintf(stderr, "Water sleep %d: %s (%s)\n", __LINE__, #x, af_physics_error(p));            \
            return 1;                                                                                   \
        }                                                                                               \
    } while (0)
static af_object_id make_cup(af_scene *s) {
    af_object_id id = af_scene_add_object(s, "Cup", "__node");
    af_vec3 center[] = {{0, 0, 0}, {-.7f, .6f, 0}, {.7f, .6f, 0}, {0, .6f, -.7f}, {0, .6f, .7f}},
            half[] = {
                {.8f, .1f, .8f}, {.1f, .6f, .8f}, {.1f, .6f, .8f}, {.6f, .6f, .1f}, {.6f, .6f, .1f}};
    for (unsigned i = 0; i < 5; ++i) {
        af_collider_part part = af_collider_part_default();
        part.local.position = center[i];
        part.collider.half_extents = half[i];
        if (!af_scene_add_collider_part(s, id, part))
            return 0;
    }
    af_collider c = af_collider_default();
    c.type = AF_COLLIDER_COMPOUND;
    return af_scene_set_object_collider(s, id, c) ? id : 0;
}
int main(void) {
    af_physics *p = af_physics_create();
    af_scene *s = af_scene_create();
    CHECK(p && s);
    af_object_id owner = af_scene_add_object(s, "Pool", "__node");
    af_water w = af_water_default();
    w.wind = 0;
    CHECK(af_scene_set_water(s, owner, w));
    af_object_id wood = af_scene_add_object(s, "Wood sphere", "cube");
    af_collider c = af_collider_default();
    c.type = AF_COLLIDER_SPHERE;
    c.radius = .3f;
    CHECK(af_scene_set_object_collider(s, wood, c));
    af_rigid_body body = af_rigid_body_default();
    body.enabled = true;
    body.type = AF_BODY_DYNAMIC;
    body.mass = 500 * 4 * AF_PI * .027f / 3;
    CHECK(af_scene_set_body(s, wood, body));
    for (unsigned i = 0; i < 180; ++i)
        CHECK(af_physics_step(p, s, 1.f / 60));
    CHECK(af_physics_get_state(p, wood).sleeping && af_physics_water_stats(p).sleeping_surfaces == 1);
    float time = ph_water_runtime(p)->slots[0].time;
    for (unsigned i = 0; i < 30; ++i)
        CHECK(af_physics_step(p, s, 1.f / 60));
    CHECK(ph_water_runtime(p)->slots[0].time == time);
    CHECK(af_physics_disturb_water(p, s, owner, (af_vec3){0}, 1, 1) && af_physics_step(p, s, 1.f / 60));
    CHECK(!af_physics_get_state(p, wood).sleeping && af_physics_water_stats(p).sleeping_surfaces == 0);
    w.density = 500;
    CHECK(af_scene_set_water(s, owner, w));
    for (unsigned i = 0; i < 120; ++i)
        CHECK(af_physics_step(p, s, 1.f / 60));
    CHECK(af_physics_get_state(p, wood).position.y < -.15f);
    af_scene_destroy(s);
    af_physics_reset(p);
    s = af_scene_create();
    af_object_id cup = make_cup(s);
    owner = af_scene_add_object(s, "Fluid", "__node");
    af_transform t = af_transform_identity();
    t.position.y = .52f;
    CHECK(af_scene_set_object_transform(s, owner, t));
    w = af_water_default();
    w.mode = AF_WATER_PARTICLES;
    w.half_size = (af_vec2){.5f, .5f};
    w.initial_volume = .4096f;
    w.emission_velocity = (af_vec3){0};
    CHECK(af_scene_set_water(s, owner, w));
    for (unsigned i = 0; i < 480; ++i)
        CHECK(af_physics_step(p, s, 1.f / 60));
    printf("Particle sleep count %zu/%zu\n", af_physics_water_stats(p).sleeping_particles,
           af_physics_water_stats(p).particles);
    CHECK(af_physics_water_stats(p).sleeping_particles == 100);
    af_water_particle before[100], after[100];
    CHECK(af_physics_water_particles(p, owner, before, 100) == 100);
    for (unsigned i = 0; i < 30; ++i)
        CHECK(af_physics_step(p, s, 1.f / 60));
    CHECK(af_physics_water_particles(p, owner, after, 100) == 100);
    for (unsigned i = 0; i < 100; ++i)
        CHECK(af_vec3_length(af_vec3_sub(before[i].position, after[i].position)) == 0);
    t = af_transform_identity();
    t.rotation.z = 1.4f;
    t.position.y = 1;
    CHECK(af_scene_set_object_transform(s, cup, t) && af_physics_step(p, s, 1.f / 60));
    CHECK(af_physics_water_stats(p).sleeping_particles == 0);
    af_physics_destroy(p);
    af_scene_destroy(s);
    puts("Calm water/floating body/contained particles sleep, stay fixed, and wake on waves, density "
         "and container changes.");
    return 0;
}
