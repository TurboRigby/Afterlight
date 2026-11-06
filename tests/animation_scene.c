/* SPDX-License-Identifier: MPL-2.0 */
#include <afterlight/afterlight.h>
#include <afterlight/model.h>
#include "render_test.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
typedef struct state {const char *path,*skin;af_scene *scene;af_object_id root,node,primitive;af_camera camera;unsigned phase,ticks;bool failed;} state;
#define CHECK(c) do{if(!(c)){fprintf(stderr,"Animation scene phase %u line %d: %s\n",s->phase,__LINE__,#c);s->failed=true;af_engine_request_quit(engine);return;}}while(0)
static af_vec2 project(state *s,af_vec3 p){af_mat4 vp=af_mat4_mul(af_mat4_perspective(s->camera.vertical_fov,4.0f/3,.1f,100),af_camera_view(&s->camera));af_vec4 q=af_mat4_transform(vp,(af_vec4){p.x,p.y,p.z,1});return (af_vec2){(q.x/q.w+1)*200,(q.y/q.w+1)*150};}
static bool load(af_engine *engine,state *s,const char *path){
    af_engine_set_scene(engine,NULL);af_scene_destroy(s->scene);s->scene=af_scene_create();af_asset_error error;if(!s->scene||!af_scene_import_model(s->scene,"actor",path,&s->root,&error)||!af_engine_load_scene_assets(engine,s->scene,&error))return false;
    s->node=s->primitive=0;for(size_t i=0;i<af_scene_object_count(s->scene);++i){const af_scene_object *o=af_scene_object_at(s->scene,i);af_material material=o->material;material.unlit=true;af_scene_set_object_material(s->scene,o->id,material);if(o->model.root==s->root&&o->model.node==0){if(o->model.primitive)s->primitive=o->id;else s->node=o->id;}}
    af_shadow_settings shadows=af_shadow_settings_default();shadows.enabled=false;shadows.resolution=512;af_scene_set_shadows(s->scene,shadows);af_engine_set_scene(engine,s->scene);return s->node&&s->primitive;
}
static bool init(af_engine *engine,void *user){state *s=user;if(!load(engine,s,s->path))return false;s->camera=af_camera_default();s->camera.position=(af_vec3){0,1,8};af_engine_set_camera(engine,&s->camera);af_engine_set_simulation_paused(engine,true);af_engine_set_animation_playback(engine,false,false);af_engine_set_clear_color(engine,(af_color){0,0,0,1});return true;}
static void update(af_engine *engine,double delta,void *user){
    (void)delta;state *s=user;if(++s->ticks<3)return;unsigned char pixel[4];if(s->phase)CHECK(af_engine_test_read_pixel(engine,pixel));af_vec3 sample={0,2,0};
    switch(s->phase){
    case 0:break;
    case 1:CHECK(pixel[0]>240);CHECK(af_engine_animation_root(engine,s->primitive)==s->root);CHECK(af_engine_animation_play(engine,s->root,0,0)&&af_engine_animation_seek(engine,s->root,1)&&af_engine_animation_pause(engine,s->root,true));af_engine_set_animation_playback(engine,true,false);break;
    case 2:CHECK(pixel[0]<5);sample=(af_vec3){1,1,0};break;
    case 3:{CHECK(pixel[0]>240);af_mat4 world;CHECK(af_engine_animated_world_matrix(engine,s->node,&world)&&fabsf(world.elements[12]-2)<.0001f);CHECK(af_engine_pick_object(engine,project(s,(af_vec3){1,1,0}))==s->primitive);CHECK(af_engine_animation_play(engine,s->root,1,.5f)&&af_engine_animation_pause(engine,s->root,true));sample=(af_vec3){1,1,0};break;}
    case 4:CHECK(pixel[0]>240&&af_engine_animation_status(engine,s->root).fading);CHECK(af_engine_animation_seek(engine,s->root,0));break;
    case 5:CHECK(pixel[0]>240);CHECK(!af_engine_animation_play(engine,s->root,99,0)&&af_engine_animation_options(engine,s->root,2,false)&&af_engine_animation_play(engine,s->root,0,0));break;
    case 6:CHECK(af_engine_animation_status(engine,s->root).time==0);CHECK(af_engine_step_simulation(engine));break;
    case 7:CHECK(fabs(af_engine_animation_status(engine,s->root).time-2.0/60)<1e-8);af_engine_set_animation_playback(engine,false,false);break;
    case 8:CHECK(pixel[0]>240&&af_scene_get_object(s->scene,s->node)->transform.position.x==0);CHECK(load(engine,s,s->skin));sample=(af_vec3){0,1.8f,0};break;
    case 9:CHECK(pixel[0]>240);CHECK(af_engine_animation_seek(engine,s->root,1)&&af_engine_animation_pause(engine,s->root,true));af_engine_set_animation_playback(engine,true,false);sample=(af_vec3){0,1.8f,0};break;
    case 10:CHECK(pixel[0]<5);sample=(af_vec3){-.7f,1.5f,0};break;
    case 11:CHECK(pixel[0]>240);{af_asset_error error;CHECK(af_engine_reload_asset_source(engine,s->scene,s->skin,&error));CHECK(af_engine_animation_status(engine,s->root).time==0);}sample=(af_vec3){0,1.8f,0};break;
    case 12:CHECK(pixel[0]>240);af_engine_request_quit(engine);return;
    default:CHECK(false);
    }CHECK(af_engine_test_capture_pixel(engine,project(s,sample)));++s->phase;
}
static void fixed(af_engine *engine,double delta,void *user){(void)engine;(void)delta;(void)user;}
static void shutdown(af_engine *engine,void *user){state *s=user;af_engine_set_scene(engine,NULL);af_scene_destroy(s->scene);}
int main(int argc,char **argv){if(argc!=3)return 1;af_engine_config config=af_engine_config_default();config.title="Afterlight animation integration";config.width=400;config.height=300;config.enable_validation=true;af_engine *engine=af_engine_create(&config);if(!engine)return 1;state s={.path=argv[1],.skin=argv[2]};af_game game={.init=init,.update=update,.shutdown=shutdown,.fixed_update=fixed,.user_data=&s};int result=af_engine_run(engine,&game);af_engine_stats stats=af_engine_get_stats(engine);af_engine_destroy(engine);if(result||s.failed||s.phase!=12||stats.validation_errors)return 1;puts("Animation scene: automatic TRS/skin binding, pose picking, fixed pause/step, crossfade hold, authored isolation and transactional source reload passed.");return 0;}
