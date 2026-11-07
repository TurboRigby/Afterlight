/* SPDX-License-Identifier: MPL-2.0 */
#include <afterlight/afterlight.h>
#include <afterlight/animation.h>
#include "render_test.h"
#include "cube.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct state {af_scene *scene;af_camera camera;af_object_id object;unsigned phase,ticks;bool failed;unsigned char shadow[4];} state;
#define CHECK(c) do{if(!(c)){fprintf(stderr,"Skin pixels phase %u line %d: %s\n",s->phase,__LINE__,#c);s->failed=true;af_engine_request_quit(engine);return;}}while(0)
static af_vec2 project(state *s,af_vec3 p){af_mat4 vp=af_mat4_mul(af_mat4_perspective(s->camera.vertical_fov,4.0f/3,s->camera.near_clip,s->camera.far_clip),af_camera_view(&s->camera));af_vec4 c=af_mat4_transform(vp,(af_vec4){p.x,p.y,p.z,1});return (af_vec2){(c.x/c.w+1)*200,(c.y/c.w+1)*150};}
static bool set_high_joint(af_engine *engine,af_object_id object,af_mat4 transform)
{
    af_mat4 palette[AF_ANIMATION_MAX_JOINTS];
    for(size_t i=0;i<AF_ANIMATION_MAX_JOINTS-1;++i)palette[i]=af_mat4_identity();
    palette[AF_ANIMATION_MAX_JOINTS-1]=transform;
    return af_engine_set_skin_palette(engine,object,palette,AF_ANIMATION_MAX_JOINTS);
}
static bool init(af_engine *engine,void *user){
    state *s=user;s->scene=af_scene_create();if(!s->scene)return false;
    af_vertex vertices[]={ {.position={-.5f,-.5f,0},.color={1,0,0,1},.normal={0,1,0},.weights={1,0,0,0}}, {.position={.5f,-.5f,0},.color={1,0,0,1},.normal={0,1,0},.weights={1,0,0,0}}, {.position={.5f,.5f,0},.color={1,0,0,1},.normal={0,1,0},.weights={1,0,0,0}}, {.position={-.5f,.5f,0},.color={1,0,0,1},.normal={0,1,0},.weights={1,0,0,0}}};uint32_t indices[]={0,1,2,0,2,3};af_mesh_data mesh={vertices,4,indices,6};
    for(size_t i=0;i<4;++i)vertices[i].joints[0]=AF_ANIMATION_MAX_JOINTS-1;
    if(!af_engine_upload_mesh(engine,"skin",&mesh))return false;s->object=af_scene_add_object(s->scene,"Skin","skin");af_material material=af_material_default();material.unlit=true;af_scene_set_object_material(s->scene,s->object,material);
    af_shadow_settings shadows=af_shadow_settings_default();shadows.enabled=false;shadows.resolution=512;af_scene_set_shadows(s->scene,shadows);
    s->camera=af_camera_default();s->camera.position.z=6;af_engine_set_camera(engine,&s->camera);af_engine_set_scene(engine,s->scene);af_engine_set_clear_color(engine,(af_color){0,0,0,1});return set_high_joint(engine,s->object,af_mat4_identity());
}
static bool shadow_scene(af_engine *engine,state *s){
    af_engine_set_scene(engine,NULL);af_scene_destroy(s->scene);s->scene=af_scene_create();if(!s->scene)return false;
    af_mesh_data cube=cube_mesh_data();af_vertex *vertices=malloc(cube.vertex_count*sizeof(*vertices));if(!vertices)return false;memcpy(vertices,cube.vertices,cube.vertex_count*sizeof(*vertices));for(uint32_t i=0;i<cube.vertex_count;++i)vertices[i].weights=(af_vec4){1,0,0,0};cube.vertices=vertices;bool ok=af_engine_upload_mesh(engine,"cube",&cube);free(vertices);if(!ok)return false;
    af_vertex floor[]={ {.position={-6,0,-6},.color={1,1,1,1},.normal={0,1,0}}, {.position={6,0,-6},.color={1,1,1,1},.normal={0,1,0}}, {.position={6,0,6},.color={1,1,1,1},.normal={0,1,0}}, {.position={-6,0,6},.color={1,1,1,1},.normal={0,1,0}}};uint32_t indices[]={0,2,1,0,3,2};af_mesh_data mesh={floor,4,indices,6};if(!af_engine_upload_mesh(engine,"floor",&mesh))return false;
    s->object=af_scene_add_object(s->scene,"Caster","cube");af_scene_add_object(s->scene,"Receiver","floor");af_transform t=af_transform_identity();t.position.y=1;af_scene_set_object_transform(s->scene,s->object,t);
    af_shadow_settings shadows=af_shadow_settings_default();shadows.mode=AF_SHADOW_SINGLE;shadows.resolution=1024;af_scene_set_shadows(s->scene,shadows);af_directional_light light=af_directional_light_default();light.ambient=.12f;af_scene_set_light(s->scene,light);
    s->camera=af_camera_default();s->camera.position=(af_vec3){-5,8,10};s->camera.yaw=atan2f(-10,5);s->camera.pitch=atan2f(-8,sqrtf(125));af_engine_set_camera(engine,&s->camera);af_engine_set_scene(engine,s->scene);af_mat4 identity=af_mat4_identity();return af_engine_set_skin_palette(engine,s->object,&identity,1);
}
static void update(af_engine *engine,double delta,void *user){
    (void)delta;state *s=user;if(++s->ticks<3)return;unsigned char pixel[4];if(s->phase)CHECK(af_engine_test_read_pixel(engine,pixel));af_vec3 sample={0};
    switch(s->phase){
    case 0:break;
    case 1:{CHECK(pixel[0]>240&&pixel[1]<5);af_mat4 move=af_mat4_translation((af_vec3){2,0,0});CHECK(set_high_joint(engine,s->object,move));break;}
    case 2:CHECK(pixel[0]<5);sample.x=2;break;
    case 3:{CHECK(pixel[0]>240);CHECK(af_engine_pick_object(engine,project(s,(af_vec3){2,0,0}))==s->object);af_mat4 bad=af_mat4_identity();bad.elements[0]=NAN;CHECK(!af_engine_set_skin_palette(engine,s->object,&bad,1));sample.x=2;break;}
    case 4:{CHECK(pixel[0]>240);CHECK(set_high_joint(engine,s->object,af_mat4_identity()));af_material m=af_material_default();CHECK(af_scene_set_object_material(s->scene,s->object,m));af_directional_light light=af_directional_light_default();light.direction=(af_vec3){0,-1,0};light.ambient=0;light.intensity=1;CHECK(af_scene_set_light(s->scene,light));break;}
    case 5:{CHECK(pixel[0]>230);af_mat4 rotation=af_mat4_rotation_z(AF_PI/2);CHECK(set_high_joint(engine,s->object,rotation));break;}
    case 6:CHECK(pixel[0]<5);CHECK(shadow_scene(engine,s));sample=(af_vec3){-1,0,-.6f};break;
    case 7:memcpy(s->shadow,pixel,4);{af_mat4 move=af_mat4_translation((af_vec3){4,0,0});CHECK(af_engine_set_skin_palette(engine,s->object,&move,1));}sample=(af_vec3){-1,0,-.6f};break;
    case 8:CHECK(pixel[0]>s->shadow[0]+30);{af_mat4 identity=af_mat4_identity();CHECK(af_engine_set_skin_palette(engine,s->object,&identity,1));}sample=(af_vec3){-1,0,-.6f};break;
    case 9:CHECK(abs((int)pixel[0]-s->shadow[0])<5);{af_directional_light light=af_directional_light_default();light.intensity=0;light.ambient=.12f;CHECK(af_scene_set_light(s->scene,light));af_shadow_settings shadows=af_scene_get_shadows(s->scene);shadows.enabled=false;CHECK(af_scene_set_shadows(s->scene,shadows));af_local_light point=af_local_light_default(AF_LIGHT_POINT);point.position=(af_vec3){0,5,0};point.intensity=50;point.range=10;point.cast_shadows=true;point.shadow_bias=.0002f;CHECK(af_scene_add_local_light(s->scene,point));}break;
    case 10:memcpy(s->shadow,pixel,4);{af_mat4 move=af_mat4_translation((af_vec3){4,0,0});CHECK(af_engine_set_skin_palette(engine,s->object,&move,1));}break;
    case 11:CHECK(pixel[0]>s->shadow[0]+30);{af_mat4 identity=af_mat4_identity();CHECK(af_engine_set_skin_palette(engine,s->object,&identity,1));}break;
    case 12:CHECK(abs((int)pixel[0]-s->shadow[0])<5);CHECK(af_engine_set_window_size(engine,440,330));break;
    case 13:af_engine_request_quit(engine);return;
    default:CHECK(false);
    }
    CHECK(af_engine_test_capture_pixel(engine,project(s,sample)));++s->phase;
}
static void shutdown(af_engine *engine,void *user){state *s=user;af_engine_set_scene(engine,NULL);af_scene_destroy(s->scene);}
int main(void){af_engine_config config=af_engine_config_default();config.title="Afterlight GPU skinning regression";config.width=400;config.height=300;config.enable_validation=true;af_engine *engine=af_engine_create(&config);if(!engine)return 1;state s={0};af_game game={.init=init,.update=update,.shutdown=shutdown,.user_data=&s};int result=af_engine_run(engine,&game);af_engine_stats stats=af_engine_get_stats(engine);af_engine_destroy(engine);if(result||s.failed||s.phase!=13||stats.validation_errors)return 1;puts("Skin pixels: deformation, normals, posed picking, invalid retention, directional/point shadows, palette cache invalidation, scene detach and resize passed.");return 0;}
