/* SPDX-License-Identifier: MPL-2.0 */
#ifndef AF_ENVIRONMENT_JOB_H
#define AF_ENVIRONMENT_JOB_H
#include <afterlight/afterlight.h>
#include "ibl.h"
typedef struct af_environment_job af_environment_job;
af_environment_job *af_environment_job_create(const char *name,const char *path);
void af_environment_job_destroy(af_environment_job *job);
void af_environment_job_cancel(af_environment_job *job);
af_environment_job_status af_environment_job_status_get(const af_environment_job *job);
/* Main thread only; acquire-published CPU output is immutable after UPLOADING. */
bool af_environment_job_result(af_environment_job *job,const char **name,const af_hdr_file **source,const af_ibl_images **images);
void af_environment_job_finish(af_environment_job *job,bool success,double upload_ms);
#endif
