/* SPDX-License-Identifier: MPL-2.0 */
#include <afterlight/physics.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#define CHECK(x)                                                                                        \
    do {                                                                                                \
        if (!(x)) {                                                                                     \
            fprintf(stderr, "Stress %d: %s\n", __LINE__, #x);                                           \
            return 1;                                                                                   \
        }                                                                                               \
    } while (0)
static af_object_id box(af_scene *s, af_vec3 pos, af_vec3 half, float mass) {
    af_object_id id = af_scene_add_object(s, "Stress body", "cube");
    af_collider c = af_collider_default();
    c.type = AF_COLLIDER_BOX;
    c.half_extents = half;
    af_transform t = af_transform_identity();
    t.position = pos;
    if (!af_scene_set_object_transform(s, id, t) || !af_scene_set_object_collider(s, id, c))
        return 0;
    if (mass > 0) {
        af_rigid_body b = af_rigid_body_default();
        b.enabled = true;
        b.type = AF_BODY_DYNAMIC;
        b.mass = mass;
        b.restitution = 0;
        if (!af_scene_set_body(s, id, b))
            return 0;
    }
    return id;
}
static int compare(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}
int main(void) {
    puts("bodies,samples,mean_cpu_ms,p95_cpu_ms,max_penetration,contacts,pairs");
    unsigned counts[] = {16, 64};
    for (unsigned test = 0; test < 2; ++test) {
        af_scene *s = af_scene_create();
        af_physics *p = af_physics_create();
        CHECK(s && p && box(s, (af_vec3){0, -.5f, 0}, (af_vec3){30, .5f, 30}, 0));
        af_object_id ids[64];
        for (unsigned i = 0; i < counts[test]; ++i) {
            unsigned stack = i / 4, level = i % 4;
            ids[i] = box(s,
                         (af_vec3){(float)(stack % 4) * 3 - 5, .5f + (float)level * 1.01f,
                                   (float)(stack / 4) * 3 - 5},
                         (af_vec3){.5f, .5f, .5f}, level == 3 ? 8 : 1);
            CHECK(ids[i]);
        }
        double times[60], total = 0;
        float peak = 0;
        for (unsigned tick = 0; tick < 100; ++tick) {
            clock_t start = clock();
            CHECK(af_physics_step(p, s, 1.f / 60));
            double ms = 1000.0 * (double)(clock() - start) / CLOCKS_PER_SEC;
            if (tick >= 40) {
                times[tick - 40] = ms;
                total += ms;
                af_physics_contact contacts[1024];
                size_t n = af_physics_contacts(p, contacts, 1024);
                CHECK(n <= 1024);
                for (size_t j = 0; j < n; ++j)
                    peak = fmaxf(peak, contacts[j].penetration);
            }
        }
        for (unsigned i = 0; i < counts[test]; ++i) {
            af_physics_state b = af_physics_get_state(p, ids[i]);
            CHECK(isfinite(b.position.x) && isfinite(b.position.y) && b.position.y > .35f &&
                  af_vec3_length(b.velocity) < 5);
        }
        qsort(times, 60, sizeof(double), compare);
        af_physics_stats stats = af_physics_get_stats(p);
        printf("%u,60,%.4f,%.4f,%.5f,%zu,%zu\n", counts[test], total / 60, times[56], peak,
               stats.contacts, stats.broadphase_pairs);
        af_physics_destroy(p);
        af_scene_destroy(s);
    }
    return 0;
}
