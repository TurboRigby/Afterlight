/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef AF_JOBS_H
#define AF_JOBS_H

#include <stdbool.h>
#include <stddef.h>

typedef struct af_jobs af_jobs;

/* Range callback invoked for disjoint sub-ranges [start, end). */
typedef void (*af_job_range_fn)(size_t start, size_t end, void *user_data);

/* Creates a worker thread pool. Pass worker_count == 0 to auto-select based on CPU cores.
 * Honors AFTERLIGHT_JOB_THREADS environment variable (e.g. 0 or 1 for single-threaded mode). */
af_jobs *af_jobs_create(unsigned worker_count);
void af_jobs_destroy(af_jobs *jobs);
unsigned af_jobs_worker_count(const af_jobs *jobs);

/* Lazily initialized process-wide shared pool, automatically cleaned up on process exit. */
af_jobs *af_jobs_global(void);
void af_jobs_global_shutdown(void);

/* Executes fn over [0, count) in chunks of size >= grain across worker threads and the calling thread.
 * Always deadlock-free: nested calls from inside a worker, tiny workloads (count <= grain), or
 * concurrent contention fall back smoothly to inline execution on the calling thread. */
void af_jobs_parallel_for(af_jobs *jobs, size_t count, size_t grain, af_job_range_fn fn, void *user_data);

#endif /* AF_JOBS_H */
