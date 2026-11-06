/* SPDX-License-Identifier: MPL-2.0 */
#include <afterlight/physics.h>
#include <afterlight/water.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#define CHECK(x)                                                                                        \
    do {                                                                                                \
        if (!(x)) {                                                                                     \
            fprintf(stderr, "Course scene %d: %s (%s)\n", __LINE__, #x, af_physics_error(p));           \
            return 1;                                                                                   \
        }                                                                                               \
    } while (0)
int main(int argc, char **argv) {
    if (argc != 3)
        return 2;
    af_physics *p = af_physics_create();
    CHECK(p);
    for (unsigned hard = 0; hard < 2; ++hard) {
        af_scene_error error;
        af_scene *s = af_scene_load(argv[hard + 1], &error);
        if (!s)
            fprintf(stderr, "%s\n", error.message);
        CHECK(s && af_physics_validate_scene(s, NULL, 0));
        unsigned checkpoints = 0, players = 0, goals = 0;
        for (size_t i = 0; i < af_scene_object_count(s); ++i) {
            const af_scene_object *o = af_scene_object_at(s, i);
            checkpoints += o->gameplay.role == AF_ROLE_CHECKPOINT;
            players += o->gameplay.player.enabled;
            goals += o->gameplay.role == AF_ROLE_GOAL;
        }
        CHECK(players == 1 && goals == 1 && checkpoints == (hard ? 3u : 2u) &&
              af_scene_water_count(s) == (hard ? 3u : 1u));
        CHECK(af_scene_get_render_settings(s).underwater && af_scene_get_render_settings(s).refraction);
        af_physics_reset(p);
        for (unsigned tick = 0; tick < 360; ++tick) {
            CHECK(af_physics_step(p, s, 1.f / 60));
            for (size_t i = 0; i < af_scene_object_count(s); ++i) {
                const af_scene_object *o = af_scene_object_at(s, i);
                if (o->body.enabled && o->body.type == AF_BODY_DYNAMIC) {
                    af_physics_state b = af_physics_get_state(p, o->id);
                    CHECK(isfinite(b.position.x) && isfinite(b.position.y) && b.position.y > -4 &&
                          b.position.y < 5);
                }
            }
        }
        if (hard) {
            af_object_id source = 0, receiver = 0, fluid = 0;
            for (size_t i = 0; i < af_scene_object_count(s); ++i) {
                const af_scene_object *o = af_scene_object_at(s, i);
                if (!strcmp(o->name, "Source cup"))
                    source = o->id;
                if (!strcmp(o->name, "Receiving cup"))
                    receiver = o->id;
                if (!strcmp(o->name, "Course pour"))
                    fluid = o->id;
            }
            float maximum = 0;
            for (unsigned tick = 0; tick < 600; ++tick) {
                const af_scene_object *cup = af_scene_get_object(s, source);
                af_transform t = cup->transform;
                t.rotation.z = -fminf(1.65f, (float)tick * .65f / 60);
                CHECK(af_scene_set_object_transform(s, source, t) && af_physics_step(p, s, 1.f / 60));
                af_water_particle particles[4096];
                size_t n = af_physics_water_particles(p, fluid, particles, 4096);
                af_vec3 center = af_scene_get_object(s, receiver)->transform.position;
                float volume = 0;
                af_vec3 low = {100, 100, 100}, high = {-100, -100, -100};
                for (size_t i = 0; i < n && i < 4096; ++i) {
                    af_vec3 q = particles[i].position;
                    if (fabsf(q.x - center.x) < .64f && fabsf(q.z - center.z) < .64f && q.y > center.y &&
                        q.y < center.y + 1.2f)
                        volume += particles[i].volume;
                    low = (af_vec3){fminf(low.x, q.x), fminf(low.y, q.y), fminf(low.z, q.z)};
                    high = (af_vec3){fmaxf(high.x, q.x), fmaxf(high.y, q.y), fmaxf(high.z, q.z)};
                }
                maximum = fmaxf(maximum, volume);
                if (tick % 60 == 0)
                    fprintf(stderr, "Pour tick %u vol %.4f bounds %.2f %.2f %.2f to %.2f %.2f %.2f\n",
                            tick, volume, low.x, low.y, low.z, high.x, high.y, high.z);
            }
            CHECK(maximum >= .05f);
        }
        CHECK(af_scene_save(s, "course-roundtrip.json", NULL));
        af_scene *copy = af_scene_load("course-roundtrip.json", NULL);
        CHECK(copy && af_scene_water_count(copy) == af_scene_water_count(s));
        af_scene_destroy(copy);
        remove("course-roundtrip.json");
        af_scene_destroy(s);
    }
    af_physics_destroy(p);
    puts("Course scenes: ordered simple/expedition components, finite buoyant cargo, combined water and "
         "persistence passed.");
    return 0;
}
