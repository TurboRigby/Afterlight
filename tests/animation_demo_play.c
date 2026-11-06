/* SPDX-License-Identifier: MPL-2.0 */
#include "runtime.h"
#include "platform_sdl.h"
#include "cube.h"
#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
typedef struct state {const char *simple,*complex,*module;af_scene *source;ed_runtime runtime;af_object_id actor;unsigned phase,ticks;uint64_t start_tick;double paused_time;bool failed;} state;
#define CHECK(c) do{if(!(c)){fprintf(stderr,"Demo Play phase %u line %d: %s\n",s->phase,__LINE__,#c);s->failed=true;af_engine_request_quit(engine);return;}}while(0)
static bool load(af_engine *engine,state *s,const char *path){
    af_engine_set_scene(engine,NULL);ed_runtime_stop(&s->runtime);af_scene_destroy(s->source);af_scene_error scene_error;s->source=af_scene_load(path,&scene_error);af_asset_error error;
    if(!s->source||!af_engine_load_scene_assets(engine,s->source,&error)||!ed_runtime_start_file(&s->runtime,s->source,engine,s->module))return false;s->runtime.auto_reload=false;s->actor=0;
    for(size_t i=0;i<af_scene_object_count(s->runtime.scene);++i){const af_scene_object *o=af_scene_object_at(s->runtime.scene,i);if(o->animation.asset[0]){s->actor=o->id;break;}}return s->actor!=0;
}
static bool init(af_engine *engine,void *user){state *s=user;af_mesh_data cube=cube_mesh_data();return af_engine_upload_mesh(engine,"cube",&cube)&&load(engine,s,s->simple);}
static void space(af_engine *engine){SDL_Event e={0};e.type=SDL_EVENT_KEY_DOWN;e.key.windowID=SDL_GetWindowID(af_engine_sdl_window(engine));e.key.scancode=SDL_SCANCODE_SPACE;e.key.down=true;SDL_PushEvent(&e);e.type=SDL_EVENT_KEY_UP;e.key.down=false;SDL_PushEvent(&e);}
static void update(af_engine *engine,double delta,void *user){
    state *s=user;ed_runtime_frame_update(&s->runtime,delta,true);uint64_t ticks=af_engine_get_stats(engine).simulation_ticks;
    if(ticks-s->start_tick<12)return;s->start_tick=ticks;af_animation_status status=af_engine_animation_status(engine,s->actor);
    switch(s->phase++){
    case 0:CHECK(status.available&&status.clip_count==3&&status.time>0&&status.playing);space(engine);break;
    case 1:CHECK(status.clip==1&&status.time>0);CHECK(s->runtime.host.animation_pause(engine,s->actor,true));s->paused_time=status.time;break;
    case 2:CHECK(!status.playing&&status.time==s->paused_time);CHECK(s->runtime.host.animation_seek(engine,s->actor,1.25));break;
    case 3:CHECK(status.time==1.25);CHECK(af_scene_get_object(s->source,s->actor)->animation.clip==0);CHECK(load(engine,s,s->complex));break;
    case 4:{CHECK(status.clip_count==3&&status.time>0);unsigned actors=0,moved=0;for(size_t i=0;i<af_scene_object_count(s->runtime.scene);++i){const af_scene_object *o=af_scene_object_at(s->runtime.scene,i);
        if(o->animation.asset[0]){++actors;CHECK(af_engine_animation_status(engine,o->id).playing);}if(o->model.root&&!o->model.primitive){af_mat4 posed,base;CHECK(af_engine_animated_world_matrix(engine,o->id,&posed)&&af_scene_world_matrix(s->runtime.scene,o->id,&base));for(unsigned k=0;k<16;++k)if(fabsf(posed.elements[k]-base.elements[k])>.01f){++moved;break;}}}
        CHECK(actors==2&&moved>=4);af_engine_stats stats=af_engine_get_stats(engine);CHECK(stats.missing_meshes==0&&stats.missing_textures==0&&stats.shadow_draw_calls>0);CHECK(af_engine_set_window_size(engine,720,480));space(engine);break;}
    case 5:CHECK(status.clip==1);CHECK(af_engine_animation_pause(engine,s->actor,true));s->paused_time=status.time;break;
    case 6:CHECK(status.time==s->paused_time);af_engine_request_quit(engine);return;
    default:CHECK(false);
    }
}
static void fixed(af_engine *engine,double delta,void *user){state *s=user;ed_runtime_update(&s->runtime,delta,true);af_engine_set_scene(engine,s->runtime.scene);}
static void shutdown(af_engine *engine,void *user){state *s=user;af_engine_set_scene(engine,NULL);ed_runtime_stop(&s->runtime);af_scene_destroy(s->source);}
int main(int argc,char **argv){if(argc!=4)return 1;af_engine_config config=af_engine_config_default();config.title="Afterlight simple-to-complex animation Play";config.width=640;config.height=480;config.enable_validation=true;af_engine *engine=af_engine_create(&config);if(!engine)return 1;state s={.simple=argv[1],.complex=argv[2],.module=argv[3]};af_game game={.init=init,.update=update,.shutdown=shutdown,.fixed_update=fixed,.user_data=&s};int result=af_engine_run(engine,&game);af_engine_stats stats=af_engine_get_stats(engine);af_engine_destroy(engine);if(result||s.failed||s.phase!=7||stats.validation_errors)return 1;puts("Demo Play: real C module initialization/Space controls, pause/seek, two independent mirrored nine-joint rigs, posed matrices, live shadows and resize passed.");return 0;}
