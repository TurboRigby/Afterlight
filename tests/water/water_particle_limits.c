/* SPDX-License-Identifier: MPL-2.0 */

#include "water/water_internal.h"
#include <afterlight/physics.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            fprintf(stderr, "Particle limits %d: %s\n", __LINE__, #x);                                                 \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
int main(void) {
    af_physics *p = af_physics_create();
    CHECK(p);
    for (unsigned quality = 0; quality < 3; ++quality) {
        af_scene *s = af_scene_create();
        af_object_id id = af_scene_add_object(s, "Fluid", "__node");
        af_water w = af_water_default();
        w.mode = AF_WATER_PARTICLES;
        w.quality = (af_water_quality)quality;
        w.half_size = (af_vec2){2, 2};
        w.initial_volume = 0;
        w.emission_rate = 1;
        CHECK(af_scene_set_water(s, id, w));
        af_transform invalid = af_transform_identity();
        invalid.position.x = 1e30f;
        CHECK(!af_scene_set_object_transform(s, id, invalid));
        af_physics_reset(p);
        CHECK(af_physics_step(p, s, 1.f / 60));
        af_water_stats stats = af_physics_water_stats(p);
        CHECK(stats.particles > 0 && stats.particles < 20 && stats.emitted_volume > 0);
        size_t before = stats.particles;
        w.emission_rate = 0;
        CHECK(af_scene_set_water(s, id, w));
        CHECK(af_physics_step(p, s, 1.f / 60));
        CHECK(af_physics_water_stats(p).particles == before);
        af_vertex *v;
        uint32_t *ix;
        af_mesh_data mesh;
        CHECK(wa_mesh(ph_water_runtime(p), s, id, .5f, &v, &ix, &mesh));
        CHECK(mesh.vertex_count > 0 && mesh.vertex_count <= 180000 && mesh.index_count % 3 == 0);
        for (uint32_t k = 0; k < mesh.vertex_count; ++k)
            CHECK(isfinite(v[k].position.x) && isfinite(v[k].normal.x));
        free(v);
        free(ix);
        size_t capacity = 256u << (2u * quality);
        w.initial_volume = (float)capacity * w.particle_spacing * w.particle_spacing * w.particle_spacing;
        CHECK(af_scene_set_water(s, id, w));
        CHECK(af_physics_step(p, s, 1.f / 240));
        CHECK(af_physics_water_stats(p).particles == capacity);
        w.initial_volume += 1;
        CHECK(!af_scene_set_water(s, id, w));
        CHECK(af_scene_remove_water(s, id));
        CHECK(af_physics_step(p, s, 1.f / 60) && af_physics_water_stats(p).particles == 0);
        af_scene_destroy(s);
    }
    {
        af_scene *s = af_scene_create();
        af_object_id id = af_scene_add_object(s, "Blocked emitter", "__node"),
                     floor = af_scene_add_object(s, "Support", "cube");
        af_transform t = af_transform_identity();
        t.position.y = -.15f;
        CHECK(af_scene_set_object_transform(s, floor, t));
        af_collider c = af_collider_default();
        c.type = AF_COLLIDER_BOX;
        c.half_extents = (af_vec3){1, .1f, 1};
        CHECK(af_scene_set_object_collider(s, floor, c));
        af_water w = af_water_default();
        w.mode = AF_WATER_PARTICLES;
        w.half_size = (af_vec2){.25f, .25f};
        w.initial_volume = 0;
        w.emission_rate = 2;
        w.emission_velocity = (af_vec3){0};
        CHECK(af_scene_set_water(s, id, w));
        af_physics_reset(p);
        for (unsigned i = 0; i < 30; ++i)
            CHECK(af_physics_step(p, s, 1.f / 60));
        size_t before = af_physics_water_particles(p, id, NULL, 0);
        CHECK(before > 0 && before < 1024);
        w.emission_rate = 0;
        CHECK(af_scene_set_water(s, id, w) && af_scene_remove_object(s, floor));
        for (unsigned i = 0; i < 60; ++i)
            CHECK(af_physics_step(p, s, 1.f / 60));
        CHECK(af_physics_water_particles(p, id, NULL, 0) == before);
        af_scene_destroy(s);
    }
    af_physics_destroy(p);
    puts("Particles: emission stop without reset, volume/capacity bounds, finite bounded surface "
         "reconstruction and removal cleanup passed.");
    return 0;
}
