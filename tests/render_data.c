/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "asset_browser.h"
#include "cJSON.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do{if(!(c)){fprintf(stderr,"Render data %d: %s\n",__LINE__,#c);return 1;}}while(0)
static bool rejects(cJSON *root){char *text=cJSON_Print(root);if(!text)return false;FILE *file=fopen("bad.json","wb");bool written=file&&fputs(text,file)>=0;if(file)written=fclose(file)==0&&written;cJSON_free(text);cJSON_Delete(root);if(!written)return false;af_scene_error error;af_scene *scene=af_scene_load("bad.json",&error);if(scene){af_scene_destroy(scene);return false;}return error.code==AF_SCENE_ERROR_FORMAT;}
static cJSON *material(cJSON *root){return cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(root,"objects"),0),"material");}
static cJSON *local(cJSON *root){return cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(root,"local_lights"),0);}
int main(int argc,char **argv){
    CHECK(argc==3);af_scene_error error;
    for(int i=1;i<3;++i){af_scene *legacy=af_scene_load(argv[i],&error);CHECK(legacy);CHECK(!af_scene_object_at(legacy,0)->material.physically_based&&af_scene_object_at(legacy,0)->material.normal_strength==1);CHECK(af_scene_local_light_count(legacy)==0&&af_scene_get_render_settings(legacy).exposure==0);af_scene_destroy(legacy);}
    ed_document d;CHECK(ed_document_init(&d));CHECK(ed_document_import_texture(&d,"checker","/tmp/Checker.png"));CHECK(ed_document_import_normal_map(&d,"normal","/tmp/Normals.png"));CHECK(af_scene_asset_count(d.scene)==2&&!d.selection);
    CHECK(af_scene_set_asset(d.scene,"pyramid",AF_ASSET_MESH,"/tmp/Pyramid.obj"));CHECK(ed_document_create_mesh_object(&d,"pyramid"));af_object_id object=d.selection;
    const af_scene_asset *asset=af_scene_find_asset(d.scene,"checker");CHECK(ed_asset_matches(asset,"CHECK",-1)&&ed_asset_matches(asset,"/tmp/checker",AF_ASSET_TEXTURE)&&!ed_asset_matches(asset,"other",-1)&&!ed_asset_matches(asset,"",AF_ASSET_MESH));
    CHECK(ed_document_assign_asset(&d,"checker"));ed_document_end_edit(&d);CHECK(ed_document_assign_asset(&d,"normal"));ed_document_end_edit(&d);
    CHECK(!ed_document_assign_asset(&d,"missing"));CHECK(!ed_document_create_mesh_object(&d,"checker"));
    af_material m=af_scene_get_object(d.scene,object)->material;m.alpha_mode=AF_ALPHA_MASK;m.alpha_cutoff=.6f;m.physically_based=true;m.roughness=0.25f;m.metallic=0.75f;m.emission=(af_vec3){0.1f,0.2f,0.3f};m.emission_strength=5;m.normal_strength=1.5f;
    CHECK(ed_document_material(&d,m));ed_document_end_edit(&d);uint64_t revision=d.revision;CHECK(ed_document_material(&d,m)&&d.revision==revision);
    CHECK(ed_document_undo(&d)&&!af_scene_get_object(d.scene,object)->material.physically_based);CHECK(ed_document_redo(&d)&&af_scene_get_object(d.scene,object)->material.physically_based);
    CHECK(ed_document_add_light(&d,AF_LIGHT_POINT)&&ed_document_add_light(&d,AF_LIGHT_SPOT));af_local_light light=*af_scene_local_light_at(d.scene,1);light.position=(af_vec3){2,4,3};light.color=(af_vec3){1,0.5f,0.25f};light.intensity=35;light.range=20;light.enabled=false;light.cast_shadows=true;light.shadow_bias=.002f;snprintf(light.name,sizeof(light.name),"Spot Україна");CHECK(ed_document_local_light(&d,1,light));ed_document_end_edit(&d);
    af_render_settings lighting=af_render_settings_default();lighting.exposure=1.25f;lighting.tone_mapping=true;lighting.environment_enabled=true;lighting.environment_profile=2;lighting.environment_rotation=.7f;lighting.environment_intensity=1.4f;lighting.bloom=true;lighting.bloom_strength=.3f;lighting.bloom_threshold=2;lighting.bloom_radius=2.5f;lighting.local_shadow_resolution=1024;strcpy(lighting.environment_asset,"sky");lighting.skybox=true;lighting.skybox_intensity=.7f;CHECK(af_scene_set_asset(d.scene,"sky",AF_ASSET_ENVIRONMENT,"/tmp/panorama.hdr"));
    CHECK(ed_document_render_settings(&d,lighting));ed_document_end_edit(&d);
    CHECK(ed_document_save(&d,"render.json"));CHECK(ed_document_remove_light(&d,0)&&af_scene_local_light_count(d.scene)==1);CHECK(ed_document_undo(&d)&&af_scene_local_light_count(d.scene)==2&&!ed_document_dirty(&d));
    af_scene *copy=af_scene_load("render.json",&error);CHECK(copy);const af_material *restored=&af_scene_get_object(copy,object)->material;
    CHECK(restored->physically_based&&restored->roughness==m.roughness&&restored->metallic==m.metallic&&restored->emission_strength==m.emission_strength&&restored->emission.y==m.emission.y&&restored->normal_strength==m.normal_strength&&!strcmp(restored->normal_map,"normal"));
    CHECK(af_scene_find_asset(copy,"normal")->kind==AF_ASSET_NORMAL_MAP&&af_scene_local_light_count(copy)==2);const af_local_light *spot=af_scene_local_light_at(copy,1);CHECK(!spot->enabled&&spot->type==AF_LIGHT_SPOT&&spot->position.x==2&&spot->color.y==0.5f&&spot->intensity==35&&spot->range==20&&!strcmp(spot->name,light.name)&&spot->inner_angle==light.inner_angle);
    CHECK(af_scene_get_render_settings(copy).exposure==1.25f&&af_scene_get_render_settings(copy).tone_mapping);af_scene *clone=af_scene_clone(copy);CHECK(clone&&af_scene_local_light_count(clone)==2&&af_scene_get_render_settings(clone).tone_mapping);af_scene_destroy(clone);
    CHECK(af_scene_get_object(copy,object)->material.alpha_mode==AF_ALPHA_MASK&&af_scene_get_object(copy,object)->material.alpha_cutoff==.6f);
    af_render_settings saved=af_scene_get_render_settings(copy);CHECK(saved.environment_enabled&&saved.environment_profile==2&&saved.environment_rotation==.7f&&saved.bloom&&saved.bloom_radius==2.5f&&saved.local_shadow_resolution==1024&&saved.skybox&&saved.skybox_intensity==.7f&&!strcmp(saved.environment_asset,"sky"));
    CHECK(af_scene_local_light_at(copy,1)->cast_shadows&&af_scene_local_light_at(copy,1)->shadow_bias==.002f);
    af_material invalid=m;invalid.roughness=NAN;CHECK(!af_scene_set_object_material(copy,object,invalid));invalid=m;invalid.metallic=2;CHECK(!af_scene_set_object_material(copy,object,invalid));invalid=m;invalid.normal_strength=-1;CHECK(!af_scene_set_object_material(copy,object,invalid));invalid=m;invalid.emission.x=-1;CHECK(!af_scene_set_object_material(copy,object,invalid));
    light=af_local_light_default(AF_LIGHT_SPOT);light.direction=(af_vec3){0};CHECK(!af_scene_add_local_light(copy,light));light=af_local_light_default(AF_LIGHT_SPOT);light.inner_angle=light.outer_angle+1;CHECK(!af_scene_add_local_light(copy,light));light=af_local_light_default(AF_LIGHT_POINT);light.range=0;CHECK(!af_scene_add_local_light(copy,light));
    light=af_local_light_default(AF_LIGHT_POINT);while(af_scene_local_light_count(copy)<AF_SCENE_MAX_LOCAL_LIGHTS)CHECK(af_scene_add_local_light(copy,light));CHECK(!af_scene_add_local_light(copy,light)&&!af_scene_remove_local_light(copy,AF_SCENE_MAX_LOCAL_LIGHTS));CHECK(!af_scene_set_render_settings(copy,(af_render_settings){.exposure=11}));af_scene_destroy(copy);
    FILE *file=fopen("render.json","rb");CHECK(file);CHECK(!fseek(file,0,SEEK_END));long size=ftell(file);CHECK(size>0&&!fseek(file,0,SEEK_SET));char *text=malloc((size_t)size+1);CHECK(text&&fread(text,1,(size_t)size,file)==(size_t)size&&fclose(file)==0);text[size]=0;cJSON *root=cJSON_Parse(text);free(text);CHECK(root);
    af_render_settings current=af_scene_get_render_settings(d.scene);current.environment_intensity+=.2f;CHECK(ed_document_render_settings(&d,current));ed_document_end_edit(&d);CHECK(af_scene_get_render_settings(d.scene).environment_intensity==current.environment_intensity);CHECK(ed_document_undo(&d));
    cJSON *bad=cJSON_Duplicate(root,1);cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(material(bad),"roughness"),2);CHECK(rejects(bad));
    bad=cJSON_Duplicate(root,1);cJSON_DeleteItemFromObjectCaseSensitive(material(bad),"normal_map");CHECK(rejects(bad));
    bad=cJSON_Duplicate(root,1);CHECK(cJSON_AddBoolToObject(material(bad),"physically_based",true));CHECK(rejects(bad));
    bad=cJSON_Duplicate(root,1);CHECK(cJSON_SetValuestring(cJSON_GetObjectItemCaseSensitive(local(bad),"type"),"unknown"));CHECK(rejects(bad));
    bad=cJSON_Duplicate(root,1);cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(local(bad),"range"),-1);CHECK(rejects(bad));
    bad=cJSON_Duplicate(root,1);cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(bad,"render"),"exposure"),100);CHECK(rejects(bad));
    bad=cJSON_Duplicate(root,1);cJSON *lights=cJSON_GetObjectItemCaseSensitive(bad,"local_lights");while(cJSON_GetArraySize(lights)<=AF_SCENE_MAX_LOCAL_LIGHTS)CHECK(cJSON_AddItemToArray(lights,cJSON_Duplicate(cJSON_GetArrayItem(lights,0),1)));CHECK(rejects(bad));
    bad=cJSON_Duplicate(root,1);CHECK(cJSON_AddNumberToObject(bad,"unknown",1));CHECK(rejects(bad));
    bad=cJSON_Duplicate(root,1);cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(bad,"render"),"environment_profile"),3);CHECK(rejects(bad));
    bad=cJSON_Duplicate(root,1);cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(bad,"render"),"bloom_strength"),-1);CHECK(rejects(bad));
    bad=cJSON_Duplicate(root,1);cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(bad,"render"),"local_shadow_resolution"),257);CHECK(rejects(bad));
    bad=cJSON_Duplicate(root,1);cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(local(bad),"shadow_bias"),-1);CHECK(rejects(bad));
    bad=cJSON_Duplicate(root,1);cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(bad,"render"),"skybox_intensity"),-1);CHECK(rejects(bad));
    cJSON *v8=cJSON_Duplicate(root,1);cJSON_DeleteItemFromObjectCaseSensitive(v8,"collider_parts");cJSON_DeleteItemFromObjectCaseSensitive(v8,"next_part");cJSON_DeleteItemFromObjectCaseSensitive(v8,"joints");cJSON_DeleteItemFromObjectCaseSensitive(v8,"next_joint");cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(v8,"version"),8);cJSON *v8render=cJSON_GetObjectItemCaseSensitive(v8,"render");cJSON_DeleteItemFromObjectCaseSensitive(v8render,"environment_asset");cJSON_DeleteItemFromObjectCaseSensitive(v8render,"skybox");cJSON_DeleteItemFromObjectCaseSensitive(v8render,"skybox_intensity");
    cJSON *v8object;cJSON_ArrayForEach(v8object,cJSON_GetObjectItemCaseSensitive(v8,"objects")){cJSON_DeleteItemFromObjectCaseSensitive(v8object,"animation");cJSON_DeleteItemFromObjectCaseSensitive(v8object,"model");cJSON_DeleteItemFromObjectCaseSensitive(v8object,"rig");cJSON_DeleteItemFromObjectCaseSensitive(v8object,"body");cJSON *legacy_material=cJSON_GetObjectItemCaseSensitive(v8object,"material");cJSON_DeleteItemFromObjectCaseSensitive(legacy_material,"alpha_mode");cJSON_DeleteItemFromObjectCaseSensitive(legacy_material,"alpha_cutoff");}
    cJSON *old_assets=cJSON_GetObjectItemCaseSensitive(v8,"assets");for(int i=cJSON_GetArraySize(old_assets)-1;i>=0;--i)if(!strcmp(cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(old_assets,i),"kind")->valuestring,"environment"))cJSON_DeleteItemFromArray(old_assets,i);
    char *v8text=cJSON_Print(v8);FILE *v8file=fopen("legacy-v8.json","wb");CHECK(v8text&&v8file&&fputs(v8text,v8file)>=0&&fclose(v8file)==0);cJSON_free(v8text);af_scene *v8scene=af_scene_load("legacy-v8.json",&error);CHECK(v8scene&&!af_scene_get_render_settings(v8scene).skybox&&af_scene_get_render_settings(v8scene).environment_enabled);af_scene_destroy(v8scene);
    cJSON *v7=cJSON_Duplicate(v8,1);cJSON_Delete(v8);cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(v7,"version"),7);
    cJSON *v7render=cJSON_GetObjectItemCaseSensitive(v7,"render");const char *new_fields[]={"environment_asset","skybox","skybox_intensity","bloom","bloom_strength","bloom_threshold","bloom_radius","environment_enabled","environment_profile","environment_intensity","environment_rotation","local_shadow_resolution"};
    for(unsigned i=0;i<12;++i)cJSON_DeleteItemFromObjectCaseSensitive(v7render,new_fields[i]);
    cJSON *v7light;cJSON_ArrayForEach(v7light,cJSON_GetObjectItemCaseSensitive(v7,"local_lights")){cJSON_DeleteItemFromObjectCaseSensitive(v7light,"cast_shadows");cJSON_DeleteItemFromObjectCaseSensitive(v7light,"shadow_bias");}
    char *v7text=cJSON_Print(v7);FILE *legacy_file=fopen("legacy-v7.json","wb");CHECK(v7text&&legacy_file&&fputs(v7text,legacy_file)>=0&&fclose(legacy_file)==0);cJSON_free(v7text);cJSON_Delete(v7);
    af_scene *legacy=af_scene_load("legacy-v7.json",&error);CHECK(legacy&&!af_scene_get_render_settings(legacy).environment_enabled&&!af_scene_get_render_settings(legacy).bloom&&!af_scene_local_light_at(legacy,1)->cast_shadows);af_scene_destroy(legacy);
    cJSON_Delete(root);
    ed_document_destroy(&d);puts("Render data: legacy scenes, material/light/exposure round trips, clone, undo, limits, malformed JSON and browser search/assignment passed.");return 0;
}
