/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "runtime.h"
#include <math.h>
#include <inttypes.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_timer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static bool request_scene(const af_game_host *host,const char *name){return host&&host->runtime_context&&ed_runtime_request_scene(host->runtime_context,name);}
bool ed_runtime_set_project(ed_runtime *r,const char *project_path,const char *startup){if(!r||!project_path||!startup)return false;char scene[4096],project[4096];if(!af_asset_resolve_path(NULL,startup,scene,sizeof(scene))||!af_asset_resolve_path(NULL,project_path,project,sizeof(project)))return false;char *s=strrchr(scene,'/'),*p=strrchr(project,'/');if(!s||!p)return false;s[1]=0;p[1]=0;snprintf(r->scene_directory,sizeof(r->scene_directory),"%s",scene);snprintf(r->project_directory,sizeof(r->project_directory),"%s",project);r->host.project_directory=r->project_directory;return true;}
bool ed_runtime_request_scene(ed_runtime *r,const char *name){if(!r||!r->playing||!r->scene_directory[0]||!name||!name[0]||strlen(name)>=64||r->pending_scene[0])return false;for(const char *p=name;*p;++p)if(!((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||*p=='_'||*p=='-'))return false;snprintf(r->pending_scene,sizeof(r->pending_scene),"%s",name);return true;}
static bool compatible(const af_game_module *api){
    return api&&api->abi_version==AF_GAME_ABI_VERSION&&api->struct_size==sizeof(*api)&&api->host_size==sizeof(af_game_host)&&api->object_size==sizeof(af_scene_object)&&
        (!api->save_state==!api->load_state)&&api->name&&api->state_size<=AF_GAME_STATE_MAX_BYTES&&api->init&&api->update&&api->shutdown;
}
bool ed_runtime_start(ed_runtime *r,const af_scene *source,af_engine *engine,const af_game_module *api){
    if(r->playing)return false;
    if(!compatible(api)){snprintf(r->status,sizeof(r->status),"Incompatible C game ABI or invalid callbacks.");return false;}
    if(!af_physics_validate_scene(source,r->status,sizeof(r->status)))return false;
    af_scene *scene=af_scene_clone(source);void *state=calloc(1,api->state_size?api->state_size:1);
    if(!scene||!state){af_scene_destroy(scene);free(state);snprintf(r->status,sizeof(r->status),"Not enough memory to enter Play.");return false;}
    af_game_host host=af_game_host_create(scene,engine);host.runtime_context=r;host.project_directory=r->project_directory;host.request_scene=request_scene;
    const af_scene *previous_scene=af_engine_get_scene(engine);af_engine_set_scene(engine,scene);
    af_camera previous=af_engine_get_camera(engine),camera;af_gameplay_status previous_status=af_engine_gameplay_status(engine);if(af_scene_game_camera(scene,&camera))af_engine_set_camera(engine,&camera);
    bool captured=af_engine_mouse_captured(engine);
    if(!api->init(state,&host)){api->shutdown(state,&host);af_engine_set_scene(engine,previous_scene);af_engine_set_camera(engine,&previous);af_engine_set_gameplay_status(engine,previous_status);af_engine_set_mouse_capture(engine,captured);af_scene_destroy(scene);free(state);snprintf(r->status,sizeof(r->status),"C game initialization failed.");return false;}
    r->scene=scene;r->state=state;r->api=api;r->host=host;r->playing=true;r->paused=false;
    snprintf(r->status,sizeof(r->status),"Playing %s. Stop restores the editor scene.",api->name);return true;
}
void ed_runtime_frame_update(ed_runtime *r,double delta,bool input_enabled){
    if(!r->playing||!isfinite(delta)||delta<0)return;
    if(r->auto_reload)ed_runtime_poll_reload(r,delta);
    r->host.input_enabled=input_enabled&&!r->paused;r->host.camera_enabled=input_enabled;
    if(r->api->frame_update)r->api->frame_update(r->state,&r->host,r->paused?0:fmin(delta,0.1));
    if(!r->paused&&r->pending_scene[0])ed_runtime_transition(r);
}
void ed_runtime_update(ed_runtime *r,double delta,bool input_enabled){
    if(!r->playing||r->paused||!isfinite(delta)||delta<0)return;
    r->host.input_enabled=input_enabled;r->api->update(r->state,&r->host,fmin(delta,0.1));if(r->pending_scene[0])ed_runtime_transition(r);
}
void ed_runtime_step(ed_runtime *r,double delta){
    if(!r->playing)return;bool paused=r->paused;r->paused=false;ed_runtime_update(r,delta,false);r->paused=paused;
}
void ed_runtime_stop(ed_runtime *r){
    r->pending_scene[0]=0;
    if(r->playing)r->api->shutdown(r->state,&r->host);
    if(af_engine_get_scene(r->host.engine)==r->scene)af_engine_set_scene(r->host.engine,NULL);
    af_engine_set_gameplay_status(r->host.engine,(af_gameplay_status){0});
    af_engine_stop_sounds(r->host.engine);
    free(r->state);af_scene_destroy(r->scene);
    if(r->library)SDL_UnloadObject(r->library);r->library=NULL;
    if(r->shadow_path[0])remove(r->shadow_path);r->shadow_path[0]=0;r->module_path[0]=0;r->state=NULL;r->scene=NULL;r->api=NULL;r->playing=false;r->paused=false;
    snprintf(r->status,sizeof(r->status),"Stopped. Editor scene restored.");
}

static bool copy_module(const char *source,char *shadow,size_t capacity,char *error,size_t error_capacity){
    SDL_PathInfo info;if(!SDL_GetPathInfo(source,&info)||info.type!=SDL_PATHTYPE_FILE||!info.size||info.size>AF_ASSET_FILE_MAX_BYTES){snprintf(error,error_capacity,"Missing/oversized C game module.");return false;}
    char *directory=SDL_GetPrefPath("Afterlight","Editor");
    if(!directory){snprintf(error,error_capacity,"Cannot locate module cache.");return false;}
    static uint64_t sequence;FILE *output=NULL;
    for(unsigned attempt=0;attempt<32;++attempt){
        int n=snprintf(shadow,capacity,"%safterlight-game-%" PRIu64 "-%" PRIu64 ".module",directory,SDL_GetTicksNS(),++sequence);
        if(n<0||(size_t)n>=capacity)break;output=fopen(shadow,"wbx");if(output)break;
    }SDL_free(directory);
    if(!output){snprintf(error,error_capacity,"Cannot create a unique module copy.");shadow[0]=0;return false;}
    FILE *input=fopen(source,"rb");bool success=input!=NULL;unsigned char buffer[65536];uint64_t total=0;
    while(success){size_t count=fread(buffer,1,sizeof(buffer),input);total+=count;
        if(total>AF_ASSET_FILE_MAX_BYTES||fwrite(buffer,1,count,output)!=count){success=false;break;}
        if(count<sizeof(buffer)){success=!ferror(input);break;}
    }
    if(input&&fclose(input))success=false;if(fclose(output))success=false;
    SDL_PathInfo after;if(!SDL_GetPathInfo(source,&after)||after.size!=info.size||after.modify_time!=info.modify_time||total!=info.size)success=false;
    if(!success){remove(shadow);shadow[0]=0;snprintf(error,error_capacity,"C game changed while copying; rebuild and retry.");}
    return success;
}
static bool load_module(const char *path,SDL_SharedObject **library,char *shadow,size_t capacity,const af_game_module **api,char *error,size_t error_capacity){
    *library=NULL;*api=NULL;
    if(!copy_module(path,shadow,capacity,error,error_capacity))return false;
    *library=SDL_LoadObject(shadow);
    if(!*library){snprintf(error,error_capacity,"Cannot load C game: %.210s",SDL_GetError());goto fail;}
    SDL_FunctionPointer symbol=SDL_LoadFunction(*library,"af_game_get_api");af_game_get_api_fn get_api=NULL;
    _Static_assert(sizeof(symbol)==sizeof(get_api),"Function pointer representation");memcpy(&get_api,&symbol,sizeof(get_api));
    if(!get_api){snprintf(error,error_capacity,"C game does not export af_game_get_api.");goto fail;}
    *api=get_api(AF_GAME_ABI_VERSION);
    if(!compatible(*api)){snprintf(error,error_capacity,"C game ABI mismatch. Rebuild with current Afterlight headers.");goto fail;}
    return true;
fail:
    if(*library)SDL_UnloadObject(*library);*library=NULL;*api=NULL;remove(shadow);shadow[0]=0;return false;
}
bool ed_runtime_start_file(ed_runtime *r,const af_scene *source,af_engine *engine,const char *path){
    if(r->playing)return false;char absolute[AF_ASSET_PATH_CAPACITY];
    if(!af_asset_resolve_path(NULL,path,absolute,sizeof(absolute))){snprintf(r->status,sizeof(r->status),"Invalid C game path.");return false;}
    SDL_SharedObject *library;char shadow[AF_ASSET_PATH_CAPACITY];const af_game_module *api;
    if(!load_module(absolute,&library,shadow,sizeof(shadow),&api,r->status,sizeof(r->status)))return false;
    if(!ed_runtime_start(r,source,engine,api)){SDL_UnloadObject(library);remove(shadow);return false;}
    r->library=library;snprintf(r->shadow_path,sizeof(r->shadow_path),"%s",shadow);snprintf(r->module_path,sizeof(r->module_path),"%s",absolute);
    SDL_PathInfo info;if(SDL_GetPathInfo(absolute,&info)){r->loaded_time=r->pending_time=info.modify_time;r->loaded_size=r->pending_size=info.size;}
    r->auto_reload=true;r->stable_time=0;r->reload_count=0;return true;
}
bool ed_runtime_reload(ed_runtime *r){
    if(!r->playing||!r->module_path[0])return false;af_engine *engine=r->host.engine;
    bool captured=af_engine_mouse_captured(engine),editor_camera=!r->host.camera_enabled;af_camera previous_camera=af_engine_get_camera(engine);af_gameplay_status previous_status=af_engine_gameplay_status(engine);
    size_t size=0;void *blob=NULL;uint32_t version=r->api->state_version;
    if(r->api->save_state){size=r->api->save_state(r->state,NULL,0);if(!size||size>1024u*1024u||(blob=malloc(size))==NULL||r->api->save_state(r->state,blob,size)!=size){free(blob);snprintf(r->status,sizeof(r->status),"Reload rejected: state serialization failed or exceeds 1 MiB.");return false;}}
    ed_runtime candidate={0};snprintf(candidate.scene_directory,sizeof(candidate.scene_directory),"%s",r->scene_directory);snprintf(candidate.project_directory,sizeof(candidate.project_directory),"%s",r->project_directory);bool ready=ed_runtime_start_file(&candidate,r->scene,engine,r->module_path);
    if(ready&&blob&&(!candidate.api->load_state||!candidate.api->load_state(candidate.state,&candidate.host,version,blob,size))){ed_runtime_stop(&candidate);ready=false;snprintf(candidate.status,sizeof(candidate.status),"Incompatible saved module state.");}
    free(blob);if(!ready){af_engine_set_scene(engine,r->scene);af_engine_set_camera(engine,&previous_camera);af_engine_set_gameplay_status(engine,previous_status);af_engine_set_mouse_capture(engine,captured);snprintf(r->status,sizeof(r->status),"Reload rejected: %.220s",candidate.status);return false;}
    af_camera next_camera=af_engine_get_camera(engine);af_gameplay_status next_status=af_engine_gameplay_status(engine);
    candidate.paused=r->paused;candidate.auto_reload=r->auto_reload;candidate.reload_count=r->reload_count+1;
    bool restored=size!=0;ed_runtime_stop(r);*r=candidate;r->host.runtime_context=r;r->host.project_directory=r->project_directory;af_engine_set_scene(engine,r->scene);af_engine_set_gameplay_status(engine,next_status);af_engine_set_camera(engine,editor_camera?&previous_camera:&next_camera);af_engine_set_mouse_capture(engine,captured);r->host.camera_enabled=!editor_camera;
    snprintf(r->status,sizeof(r->status),"C game reloaded (#%" PRIu64 "). %s",r->reload_count,restored?"Module state restored.":"Module state reinitialized.");return true;
}
void ed_runtime_poll_reload(ed_runtime *r,double delta){
    if(!r->playing||!r->module_path[0])return;
    SDL_PathInfo info;if(!SDL_GetPathInfo(r->module_path,&info)||info.type!=SDL_PATHTYPE_FILE)return;
    if(info.modify_time==r->loaded_time&&info.size==r->loaded_size){r->stable_time=0;return;}
    if(info.modify_time!=r->pending_time||info.size!=r->pending_size){r->pending_time=info.modify_time;r->pending_size=info.size;r->stable_time=0;return;}
    r->stable_time+=delta;if(r->stable_time<0.3)return;
    r->loaded_time=r->pending_time;r->loaded_size=r->pending_size;r->stable_time=0;
    (void)ed_runtime_reload(r);
}

bool ed_runtime_transition(ed_runtime *r){
    if(!r||!r->playing||!r->pending_scene[0])return false;char path[4096];if(snprintf(path,sizeof(path),"%s%s.afterlight.json",r->scene_directory,r->pending_scene)>=(int)sizeof(path)){r->pending_scene[0]=0;return false;}r->pending_scene[0]=0;
    af_scene_error error;af_scene *source=af_scene_load(path,&error);if(!source){snprintf(r->status,sizeof(r->status),"Scene change rejected: %.220s",error.message);return false;}
    af_engine *engine=r->host.engine;af_camera camera=af_engine_get_camera(engine);af_gameplay_status status=af_engine_gameplay_status(engine);bool captured=af_engine_mouse_captured(engine);af_asset_error asset_error;ed_runtime candidate={0};snprintf(candidate.scene_directory,sizeof(candidate.scene_directory),"%s",r->scene_directory);snprintf(candidate.project_directory,sizeof(candidate.project_directory),"%s",r->project_directory);
    bool ok=(!engine||af_engine_load_scene_assets(engine,source,&asset_error))&&(r->module_path[0]?ed_runtime_start_file(&candidate,source,engine,r->module_path):ed_runtime_start(&candidate,source,engine,r->api));af_scene_destroy(source);
    if(!ok){if(engine)(void)af_engine_load_scene_assets(engine,r->scene,&asset_error);af_engine_set_scene(engine,r->scene);af_engine_set_camera(engine,&camera);af_engine_set_gameplay_status(engine,status);af_engine_set_mouse_capture(engine,captured);snprintf(r->status,sizeof(r->status),"Scene change rejected; previous scene retained.");return false;}
    candidate.auto_reload=r->auto_reload;candidate.transition_count=r->transition_count+1;candidate.reload_count=r->reload_count;af_camera next_camera=af_engine_get_camera(engine);af_gameplay_status next_status=af_engine_gameplay_status(engine);
    ed_runtime_stop(r);*r=candidate;r->host.runtime_context=r;r->host.project_directory=r->project_directory;af_engine_set_scene(engine,r->scene);af_engine_set_camera(engine,&next_camera);af_engine_set_gameplay_status(engine,next_status);af_engine_reset_interpolation(engine);snprintf(r->status,sizeof(r->status),"Scene changed (#%" PRIu64 ").",r->transition_count);return true;
}
