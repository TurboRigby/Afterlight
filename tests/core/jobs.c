/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "core/jobs.h"
#include <afterlight/physics.h>
#include <afterlight/scene.h>

#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            fprintf(stderr, "CHECK failed %s:%d: %s\n", __FILE__, __LINE__, #c);                                       \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)

typedef struct fill_context {
    _Atomic uint32_t *hits;
    uint32_t *values;
    af_jobs *pool;
    bool test_nested;
} fill_context;

static void nested_leaf(size_t start, size_t end, void *user_data) {
    uint32_t *acc = user_data;
    for (size_t i = start; i < end; ++i)
        *acc += (uint32_t)(i + 1u);
}

static void fill_range(size_t start, size_t end, void *user_data) {
    fill_context *ctx = user_data;
    for (size_t i = start; i < end; ++i) {
        atomic_fetch_add_explicit(&ctx->hits[i], 1u, memory_order_relaxed);
        uint32_t val = (uint32_t)(i * 2654435761u);
        if (ctx->test_nested && (i % 128u) == 0u) {
            uint32_t sum = 0;
            af_jobs_parallel_for(ctx->pool, 16u, 4u, nested_leaf, &sum);
            val ^= sum;
        }
        ctx->values[i] = val;
    }
}

static int test_job_pool_basic(void) {
    af_jobs *pool = af_jobs_create(4u);
    CHECK(pool != NULL);
    CHECK(af_jobs_worker_count(pool) == 4u);

    const size_t n = 4099u;
    _Atomic uint32_t *hits = calloc(n, sizeof(*hits));
    uint32_t *values = calloc(n, sizeof(*values));
    CHECK(hits && values);

    /* Zero count and tiny count (< grain) */
    fill_context ctx = {.hits = hits, .values = values, .pool = pool, .test_nested = true};
    af_jobs_parallel_for(pool, 0u, 32u, fill_range, &ctx);
    af_jobs_parallel_for(pool, 7u, 32u, fill_range, &ctx);
    for (size_t i = 0; i < 7u; ++i)
        CHECK(atomic_load(&hits[i]) == 1u);

    /* 40 consecutive parallel rounds including nested parallel_for calls */
    for (unsigned round = 0; round < 40u; ++round) {
        for (size_t i = 0; i < n; ++i)
            atomic_store_explicit(&hits[i], 0u, memory_order_relaxed);
        af_jobs_parallel_for(pool, n, 37u, fill_range, &ctx);
        for (size_t i = 0; i < n; ++i) {
            CHECK(atomic_load_explicit(&hits[i], memory_order_relaxed) == 1u);
            uint32_t expected = (uint32_t)(i * 2654435761u);
            if ((i % 128u) == 0u)
                expected ^= 136u; /* sum(1..16) == 136 */
            CHECK(values[i] == expected);
        }
    }

    free(hits);
    free(values);
    af_jobs_destroy(pool);
    return 0;
}

static int simulate_multi_island(af_physics_state out_states[64], size_t *out_awake_islands) {
    af_scene *scene = af_scene_create();
    af_physics *physics = af_physics_create();
    CHECK(scene && physics);

    af_object_id floor = af_scene_add_object(scene, "Floor", "cube");
    CHECK(floor != AF_OBJECT_NONE);
    af_transform ft = af_transform_identity();
    ft.position = (af_vec3){0, -0.5f, 0};
    af_scene_set_object_transform(scene, floor, ft);
    af_collider fc = af_collider_default();
    fc.enabled = true;
    fc.type = AF_COLLIDER_BOX;
    fc.half_extents = (af_vec3){150.0f, 0.5f, 150.0f};
    CHECK(af_scene_set_object_collider(scene, floor, fc));

    af_object_id ids[64];
    for (unsigned i = 0; i < 64u; ++i) {
        char name[32];
        snprintf(name, sizeof(name), "Island_%u", i);
        ids[i] = af_scene_add_object(scene, name, "cube");
        CHECK(ids[i] != AF_OBJECT_NONE);
        af_transform t = af_transform_identity();
        t.position = (af_vec3){(float)(i % 8u) * 4.0f, 0.52f + (float)(i % 3u) * 0.05f, (float)(i / 8u) * 4.0f};
        t.rotation = (af_vec3){0.1f * (float)(i % 5u), 0.2f * (float)(i % 4u), 0.05f * (float)(i % 3u)};
        CHECK(af_scene_set_object_transform(scene, ids[i], t));
        af_collider col = af_collider_default();
        col.enabled = true;
        col.type = AF_COLLIDER_BOX;
        col.half_extents = (af_vec3){0.5f, 0.5f, 0.5f};
        CHECK(af_scene_set_object_collider(scene, ids[i], col));
        af_rigid_body rb = af_rigid_body_default();
        rb.enabled = true;
        rb.type = AF_BODY_DYNAMIC;
        rb.mass = 1.0f + 0.1f * (float)(i % 7u);
        rb.restitution = 0.2f;
        CHECK(af_scene_set_body(scene, ids[i], rb));
    }

    size_t peak_islands = 0;
    for (unsigned step = 0; step < 30u; ++step) {
        CHECK(af_physics_step(physics, scene, 1.0f / 60.0f));
        af_physics_stats st = af_physics_get_stats(physics);
        if (st.awake_islands > peak_islands)
            peak_islands = st.awake_islands;
    }
    *out_awake_islands = peak_islands;
    for (unsigned i = 0; i < 64u; ++i)
        out_states[i] = af_physics_get_state(physics, ids[i]);

    af_physics_destroy(physics);
    af_scene_destroy(scene);
    return 0;
}

static int test_parallel_physics_determinism(void) {
    af_physics_state mt_states[64], st_states[64];
    size_t mt_islands = 0, st_islands = 0;

    /* Run multi-threaded */
    af_jobs_global_shutdown();
    CHECK(simulate_multi_island(mt_states, &mt_islands) == 0);
    CHECK(mt_islands >= 64u);

    /* Run single-threaded (AFTERLIGHT_JOB_THREADS=1) */
    af_jobs_global_shutdown();
    CHECK(setenv("AFTERLIGHT_JOB_THREADS", "1", 1) == 0);
    CHECK(simulate_multi_island(st_states, &st_islands) == 0);
    CHECK(st_islands == mt_islands);
    unsetenv("AFTERLIGHT_JOB_THREADS");
    af_jobs_global_shutdown();

    /* Assert bit-exact equality across all 64 dynamic islands! */
    for (unsigned i = 0; i < 64u; ++i) {
        CHECK(memcmp(&mt_states[i], &st_states[i], sizeof(af_physics_state)) == 0);
    }
    return 0;
}

int main(void) {
    if (test_job_pool_basic() != 0)
        return 1;
    if (test_parallel_physics_determinism() != 0)
        return 1;
    puts("Job pool (af_jobs): parallel_for, nested safety, and bit-exact multi-island physics determinism passed.");
    return 0;
}
