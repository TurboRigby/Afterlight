/* SPDX-License-Identifier: MPL-2.0 */
#include <afterlight/afterlight.h>
#include <stdio.h>
#include <stdlib.h>
#include "cube.h"

/* Fixed camera, framebuffer and warmup. CSV goes to stdout; logs to stderr.
   GPU results are delayed fence-completed samples, excluding presentation. */
typedef struct profile {af_scene *scene;const char *path;unsigned phase,ticks,samples;double sum[9];bool failed;} profile;
static bool init(af_engine *engine,void *data){
    profile *p=data;af_asset_error error;af_scene_error scene_error;
    p->scene=af_scene_load(p->path,&scene_error);if(!p->scene){fprintf(stderr,"%s\n",scene_error.message);return false;}
    af_mesh_data cube=cube_mesh_data();if(!af_engine_upload_mesh(engine,"cube",&cube)||!af_engine_load_scene_assets(engine,p->scene,&error)){fprintf(stderr,"%s\n",error.message);return false;}
    af_camera camera=af_camera_default();if(af_scene_game_camera(p->scene,&camera)){camera.position.y+=3;camera.position.z+=6;camera.pitch=-.3f;}af_engine_set_camera(engine,&camera);
    af_rect viewport={252,104,694,664};af_engine_set_viewport(engine,&viewport);af_engine_set_scene(engine,p->scene);
    puts("variant,samples,cpu_update_ms,cpu_render_ms,cpu_fence_ms,cpu_acquire_ms,gpu_shadow_ms,gpu_scene_ms,gpu_post_ms,gpu_total_ms,cpu_animation_ms");return true;
}
static void update(af_engine *engine,double dt,void *data){
    (void)dt;profile *p=data;af_engine_stats s=af_engine_get_stats(engine);
    if(++p->ticks>30&&s.gpu_timing_valid){double v[]={s.cpu_update_ms,s.cpu_render_ms,s.cpu_fence_ms,s.cpu_acquire_ms,s.gpu_shadow_ms,s.gpu_scene_ms,s.gpu_post_ms,s.gpu_frame_ms,s.cpu_animation_ms};for(unsigned i=0;i<9;++i)p->sum[i]+=v[i];++p->samples;}
    if(p->ticks<150)return;
    static const char *names[]={"full","no_bloom","no_local_shadows","no_shadows"};
    if(!p->samples){p->failed=true;af_engine_request_quit(engine);return;}
    printf("%s,%u",names[p->phase],p->samples);for(unsigned i=0;i<9;++i){printf(",%.4f",p->sum[i]/p->samples);p->sum[i]=0;}puts("");fflush(stdout);
    p->ticks=p->samples=0;if(++p->phase==4){af_engine_request_quit(engine);return;}
    if(p->phase==1){af_render_settings r=af_scene_get_render_settings(p->scene);r.bloom=false;af_scene_set_render_settings(p->scene,r);}
    if(p->phase==2)for(size_t i=0;i<af_scene_local_light_count(p->scene);++i){af_local_light l=*af_scene_local_light_at(p->scene,i);l.cast_shadows=false;af_scene_set_local_light(p->scene,i,l);}
    if(p->phase==3){af_shadow_settings r=af_scene_get_shadows(p->scene);r.enabled=false;af_scene_set_shadows(p->scene,r);}
}
static void shutdown(af_engine *engine,void *data){profile *p=data;af_engine_set_scene(engine,NULL);af_scene_destroy(p->scene);}
int main(int argc,char **argv){
    if(argc!=2){fputs("Usage: afterlight_profile LIGHTING_SCENE\n",stderr);return 2;}
    profile p={.path=argv[1]};af_engine_config config=af_engine_config_default();config.title="Afterlight fixed-camera profiler";config.width=1280;config.height=800;config.enable_validation=false;
    af_engine *engine=af_engine_create(&config);if(!engine)return 1;af_game game={.init=init,.update=update,.shutdown=shutdown,.user_data=&p};int result=af_engine_run(engine,&game);af_engine_destroy(engine);return result||p.failed;
}
