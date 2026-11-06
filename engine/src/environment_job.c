/* SPDX-License-Identifier: MPL-2.0 */
#include "environment_job.h"
#include <SDL3/SDL_thread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

struct af_environment_job {
    SDL_Thread *thread;
    atomic_int state,progress;
    atomic_bool canceled;
    char name[AF_MESH_NAME_CAPACITY],path[AF_ASSET_PATH_CAPACITY];
    af_asset_error error;
    af_hdr_file source;
    af_ibl_images images;
    double upload_ms;
};
static void discard(af_environment_job *job){af_hdr_file_free(&job->source);af_ibl_free(&job->images);}
static bool progress(void *user,float fraction){
    af_environment_job *job=user;atomic_store(&job->progress,10+(int)(fraction*85));return !atomic_load(&job->canceled);
}
static int SDLCALL work(void *user){
    af_environment_job *job=user;
    bool ok=!atomic_load(&job->canceled)&&af_hdr_file_load(job->path,&job->source,&job->error);
    if(ok){atomic_store(&job->state,AF_ENV_BAKING);ok=af_ibl_bake_hdr_progress(job->source.rgba,job->source.width,job->source.height,&job->images,progress,job);}
    if(atomic_load(&job->canceled)){discard(job);atomic_store(&job->state,AF_ENV_CANCELED);}
    else if(!ok){discard(job);if(!job->error.message[0])snprintf(job->error.message,sizeof(job->error.message),"Environment baking failed.");atomic_store(&job->state,AF_ENV_FAILED);}
    else{atomic_store(&job->progress,95);atomic_store(&job->state,AF_ENV_UPLOADING);}
    return 0;
}
af_environment_job *af_environment_job_create(const char *name,const char *path){
    if(!name||!name[0]||strlen(name)>=AF_MESH_NAME_CAPACITY||!path||strlen(path)>=AF_ASSET_PATH_CAPACITY)return NULL;
    af_environment_job *job=calloc(1,sizeof(*job));if(!job)return NULL;
    snprintf(job->name,sizeof(job->name),"%s",name);snprintf(job->path,sizeof(job->path),"%s",path);
    atomic_init(&job->state,AF_ENV_LOADING);atomic_init(&job->progress,0);atomic_init(&job->canceled,false);
    job->thread=SDL_CreateThread(work,"Afterlight HDR",job);if(!job->thread){free(job);return NULL;}return job;
}
void af_environment_job_cancel(af_environment_job *job){if(job)atomic_store(&job->canceled,true);}
void af_environment_job_destroy(af_environment_job *job){if(job){af_environment_job_cancel(job);SDL_WaitThread(job->thread,NULL);discard(job);free(job);}}
af_environment_job_status af_environment_job_status_get(const af_environment_job *job){
    if(!job)return (af_environment_job_status){0};af_environment_job_status s={.state=(af_environment_job_state)atomic_load(&job->state),.progress=(float)atomic_load(&job->progress)/100};
    if(s.state==AF_ENV_FAILED)snprintf(s.error,sizeof(s.error),"%s",job->error.message);
    if(s.state==AF_ENV_COMPLETE||s.state==AF_ENV_FAILED)s.upload_ms=job->upload_ms;return s;
}
bool af_environment_job_result(af_environment_job *job,const char **name,const af_hdr_file **source,const af_ibl_images **images){
    if(!job||atomic_load(&job->state)!=AF_ENV_UPLOADING)return false;
    if(atomic_load(&job->canceled)){discard(job);atomic_store(&job->state,AF_ENV_CANCELED);return false;}
    *name=job->name;*source=&job->source;*images=&job->images;return true;
}
void af_environment_job_finish(af_environment_job *job,bool success,double upload_ms){
    discard(job);job->upload_ms=upload_ms;if(!success)snprintf(job->error.message,sizeof(job->error.message),"GPU environment publication failed; previous resource retained.");
    atomic_store(&job->progress,100);atomic_store(&job->state,success?AF_ENV_COMPLETE:AF_ENV_FAILED);
}
