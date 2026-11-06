/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <afterlight/project.h>
#include <afterlight/asset_watch.h>
#include "runtime.h"
#include "gui.h"
#include "play_hud.h"
#include "cube.h"
#include <SDL3/SDL_filesystem.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct player {af_project project;af_scene *source;ed_runtime runtime;ed_gui gui;af_asset_watch *watch;uint64_t frames;bool failed,hot_reload;} player;
static int check_project(const af_project *project){
    af_scene_error error;af_scene *scene=af_scene_load(project->startup_scene,&error);if(!scene){fprintf(stderr,"Scene: %s\n",error.message);return 1;}
    af_audio *audio=af_audio_create();bool ok=audio!=NULL;af_asset_error asset_error={0};
    for(size_t i=0;ok&&i<af_scene_asset_count(scene);++i){const af_scene_asset *a=af_scene_asset_at(scene,i);
        if(a->kind==AF_ASSET_MESH){af_mesh_file mesh;ok=af_mesh_file_load(a->path,&mesh,&asset_error);if(ok)af_mesh_file_free(&mesh);}
        else if(a->kind==AF_ASSET_ANIMATION){af_animation_file animation;ok=af_animation_file_load(a->path,&animation,&asset_error);if(ok)af_animation_file_free(&animation);}
        else if(a->kind==AF_ASSET_ENVIRONMENT){af_hdr_file image;ok=af_hdr_file_load(a->path,&image,&asset_error);if(ok)af_hdr_file_free(&image);}
        else if(a->kind==AF_ASSET_AUDIO)ok=af_audio_load(audio,a->name,a->path,&asset_error);
        else{af_image_file image;ok=af_image_file_load(a->path,&image,&asset_error);if(ok)af_image_file_free(&image);}
        if(!ok)fprintf(stderr,"Asset %s: %s\n",a->name,asset_error.message);
    }
    SDL_SharedObject *library=ok?SDL_LoadObject(project->game_module):NULL;
    if(ok&&!library){fprintf(stderr,"Module: %s\n",SDL_GetError());ok=false;}
    if(library){SDL_FunctionPointer symbol=SDL_LoadFunction(library,"af_game_get_api");af_game_get_api_fn get=NULL;memcpy(&get,&symbol,sizeof(get));const af_game_module *api=get?get(AF_GAME_ABI_VERSION):NULL;
        ok=api&&api->abi_version==AF_GAME_ABI_VERSION&&api->struct_size==sizeof(*api)&&api->host_size==sizeof(af_game_host)&&api->object_size==sizeof(af_scene_object)&&api->state_size<=AF_GAME_STATE_MAX_BYTES&&api->init&&api->update&&api->shutdown;
        if(!ok)fputs("Module ABI/callback validation failed. Rebuild with current headers.\n",stderr);SDL_UnloadObject(library);}
    af_audio_destroy(audio);af_scene_destroy(scene);if(ok)puts("Project scene, assets and C module ABI are valid.");return ok?0:1;
}
static bool init(af_engine *engine,void *memory){
    player *app=memory;af_scene_error scene_error;af_asset_error error={0};
    app->source=af_scene_load(app->project.startup_scene,&scene_error);
    if(!app->source){fprintf(stderr,"Player scene: %s\n",scene_error.message);return false;}
    af_mesh_data cube=cube_mesh_data();if(!af_engine_upload_mesh(engine,"cube",&cube)||!af_engine_load_scene_assets(engine,app->source,&error)){fprintf(stderr,"Player assets: %s\n",error.message);return false;}
    char font[AF_ASSET_PATH_CAPACITY];const char *base=af_engine_get_base_path(engine);
    if(!base||snprintf(font,sizeof(font),"%sfonts/FiraSans-Regular.ttf",base)>=(int)sizeof(font)||!ed_gui_init(&app->gui,engine,font))return false;
    if(!ed_runtime_start_file(&app->runtime,app->source,engine,app->project.game_module)){fprintf(stderr,"Player game: %s\n",app->runtime.status);return false;}
    if(app->hot_reload)app->watch=af_asset_watch_create();
    app->runtime.auto_reload=app->hot_reload;af_engine_set_scene(engine,app->runtime.scene);return true;
}
static void update(af_engine *engine,double delta,void *memory){
    player *app=memory;if(app->frames&&af_engine_get_stats(engine).rendered_frames>=app->frames){af_engine_request_quit(engine);return;}
    if(app->watch){af_asset_error error;af_asset_watch_update(app->watch,engine,app->runtime.scene,delta,&error);if(error.message[0])fprintf(stdout,"%s\n",error.message);}
    bool captured=af_engine_mouse_captured(engine);
    if(af_engine_key_pressed(engine,AF_KEY_F6)){app->runtime.paused=!app->runtime.paused;af_engine_set_mouse_capture(engine,false);}
    ed_runtime_frame_update(&app->runtime,delta,true);af_engine_set_scene(engine,app->runtime.scene);af_engine_set_simulation_paused(engine,app->runtime.paused);
    if(af_engine_key_pressed(engine,AF_KEY_ESCAPE)&&!captured)af_engine_request_quit(engine);
    int w,h;af_engine_get_window_size(engine,&w,&h);ed_gui_input(&app->gui,engine);bool character=false;
    for(size_t i=0;i<af_scene_object_count(app->runtime.scene);++i)character|=af_scene_object_at(app->runtime.scene,i)->gameplay.player.enabled;
    ed_play_hud(&app->gui.context,engine,(af_rect){0,0,(float)w,(float)h},app->runtime.paused,false,false,character);
    if(!ed_gui_render(&app->gui,engine,(float)w,(float)h)){app->failed=true;af_engine_request_quit(engine);}
}
static void fixed(af_engine *engine,double delta,void *memory){player *app=memory;ed_runtime_update(&app->runtime,delta,true);af_engine_set_scene(engine,app->runtime.scene);}
static void shutdown(af_engine *engine,void *memory){player *app=memory;af_engine_set_scene(engine,NULL);ed_runtime_stop(&app->runtime);af_asset_watch_destroy(app->watch);af_scene_destroy(app->source);ed_gui_destroy(&app->gui);af_engine_stats stats=af_engine_get_stats(engine);printf("Player: %" PRIu64 " frames; %u validation errors.\n",stats.rendered_frames,stats.validation_errors);}
int main(int argc,char **argv){
    player app={0};bool check=false;const char *project=NULL;af_engine_config config=af_engine_config_default();config.show_fps=true;
    for(int i=1;i<argc;++i){if(!strcmp(argv[i],"--project")&&i+1<argc)project=argv[++i];
        else if(!strcmp(argv[i],"--frames")&&i+1<argc){const char *value=argv[++i];char *end;errno=0;unsigned long long n=strtoull(value,&end,10);if(errno||value[0]<'0'||value[0]>'9'||*end||!n)return 1;app.frames=(uint64_t)n;}
        else if(!strcmp(argv[i],"--check"))check=true;else if(!strcmp(argv[i],"--hot-reload"))app.hot_reload=true;else if(!strcmp(argv[i],"--validation"))config.enable_validation=true;else if(!strcmp(argv[i],"--no-validation"))config.enable_validation=false;
        else if(!strcmp(argv[i],"--help")){puts("afterlight_player [--project PATH] [--frames N] [--hot-reload] [--validation | --no-validation]\nF6 pauses; Escape releases mouse, then exits. Without --project, loads game.afterlight.project beside app resources.");return 0;}else return 1;}
    char bundled[AF_ASSET_PATH_CAPACITY];if(!project){const char *base=SDL_GetBasePath();if(!base||snprintf(bundled,sizeof(bundled),"%sgame.afterlight.project",base)>=(int)sizeof(bundled))return 1;project=bundled;}
    af_asset_error error;if(!af_project_load(project,&app.project,&error)){fprintf(stderr,"Player project: %s\n",error.message);return 1;}config.title=app.project.name;
    if(check)return check_project(&app.project);
    af_engine *engine=af_engine_create(&config);if(!engine)return 1;const char *required=getenv("AFTERLIGHT_REQUIRE_VALIDATION");if(required&&!strcmp(required,"1")&&!af_engine_get_stats(engine).validation_enabled){af_engine_destroy(engine);return 1;}
    af_game game={.init=init,.update=update,.fixed_update=fixed,.shutdown=shutdown,.user_data=&app};int result=af_engine_run(engine,&game);af_engine_destroy(engine);return app.failed?1:result;
}
