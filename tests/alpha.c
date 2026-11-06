/* SPDX-License-Identifier: MPL-2.0 */
#include <afterlight/afterlight.h>
#include "render_test.h"
#include <stdio.h>
#include <string.h>
typedef struct state {af_scene *scene;af_object_id front,back;unsigned tick;bool failed;unsigned char blend[4];} state;
static const af_vertex vertices[]={ {.position={-2,-2,0},.color={1,1,1,1},.normal={0,0,1},.uv={0,0}}, {.position={2,-2,0},.color={1,1,1,1},.normal={0,0,1},.uv={1,0}}, {.position={2,2,0},.color={1,1,1,1},.normal={0,0,1},.uv={1,1}}, {.position={-2,2,0},.color={1,1,1,1},.normal={0,0,1},.uv={0,1}} };
static const uint32_t indices[]={0,1,2,0,2,3};
#define CHECK(x) do{if(!(x)){fprintf(stderr,"Alpha tick %u line %d: %s\n",s->tick,__LINE__,#x);s->failed=true;af_engine_request_quit(e);return;}}while(0)
static bool init(af_engine *e,void *data){state *s=data;s->scene=af_scene_create();af_mesh_data mesh={vertices,4,indices,6};if(!s->scene||!af_engine_upload_mesh(e,"quad",&mesh))return false;
    /* Near blend intentionally appears before the opaque background in scene order. */
    s->front=af_scene_add_object(s->scene,"Red glass","quad");s->back=af_scene_add_object(s->scene,"Blue wall","quad");af_transform t=af_transform_identity();t.position.z=-1;af_scene_set_object_transform(s->scene,s->back,t);
    af_material m=af_material_default();m.unlit=true;m.base_color=(af_color){0,0,1,1};af_scene_set_object_material(s->scene,s->back,m);m.base_color=(af_color){1,0,0,.5f};m.alpha_mode=AF_ALPHA_BLEND;af_scene_set_object_material(s->scene,s->front,m);af_camera camera=af_camera_default();camera.position.z=4;af_engine_set_camera(e,&camera);af_engine_set_scene(e,s->scene);return true;}
static void capture(af_engine *e,state *s){int w,h;af_engine_get_window_size(e,&w,&h);if(!af_engine_test_capture_pixel(e,(af_vec2){(float)w/2,(float)h/2})){s->failed=true;af_engine_request_quit(e);}}
static void update(af_engine *e,double dt,void *data){(void)dt;state *s=data;++s->tick;unsigned char p[4];af_material m=af_scene_get_object(s->scene,s->front)->material;
    if(s->tick==3)capture(e,s);
    if(s->tick==4){CHECK(af_engine_test_read_pixel(e,s->blend));CHECK(s->blend[0]>50&&s->blend[2]>50);m.alpha_mode=AF_ALPHA_MASK;m.alpha_cutoff=.75f;CHECK(af_scene_set_object_material(s->scene,s->front,m));capture(e,s);}
    if(s->tick==5){CHECK(af_engine_test_read_pixel(e,p)&&p[2]>p[0]+80);m.alpha_cutoff=.25f;CHECK(af_scene_set_object_material(s->scene,s->front,m));capture(e,s);}
    if(s->tick==6){CHECK(af_engine_test_read_pixel(e,p)&&p[0]>p[2]+80);m.alpha_mode=AF_ALPHA_BLEND;CHECK(af_scene_set_object_material(s->scene,s->front,m));af_material back=af_scene_get_object(s->scene,s->back)->material;back.alpha_mode=AF_ALPHA_BLEND;back.base_color.a=.5f;CHECK(af_scene_set_object_material(s->scene,s->back,back));capture(e,s);}
    if(s->tick==7){CHECK(af_engine_test_read_pixel(e,p)&&p[0]>p[2]);CHECK(af_engine_set_window_size(e,720,480));}
    if(s->tick==8){m.alpha_mode=AF_ALPHA_MASK;m.alpha_cutoff=.5f;CHECK(af_scene_set_object_material(s->scene,s->front,m));capture(e,s);}
    if(s->tick==9)CHECK(af_engine_test_read_pixel(e,p)&&p[0]>p[2]+80);
    if(s->tick==10){CHECK(!af_engine_get_stats(e).validation_errors);af_engine_request_quit(e);}}
static void shutdown(af_engine *e,void *data){state *s=data;af_engine_set_scene(e,NULL);af_scene_destroy(s->scene);}
int main(void){af_engine_config config=af_engine_config_default();config.title="Afterlight alpha pixels";config.width=640;config.height=480;config.enable_validation=true;af_engine *e=af_engine_create(&config);if(!e)return 1;state s={0};af_game game={.init=init,.update=update,.shutdown=shutdown,.user_data=&s};int result=af_engine_run(e,&game);af_engine_destroy(e);if(result||s.failed||s.tick!=10)return 1;puts("Alpha pixels: opaque-before-blend, cutoff discard, sorted blended layers and resize passed.");return 0;}
