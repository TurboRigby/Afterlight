/* SPDX-License-Identifier: MPL-2.0 */

#include <afterlight/physics.h>
#include <afterlight/water.h>
#include <math.h>
#include <stdio.h>
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            fprintf(stderr, "Particles %d: %s (%s)\n", __LINE__, #x, af_physics_error(p));                             \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
static af_object_id cup(af_scene *s) {
    af_object_id id = af_scene_add_object(s, "Cup", "__node");
    af_vec3 centers[] = {{0, 0, 0}, {-.7f, .6f, 0}, {.7f, .6f, 0}, {0, .6f, -.7f}, {0, .6f, .7f}},
            half[] = {{.8f, .1f, .8f}, {.1f, .6f, .8f}, {.1f, .6f, .8f}, {.6f, .6f, .1f}, {.6f, .6f, .1f}};
    for (unsigned i = 0; i < 5; ++i) {
        af_collider_part part = af_collider_part_default();
        part.local.position = centers[i];
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
    CHECK(p);
    af_water_particle particles[4096];
    for (unsigned quality = 0; quality < 3; ++quality) {
        af_scene *s = af_scene_create();
        af_object_id container = cup(s), owner = af_scene_add_object(s, "Fluid", "__node");
        CHECK(container && owner);
        af_transform t = af_transform_identity();
        t.position.y = .52f;
        CHECK(af_scene_set_object_transform(s, owner, t));
        af_water w = af_water_default();
        w.mode = AF_WATER_PARTICLES;
        w.quality = (af_water_quality)quality;
        w.half_size = (af_vec2){.5f, .5f};
        w.initial_volume = .4096f;
        w.emission_velocity = (af_vec3){0};
        w.viscosity = 3;
        CHECK(af_scene_set_water(s, owner, w));
        af_physics_reset(p);
        for (unsigned tick = 0; tick < 240; ++tick)
            CHECK(af_physics_step(p, s, 1.f / 60));
        size_t count = af_physics_water_particles(p, owner, particles, 4096), inside = 0;
        float speed = 0;
        for (size_t i = 0; i < count; ++i) {
            af_vec3 q = particles[i].position;
            CHECK(isfinite(q.x) && isfinite(q.y) && q.y > 0);
            if (fabsf(q.x) < .65f && fabsf(q.z) < .65f && q.y < 1.2f)
                ++inside;
            speed = fmaxf(speed, af_vec3_length(particles[i].velocity));
        }
        af_water_stats stats = af_physics_water_stats(p);
        printf("Quality %u: %zu particles, %zu in cup, speed %.4f, volume %.5f emitted %.5f\n", quality, count, inside,
               speed, stats.particle_volume, stats.emitted_volume);
        CHECK(count == 100 && inside >= 95 && speed < 2);
        CHECK(fabsf(stats.particle_volume + stats.escaped_volume - stats.emitted_volume) < .00001f);
        CHECK(!af_physics_sample_water(p, s, (af_vec3){0}).inside);
        t = af_transform_identity();
        t.position.y = 1;
        t.rotation.z = 1.5f;
        CHECK(af_scene_set_object_transform(s, container, t));
        for (unsigned tick = 0; tick < 120; ++tick)
            CHECK(af_physics_step(p, s, 1.f / 60));
        count = af_physics_water_particles(p, owner, particles, 4096);
        size_t spilled = 0;
        for (size_t i = 0; i < count; ++i)
            if (particles[i].position.y < .1f || fabsf(particles[i].position.x) > .9f)
                ++spilled;
        printf("Quality %u spilled %zu/%zu.\n", quality, spilled, count);
        CHECK(spilled > count / 2);
        CHECK(af_scene_save(s, "particles.json", NULL));
        af_scene *copy = af_scene_load("particles.json", NULL);
        CHECK(copy && af_scene_get_water(copy, owner)->mode == AF_WATER_PARTICLES);
        af_scene_destroy(copy);
        remove("particles.json");
        af_scene_destroy(s);
    }
    af_physics_reset(p);
    CHECK(af_physics_water_stats(p).particles == 0);
    af_physics_destroy(p);
    puts("Pourable fluid: all qualities fill a compound cup, remain bounded, conserve volume, spill "
         "after tilting, persist and reset.");
    return 0;
}
