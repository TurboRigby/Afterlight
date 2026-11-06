/* SPDX-License-Identifier: MPL-2.0 */
#include <afterlight/model.h>
#include <stdio.h>
#include <string.h>
static af_scene *make_scene(const char *assets,bool complex){
    af_scene *s=af_scene_create();if(!s)return NULL;char path[4096];af_asset_error error;af_object_id root;
    snprintf(path,sizeof(path),"%s/%s.gltf",assets,complex?"character":"mechanism");
    if(!af_scene_import_model(s,complex?"Character":"Mechanism",path,&root,&error)){fprintf(stderr,"%s\n",error.message);goto fail;}
    af_transform t=af_transform_identity();t.position.x=complex?-1.5f:-1;if(!af_scene_set_object_transform(s,root,t))goto fail;
    if(complex){af_object_id other;if(!af_scene_import_model(s,"Mirror character",path,&other,&error))goto fail;t=af_transform_identity();t.position.x=1.8f;t.scale=(af_vec3){-.8f,.8f,.8f};if(!af_scene_set_object_transform(s,other,t))goto fail;
        af_animation_component c=af_scene_get_object(s,other)->animation;c.clip=2;c.speed=.8f;if(!af_scene_set_object_animation(s,other,c))goto fail;
    }
    af_object_id floor=af_scene_add_object(s,"Stage","cube");t=af_transform_identity();t.position.y=-.3f;t.scale=(af_vec3){12,.5f,8};if(!floor||!af_scene_set_object_transform(s,floor,t))goto fail;
    af_material material=af_material_default();material.base_color=(af_color){.2f,.24f,.3f,1};material.physically_based=true;material.roughness=.7f;if(!af_scene_set_object_material(s,floor,material))goto fail;
    af_shadow_settings shadows=af_shadow_settings_default();shadows.resolution=1024;shadows.distance=30;if(!af_scene_set_shadows(s,shadows))goto fail;
    af_local_light key=af_local_light_default(AF_LIGHT_POINT);key.position=(af_vec3){-3,4,4};key.color=(af_vec3){1,.65f,.35f};key.intensity=35;key.range=12;key.cast_shadows=true;snprintf(key.name,sizeof(key.name),"Warm stage key");if(!af_scene_add_local_light(s,key))goto fail;
    af_local_light rim=af_local_light_default(AF_LIGHT_SPOT);rim.position=(af_vec3){3,5,-2};rim.direction=(af_vec3){-.4f,-1,.3f};rim.color=(af_vec3){.3f,.55f,1};rim.intensity=60;rim.range=12;rim.cast_shadows=true;snprintf(rim.name,sizeof(rim.name),"Blue rim");if(!af_scene_add_local_light(s,rim))goto fail;
    snprintf(path,sizeof(path),"%s/studio.hdr",assets);if(!af_scene_set_asset(s,"studio_hdr",AF_ASSET_ENVIRONMENT,path))goto fail;
    af_render_settings render=af_render_settings_default();render.environment_enabled=true;snprintf(render.environment_asset,sizeof(render.environment_asset),"studio_hdr");render.environment_intensity=.65f;render.skybox=true;render.skybox_intensity=.4f;render.tone_mapping=true;render.bloom=true;render.bloom_strength=.1f;if(!af_scene_set_render_settings(s,render))goto fail;
    af_object_id camera=af_scene_add_object(s,"Demo camera","__node");t=af_transform_identity();t.position=(af_vec3){0,3.1f,9};t.rotation=(af_vec3){-.17f,0,0};if(!af_scene_set_object_transform(s,camera,t))goto fail;
    af_gameplay_component gameplay=af_gameplay_component_default();gameplay.camera.enabled=true;gameplay.camera.primary=true;gameplay.camera.eye_height=0;if(!af_scene_set_object_gameplay(s,camera,gameplay))goto fail;
    return s;
fail:af_scene_destroy(s);return NULL;
}
static bool relative_assets(af_scene *scene,const char *scene_path){
    char folder[AF_ASSET_PATH_CAPACITY];if(!af_asset_resolve_path(scene_path,"placeholder",folder,sizeof(folder)))return false;
    char *slash=strrchr(folder,'/');if(!slash)return false;slash[1]=0;
    for(size_t i=0;i<af_scene_asset_count(scene);++i){af_scene_asset asset=*af_scene_asset_at(scene,i);char reference[AF_ASSET_PATH_CAPACITY];size_t common=0;
        for(size_t j=0;folder[j]&&folder[j]==asset.path[j];++j)if(folder[j]=='/')common=j+1;
        if(!common)return false;size_t at=0;for(size_t j=common;folder[j];++j)if(folder[j]=='/'){if(at+3>=sizeof(reference))return false;memcpy(reference+at,"../",3);at+=3;}
        if(snprintf(reference+at,sizeof(reference)-at,"%s",asset.path+common)>=(int)(sizeof(reference)-at)||!af_scene_set_asset(scene,asset.name,asset.kind,reference))return false;
    }return true;
}
int main(int argc,char **argv){
    if(argc!=3){fputs("Usage: afterlight_animation_builder ASSET_DIRECTORY OUTPUT_DIRECTORY\n",stderr);return 2;}
    for(unsigned i=0;i<2;++i){af_scene *s=make_scene(argv[1],i!=0);if(!s)return 1;char path[4096];snprintf(path,sizeof(path),"%s/%s.afterlight.json",argv[2],i?"animations":"mechanism");af_scene_error error;
        if(!relative_assets(s,path)||!af_scene_save(s,path,&error)){fprintf(stderr,"Cannot save portable demo scene.\n");af_scene_destroy(s);return 1;}af_scene *loaded=af_scene_load(path,&error);if(!loaded){af_scene_destroy(s);return 1;}printf("%s: %zu objects, %zu assets; round trip passed.\n",i?"Character stage":"Mechanism",af_scene_object_count(loaded),af_scene_asset_count(loaded));af_scene_destroy(loaded);af_scene_destroy(s);}
    return 0;
}
