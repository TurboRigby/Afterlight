/* SPDX-License-Identifier: MPL-2.0 */
#include "environment_job.h"
#include <SDL3/SDL_timer.h>
#include <stdio.h>
#include <string.h>
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            fprintf(stderr, "HDR job %d: %s\n", __LINE__, #c);                                                         \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
static af_environment_job_state wait(af_environment_job *job) {
    Uint64 deadline = SDL_GetTicks() + 10000;
    af_environment_job_state state;
    do {
        state = af_environment_job_status_get(job).state;
        if (state != AF_ENV_LOADING && state != AF_ENV_BAKING)
            return state;
        SDL_Delay(1);
    } while (SDL_GetTicks() < deadline);
    return AF_ENV_IDLE;
}
int main(int argc, char **argv) {
    CHECK(argc == 2);
    CHECK(!af_environment_job_create("", argv[1]));
    af_environment_job *job = af_environment_job_create("sky", argv[1]);
    CHECK(job && wait(job) == AF_ENV_UPLOADING);
    const char *name;
    const af_hdr_file *source;
    const af_ibl_images *images;
    CHECK(af_environment_job_result(job, &name, &source, &images));
    CHECK(!strcmp(name, "sky") && source->rgba && images->diffuse);
    af_environment_job_finish(job, true, 2);
    CHECK(af_environment_job_status_get(job).state == AF_ENV_COMPLETE);
    af_environment_job_destroy(job);
    job = af_environment_job_create("sky", argv[1]);
    CHECK(job);
    af_environment_job_cancel(job);
    CHECK(wait(job) == AF_ENV_CANCELED);
    CHECK(!af_environment_job_result(job, &name, &source, &images));
    af_environment_job_destroy(job);
    job = af_environment_job_create("sky", "no-such-environment.hdr");
    CHECK(job && wait(job) == AF_ENV_FAILED && af_environment_job_status_get(job).error[0]);
    af_environment_job_destroy(job);
    job = af_environment_job_create("sky", argv[1]);
    CHECK(job && wait(job) == AF_ENV_UPLOADING);
    af_environment_job_cancel(job);
    CHECK(!af_environment_job_result(job, &name, &source, &images) &&
          af_environment_job_status_get(job).state == AF_ENV_CANCELED);
    af_environment_job_destroy(job);
    job = af_environment_job_create("sky", argv[1]);
    CHECK(job);
    af_environment_job_destroy(job);
    puts("HDR jobs: background result, early/late cancellation, invalid input and active-worker shutdown passed.");
    return 0;
}
