/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "jobs.h"

#include <SDL3/SDL_cpuinfo.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_mutex.h>
#include <SDL3/SDL_thread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>

#define AF_JOBS_MAX_WORKERS 8u

struct af_jobs {
    _Atomic bool busy;
    SDL_Mutex *queue_mutex;
    SDL_Condition *wake_cond;
    SDL_Condition *done_cond;
    SDL_Thread **threads;
    unsigned worker_count;

    bool shutdown;
    uint64_t generation;
    unsigned done_count;

    af_job_range_fn fn;
    void *user_data;
    size_t count;
    size_t grain;
    _Atomic size_t next_index;
};

static _Thread_local bool tls_in_worker = false;

static void run_chunks(af_jobs *jobs, size_t count, size_t grain, af_job_range_fn fn, void *user_data) {
    for (;;) {
        size_t start = atomic_fetch_add_explicit(&jobs->next_index, grain, memory_order_relaxed);
        if (start >= count)
            break;
        size_t end = start + grain;
        if (end > count)
            end = count;
        fn(start, end, user_data);
    }
}

static int SDLCALL worker_main(void *arg) {
    af_jobs *jobs = arg;
    tls_in_worker = true;
    uint64_t seen = 0;

    SDL_LockMutex(jobs->queue_mutex);
    for (;;) {
        while (!jobs->shutdown && jobs->generation == seen)
            SDL_WaitCondition(jobs->wake_cond, jobs->queue_mutex);
        if (jobs->shutdown)
            break;

        seen = jobs->generation;
        af_job_range_fn fn = jobs->fn;
        void *user_data = jobs->user_data;
        size_t count = jobs->count;
        size_t grain = jobs->grain;
        SDL_UnlockMutex(jobs->queue_mutex);

        run_chunks(jobs, count, grain, fn, user_data);

        SDL_LockMutex(jobs->queue_mutex);
        if (++jobs->done_count >= jobs->worker_count)
            SDL_SignalCondition(jobs->done_cond);
    }
    SDL_UnlockMutex(jobs->queue_mutex);
    tls_in_worker = false;
    return 0;
}

static unsigned resolve_worker_count(unsigned requested) {
    const char *env = getenv("AFTERLIGHT_JOB_THREADS");
    if (env && env[0]) {
        char *end = NULL;
        long parsed = strtol(env, &end, 10);
        if (end && *end == '\0' && parsed >= 0) {
            if (parsed <= 1)
                return 0;
            unsigned count = (unsigned)(parsed - 1);
            return count > AF_JOBS_MAX_WORKERS ? AF_JOBS_MAX_WORKERS : count;
        }
    }
    if (requested > 0)
        return requested > AF_JOBS_MAX_WORKERS ? AF_JOBS_MAX_WORKERS : requested;
    int cores = SDL_GetNumLogicalCPUCores();
    if (cores <= 1)
        return 0;
    unsigned auto_workers = (unsigned)(cores - 1);
    return auto_workers > AF_JOBS_MAX_WORKERS ? AF_JOBS_MAX_WORKERS : auto_workers;
}

af_jobs *af_jobs_create(unsigned worker_count) {
    af_jobs *jobs = calloc(1, sizeof(*jobs));
    if (!jobs)
        return NULL;

    atomic_init(&jobs->busy, false);
    atomic_init(&jobs->next_index, 0);
    unsigned target = resolve_worker_count(worker_count);
    if (target == 0)
        return jobs;

    jobs->queue_mutex = SDL_CreateMutex();
    jobs->wake_cond = SDL_CreateCondition();
    jobs->done_cond = SDL_CreateCondition();
    jobs->threads = calloc(target, sizeof(*jobs->threads));
    if (!jobs->queue_mutex || !jobs->wake_cond || !jobs->done_cond || !jobs->threads) {
        af_jobs_destroy(jobs);
        return NULL;
    }

    SDL_LockMutex(jobs->queue_mutex);
    for (unsigned i = 0; i < target; ++i) {
        char name[32];
        SDL_snprintf(name, sizeof(name), "af_job_%u", i);
        jobs->threads[i] = SDL_CreateThread(worker_main, name, jobs);
        if (!jobs->threads[i])
            break;
        ++jobs->worker_count;
    }
    SDL_UnlockMutex(jobs->queue_mutex);
    return jobs;
}

void af_jobs_destroy(af_jobs *jobs) {
    if (!jobs)
        return;
    if (jobs->worker_count > 0) {
        SDL_LockMutex(jobs->queue_mutex);
        jobs->shutdown = true;
        SDL_BroadcastCondition(jobs->wake_cond);
        SDL_UnlockMutex(jobs->queue_mutex);
        for (unsigned i = 0; i < jobs->worker_count; ++i) {
            if (jobs->threads[i])
                SDL_WaitThread(jobs->threads[i], NULL);
        }
    }
    free(jobs->threads);
    if (jobs->done_cond)
        SDL_DestroyCondition(jobs->done_cond);
    if (jobs->wake_cond)
        SDL_DestroyCondition(jobs->wake_cond);
    if (jobs->queue_mutex)
        SDL_DestroyMutex(jobs->queue_mutex);
    free(jobs);
}

unsigned af_jobs_worker_count(const af_jobs *jobs) {
    return jobs ? jobs->worker_count : 0;
}

void af_jobs_parallel_for(af_jobs *jobs, size_t count, size_t grain, af_job_range_fn fn, void *user_data) {
    if (!fn || count == 0)
        return;
    if (grain == 0)
        grain = 1;
    if (!jobs || jobs->worker_count == 0 || count <= grain || tls_in_worker) {
        fn(0, count, user_data);
        return;
    }
    if (atomic_exchange_explicit(&jobs->busy, true, memory_order_acquire)) {
        fn(0, count, user_data);
        return;
    }

    SDL_LockMutex(jobs->queue_mutex);
    jobs->fn = fn;
    jobs->user_data = user_data;
    jobs->count = count;
    jobs->grain = grain;
    atomic_store_explicit(&jobs->next_index, 0, memory_order_release);
    jobs->done_count = 0;
    ++jobs->generation;
    SDL_BroadcastCondition(jobs->wake_cond);
    SDL_UnlockMutex(jobs->queue_mutex);

    tls_in_worker = true;
    run_chunks(jobs, count, grain, fn, user_data);
    tls_in_worker = false;

    SDL_LockMutex(jobs->queue_mutex);
    while (jobs->done_count < jobs->worker_count)
        SDL_WaitCondition(jobs->done_cond, jobs->queue_mutex);
    SDL_UnlockMutex(jobs->queue_mutex);

    atomic_store_explicit(&jobs->busy, false, memory_order_release);
}

static SDL_InitState g_jobs_init = {0};
static af_jobs *g_jobs_pool = NULL;

void af_jobs_global_shutdown(void) {
    if (g_jobs_pool) {
        af_jobs *pool = g_jobs_pool;
        g_jobs_pool = NULL;
        af_jobs_destroy(pool);
    }
    g_jobs_init = (SDL_InitState){0};
}

af_jobs *af_jobs_global(void) {
    if (SDL_ShouldInit(&g_jobs_init)) {
        g_jobs_pool = af_jobs_create(0);
        if (g_jobs_pool)
            atexit(af_jobs_global_shutdown);
        SDL_SetInitialized(&g_jobs_init, true);
    }
    return g_jobs_pool;
}
