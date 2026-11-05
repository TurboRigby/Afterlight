/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <afterlight/asset_watch.h>
#include <afterlight/model.h>
#include <SDL3/SDL_filesystem.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct source {char path[AF_ASSET_PATH_CAPACITY];char (*deps)[AF_ASSET_PATH_CAPACITY];size_t count;uint64_t loaded,pending;double stable;unsigned seen;} source;
struct af_asset_watch {source *sources;size_t count;double elapsed;unsigned generation;af_asset_watch_stats stats;};
static uint64_t fingerprint(const source *s){uint64_t hash=UINT64_C(14695981039346656037);for(size_t i=0;i<=s->count;++i){SDL_PathInfo info={0};bool exists=SDL_GetPathInfo(i?s->deps[i-1]:s->path,&info);uint64_t values[]={exists?1:0,info.size,(uint64_t)info.modify_time};for(unsigned j=0;j<3;++j){hash^=values[j];hash*=UINT64_C(1099511628211);}}return hash;}
static void dependencies(source *s){af_model_file model;af_asset_error error;if(af_asset_is_model_path(s->path)&&af_model_file_load(s->path,&model,&error)){
    free(s->deps);s->deps=model.dependencies;s->count=model.dependency_count;model.dependencies=NULL;model.dependency_count=0;af_model_file_free(&model);}}
af_asset_watch *af_asset_watch_create(void){return calloc(1,sizeof(af_asset_watch));}
void af_asset_watch_destroy(af_asset_watch *watch){if(!watch)return;for(size_t i=0;i<watch->count;++i)free(watch->sources[i].deps);free(watch->sources);free(watch);}
void af_asset_watch_update(af_asset_watch *watch,af_engine *engine,const af_scene *scene,double delta,af_asset_error *message){
    if(message)*message=(af_asset_error){0};if(!watch||!engine||!scene||!isfinite(delta)||delta<0)return;watch->elapsed+=delta;if(watch->elapsed<.25)return;double elapsed=watch->elapsed;watch->elapsed=0;++watch->generation;
    for(size_t i=0;i<af_scene_asset_count(scene);++i){const af_scene_asset *asset=af_scene_asset_at(scene,i);char path[AF_ASSET_PATH_CAPACITY];size_t n=strcspn(asset->path,"#");memcpy(path,asset->path,n);path[n]=0;
        size_t at=0;while(at<watch->count&&strcmp(watch->sources[at].path,path))++at;
        if(at==watch->count){void *p=realloc(watch->sources,(at+1)*sizeof(*watch->sources));if(!p)continue;watch->sources=p;source *s=&watch->sources[watch->count++];*s=(source){0};snprintf(s->path,sizeof(s->path),"%s",path);dependencies(s);s->loaded=s->pending=fingerprint(s);}
        source *s=&watch->sources[at];if(s->seen==watch->generation)continue;s->seen=watch->generation;uint64_t value=fingerprint(s);
        if(value==s->loaded){s->stable=0;continue;}if(value!=s->pending){s->pending=value;s->stable=0;continue;}s->stable+=elapsed;if(s->stable<.35)continue;
        s->loaded=value;s->stable=0;af_asset_error error;
        if(af_engine_reload_asset_source(engine,scene,s->path,&error)){++watch->stats.reloads;dependencies(s);s->loaded=s->pending=fingerprint(s);if(message)snprintf(message->message,sizeof(message->message),"Asset reloaded: %.210s",s->path);}
        else{++watch->stats.rejections;if(message)snprintf(message->message,sizeof(message->message),"Reload kept last valid assets: %.210s",error.message);}
    }
    for(size_t i=watch->count;i>0;--i)if(watch->sources[i-1].seen!=watch->generation){free(watch->sources[i-1].deps);memmove(&watch->sources[i-1],&watch->sources[i],(watch->count-i)*sizeof(*watch->sources));--watch->count;}
    watch->stats.sources=watch->count;
}
af_asset_watch_stats af_asset_watch_get_stats(const af_asset_watch *watch){return watch?watch->stats:(af_asset_watch_stats){0};}
