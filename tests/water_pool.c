/* SPDX-License-Identifier: MPL-2.0 */

#include <afterlight/physics.h>
#include <afterlight/water.h>
#include <math.h>
#include <stdio.h>
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            fprintf(stderr, "Pool %d: %s (%s)\n", __LINE__, #x, af_physics_error(p));                                  \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
static af_object_id body(af_scene *s, const char *name, float x, float density, bool sphere) {
    af_object_id id = af_scene_add_object(s, name, "cube");
    af_transform t = af_transform_identity();
    t.position = (af_vec3){x, 1.4f, 0};
    af_scene_set_object_transform(s, id, t);
    af_collider c = af_collider_default();
    c.type = sphere ? AF_COLLIDER_SPHERE : AF_COLLIDER_BOX;
    c.radius = .3f;
    c.half_extents = (af_vec3){.3f, .3f, .3f};
    af_scene_set_object_collider(s, id, c);
    af_rigid_body b = af_rigid_body_default();
    b.enabled = true;
    b.type = AF_BODY_DYNAMIC;
    b.mass = density * (sphere ? 4 * AF_PI * .027f / 3 : .216f);
    b.restitution = 0;
    af_scene_set_body(s, id, b);
    return id;
}
int main(void) {
    af_physics *p = af_physics_create();
    CHECK(p);
    float final[3][3];
    for (unsigned quality = 0; quality < 3; ++quality) {
        af_scene *s = af_scene_create();
        CHECK(s);
        af_object_id surface = af_scene_add_object(s, "Pool", "__node");
        af_water w = af_water_default();
        w.quality = (af_water_quality)quality;
        w.wind = 0;
        w.depth = 3;
        CHECK(af_scene_set_water(s, surface, w));
        af_object_id wood = body(s, "Wood", -2, 500, false), ball = body(s, "Ball", 0, 500, true),
                     stone = body(s, "Stone", 2, 2500, true);
        af_object_id floor = af_scene_add_object(s, "Floor", "cube");
        af_transform ft = af_transform_identity();
        ft.position.y = -3.4f;
        CHECK(af_scene_set_object_transform(s, floor, ft));
        af_collider fc = af_collider_default();
        fc.type = AF_COLLIDER_BOX;
        fc.half_extents = (af_vec3){5, .3f, 5};
        CHECK(af_scene_set_object_collider(s, floor, fc));
        af_physics_reset(p);
        CHECK(af_physics_active(s));
        for (unsigned i = 0; i < 900; ++i) {
            CHECK(af_physics_step(p, s, 1.f / 60));
            CHECK(isfinite(af_physics_get_state(p, wood).position.y));
        }
        final[quality][0] = af_physics_get_state(p, wood).position.y;
        final[quality][1] = af_physics_get_state(p, ball).position.y;
        final[quality][2] = af_physics_get_state(p, stone).position.y;
        printf("quality %u: wood %.4f ball %.4f stone %.4f, wave %.5f\n", quality, final[quality][0], final[quality][1],
               final[quality][2], af_physics_water_stats(p).max_wave);
        CHECK(fabsf(final[quality][0]) < .1f && fabsf(final[quality][1]) < .1f && final[quality][2] < -2.7f &&
              final[quality][2] > -3);
        CHECK(af_vec3_length(af_physics_get_state(p, wood).velocity) < .08f);
        af_water_sample sample = af_physics_sample_water(p, s, (af_vec3){1, 0, 1});
        CHECK(sample.inside && sample.owner == surface && sample.density == 1000 && sample.bottom == -3);
        CHECK(!af_physics_sample_water(p, s, (af_vec3){20, 0, 0}).inside);
        CHECK(af_physics_disturb_water(p, s, surface, (af_vec3){1, 0, 1}, .8f, 1));
        CHECK(af_physics_step(p, s, 1.f / 60));
        CHECK(fabsf(af_physics_sample_water(p, s, (af_vec3){1, 0, 1}).height) > .001f);
        for (unsigned i = 0; i < 180; ++i)
            CHECK(af_physics_step(p, s, 1.f / 60));
        CHECK(af_physics_water_stats(p).max_wave < .5f);
        af_scene_destroy(s);
    }
    CHECK(fabsf(final[0][0] - final[2][0]) < .05f);
    af_physics_reset(p);
    CHECK(af_physics_water_stats(p).surfaces == 0);
    af_physics_destroy(p);
    puts("Pools: wood/ball float, dense stone sinks, all qualities settle, bounded interactive waves "
         "and reset passed.");
    return 0;
}
