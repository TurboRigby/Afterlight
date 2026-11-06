/* SPDX-License-Identifier: MPL-2.0 */
#include <afterlight/model.h>
#include <stdio.h>
#include <string.h>
static af_object_id box(af_scene *s,const char *name,af_vec3 position,af_vec3 half,af_color color){
    af_object_id id=af_scene_add_object(s,name,"cube");af_transform t=af_transform_identity();t.position=position;t.scale=af_vec3_scale(half,2/1.3f);af_collider c=af_collider_default();c.type=AF_COLLIDER_BOX;c.half_extents=(af_vec3){.65f,.65f,.65f};af_material m=af_material_default();m.base_color=color;m.physically_based=true;m.roughness=.7f;
    if(!id||!af_scene_set_object_transform(s,id,t)||!af_scene_set_object_collider(s,id,c)||!af_scene_set_object_material(s,id,m))return 0;return id;
}
static bool portable(af_scene *s,const char *output){
    char folder[4096];if(!af_asset_resolve_path(output,"placeholder",folder,sizeof(folder)))return false;char *slash=strrchr(folder,'/');if(!slash)return false;slash[1]=0;
    for(size_t i=0;i<af_scene_asset_count(s);++i){af_scene_asset a=*af_scene_asset_at(s,i);char absolute[4096],reference[4096];if(!af_asset_resolve_path(output,a.path,absolute,sizeof(absolute)))return false;size_t common=0;
        for(size_t j=0;folder[j]&&folder[j]==absolute[j];++j)if(folder[j]=='/')common=j+1;if(!common)return false;size_t at=0;
        for(size_t j=common;folder[j];++j)if(folder[j]=='/'){if(at+3>=sizeof(reference))return false;memcpy(reference+at,"../",3);at+=3;}
        if(snprintf(reference+at,sizeof(reference)-at,"%s",absolute+common)>=(int)(sizeof(reference)-at)||!af_scene_set_asset(s,a.name,a.kind,reference))return false;
    }return true;
}
int main(int argc,char **argv){
    if(argc!=4){fputs("Usage: afterlight_character_builder ASSETS OBSTACLE_SCENE OUTPUT_DIRECTORY\n",stderr);return 2;}
    for(unsigned variant=0;variant<3;++variant){af_scene_error error;af_asset_error asset_error;af_scene *s=variant==1?af_scene_load(argv[2],&error):af_scene_create();if(!s)return 1;af_object_id body=0;
        if(variant==1){for(size_t i=0;i<af_scene_object_count(s);++i){const af_scene_object *o=af_scene_object_at(s,i);if(o->gameplay.player.enabled)body=o->id;}
            /* Resolve the older course's relative sounds before writing a new file. */
            for(size_t i=0;i<af_scene_asset_count(s);++i){af_scene_asset a=*af_scene_asset_at(s,i);char path[4096];if(!af_asset_resolve_path(argv[2],a.path,path,sizeof(path))||!af_scene_set_asset(s,a.name,a.kind,path))return 1;}
        }else{
            if(!box(s,"Practice ground",(af_vec3){0,-.5f,-2},(af_vec3){10,.5f,12},(af_color){.19f,.24f,.3f,1})||!box(s,"Camera wall",(af_vec3){-4,1,-1},(af_vec3){.25f,1,4},(af_color){.3f,.38f,.5f,1})||!box(s,"Low platform",(af_vec3){2,.25f,-3},(af_vec3){1.5f,.25f,1.5f},(af_color){.3f,.45f,.5f,1}))return 1;
            body=af_scene_add_object(s,"Player","__node");af_transform t=af_transform_identity();t.position=(af_vec3){0,.902f,5};af_scene_set_object_transform(s,body,t);af_collider c=af_collider_default();c.type=AF_COLLIDER_CAPSULE;c.radius=.35f;c.half_height=.55f;af_scene_set_object_collider(s,body,c);
            af_gameplay_component g=af_gameplay_component_default();g.player.enabled=true;g.camera.enabled=true;g.camera.primary=true;af_scene_set_object_gameplay(s,body,g);
            const char *sounds[]={"jump","land","checkpoint","finish"};for(unsigned i=0;i<4;++i){char path[4096];snprintf(path,sizeof(path),"%s/%s.wav",argv[1],sounds[i]);if(!af_scene_set_asset(s,sounds[i],AF_ASSET_AUDIO,path))return 1;}
        }
        if(!body||!af_scene_set_object_mesh(s,body,"__node"))return 1;char path[4096];snprintf(path,sizeof(path),"%s/locomotion.gltf",argv[1]);af_object_id actor;
        if(!af_scene_import_model(s,"Character",path,&actor,&asset_error)){fprintf(stderr,"%s\n",asset_error.message);return 1;}
        af_transform visual=af_transform_identity();visual.position.y=-.9f;visual.scale=(af_vec3){.65f,.65f,.65f};if(!af_scene_set_parent(s,actor,body)||!af_scene_set_object_transform(s,actor,visual))return 1;
        af_animation_component animation=af_scene_get_object(s,actor)->animation;animation.autoplay=false;af_character_rig rig=af_character_rig_default();rig.enabled=true;rig.root_node=10;if(!af_scene_set_character_rig(s,actor,rig))return 1;if(!af_scene_set_object_animation(s,actor,animation))return 1;
        if(variant==2){/* 64 independently posed actors share two asset records. */
            for(unsigned i=1;i<64;++i){af_object_id root;if(!af_scene_import_model(s,"Character",path,&root,&asset_error))return 1;af_transform t=af_transform_identity();t.position=(af_vec3){(float)(i%8)*2-7,0,-(float)(i/8)*2};t.scale=(af_vec3){.65f,.65f,.65f};if(!af_scene_set_object_transform(s,root,t))return 1;af_animation_component a=af_scene_get_object(s,root)->animation;af_character_rig rig=af_character_rig_default();rig.enabled=true;rig.root_node=10;if(!af_scene_set_character_rig(s,root,rig))return 1;a.clip=i%3;a.autoplay=true;if(!af_scene_set_object_animation(s,root,a))return 1;

            }
        }
        af_shadow_settings shadow=af_shadow_settings_default();shadow.resolution=1024;shadow.distance=45;if(!af_scene_set_shadows(s,shadow))return 1;
        snprintf(path,sizeof(path),"%s/%s.afterlight.json",argv[3],variant==0?"character":variant==1?"character-course":"character-stress");if(!portable(s,path)||!af_scene_save(s,path,&error))return 1;
        printf("%s: %zu objects, portable scene saved.\n",path,af_scene_object_count(s));af_scene_destroy(s);
    }return 0;
}
