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
    af_material m=af_scene_get_object(d.scene,object)->material;m.physically_based=true;m.roughness=0.25f;m.metallic=0.75f;m.emission=(af_vec3){0.1f,0.2f,0.3f};m.emission_strength=5;m.normal_strength=1.5f;
    CHECK(ed_document_material(&d,m));ed_document_end_edit(&d);uint64_t revision=d.revision;CHECK(ed_document_material(&d,m)&&d.revision==revision);
    CHECK(ed_document_undo(&d)&&!af_scene_get_object(d.scene,object)->material.physically_based);CHECK(ed_document_redo(&d)&&af_scene_get_object(d.scene,object)->material.physically_based);
    CHECK(ed_document_add_light(&d,AF_LIGHT_POINT)&&ed_document_add_light(&d,AF_LIGHT_SPOT));af_local_light light=*af_scene_local_light_at(d.scene,1);light.position=(af_vec3){2,4,3};light.color=(af_vec3){1,0.5f,0.25f};light.intensity=35;light.range=20;light.enabled=false;snprintf(light.name,sizeof(light.name),"Spot Україна");CHECK(ed_document_local_light(&d,1,light));ed_document_end_edit(&d);
    CHECK(ed_document_render_settings(&d,(af_render_settings){.exposure=1.25f,.tone_mapping=true}));ed_document_end_edit(&d);
    CHECK(ed_document_save(&d,"render.json"));CHECK(ed_document_remove_light(&d,0)&&af_scene_local_light_count(d.scene)==1);CHECK(ed_document_undo(&d)&&af_scene_local_light_count(d.scene)==2&&!ed_document_dirty(&d));
    af_scene *copy=af_scene_load("render.json",&error);CHECK(copy);const af_material *restored=&af_scene_get_object(copy,object)->material;
    CHECK(restored->physically_based&&restored->roughness==m.roughness&&restored->metallic==m.metallic&&restored->emission_strength==m.emission_strength&&restored->emission.y==m.emission.y&&restored->normal_strength==m.normal_strength&&!strcmp(restored->normal_map,"normal"));
    CHECK(af_scene_find_asset(copy,"normal")->kind==AF_ASSET_NORMAL_MAP&&af_scene_local_light_count(copy)==2);const af_local_light *spot=af_scene_local_light_at(copy,1);CHECK(!spot->enabled&&spot->type==AF_LIGHT_SPOT&&spot->position.x==2&&spot->color.y==0.5f&&spot->intensity==35&&spot->range==20&&!strcmp(spot->name,light.name)&&spot->inner_angle==light.inner_angle);
    CHECK(af_scene_get_render_settings(copy).exposure==1.25f&&af_scene_get_render_settings(copy).tone_mapping);af_scene *clone=af_scene_clone(copy);CHECK(clone&&af_scene_local_light_count(clone)==2&&af_scene_get_render_settings(clone).tone_mapping);af_scene_destroy(clone);
    af_material invalid=m;invalid.roughness=NAN;CHECK(!af_scene_set_object_material(copy,object,invalid));invalid=m;invalid.metallic=2;CHECK(!af_scene_set_object_material(copy,object,invalid));invalid=m;invalid.normal_strength=-1;CHECK(!af_scene_set_object_material(copy,object,invalid));invalid=m;invalid.emission.x=-1;CHECK(!af_scene_set_object_material(copy,object,invalid));
    light=af_local_light_default(AF_LIGHT_SPOT);light.direction=(af_vec3){0};CHECK(!af_scene_add_local_light(copy,light));light=af_local_light_default(AF_LIGHT_SPOT);light.inner_angle=light.outer_angle+1;CHECK(!af_scene_add_local_light(copy,light));light=af_local_light_default(AF_LIGHT_POINT);light.range=0;CHECK(!af_scene_add_local_light(copy,light));
    light=af_local_light_default(AF_LIGHT_POINT);while(af_scene_local_light_count(copy)<AF_SCENE_MAX_LOCAL_LIGHTS)CHECK(af_scene_add_local_light(copy,light));CHECK(!af_scene_add_local_light(copy,light)&&!af_scene_remove_local_light(copy,AF_SCENE_MAX_LOCAL_LIGHTS));CHECK(!af_scene_set_render_settings(copy,(af_render_settings){.exposure=11}));af_scene_destroy(copy);
    FILE *file=fopen("render.json","rb");CHECK(file);CHECK(!fseek(file,0,SEEK_END));long size=ftell(file);CHECK(size>0&&!fseek(file,0,SEEK_SET));char *text=malloc((size_t)size+1);CHECK(text&&fread(text,1,(size_t)size,file)==(size_t)size&&fclose(file)==0);text[size]=0;cJSON *root=cJSON_Parse(text);free(text);CHECK(root);
    cJSON *bad=cJSON_Duplicate(root,1);cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(material(bad),"roughness"),2);CHECK(rejects(bad));
    bad=cJSON_Duplicate(root,1);cJSON_DeleteItemFromObjectCaseSensitive(material(bad),"normal_map");CHECK(rejects(bad));
    bad=cJSON_Duplicate(root,1);CHECK(cJSON_AddBoolToObject(material(bad),"physically_based",true));CHECK(rejects(bad));
    bad=cJSON_Duplicate(root,1);CHECK(cJSON_SetValuestring(cJSON_GetObjectItemCaseSensitive(local(bad),"type"),"unknown"));CHECK(rejects(bad));
    bad=cJSON_Duplicate(root,1);cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(local(bad),"range"),-1);CHECK(rejects(bad));
    bad=cJSON_Duplicate(root,1);cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(bad,"render"),"exposure"),100);CHECK(rejects(bad));
    bad=cJSON_Duplicate(root,1);cJSON *lights=cJSON_GetObjectItemCaseSensitive(bad,"local_lights");while(cJSON_GetArraySize(lights)<=AF_SCENE_MAX_LOCAL_LIGHTS)CHECK(cJSON_AddItemToArray(lights,cJSON_Duplicate(cJSON_GetArrayItem(lights,0),1)));CHECK(rejects(bad));
    bad=cJSON_Duplicate(root,1);CHECK(cJSON_AddNumberToObject(bad,"unknown",1));CHECK(rejects(bad));cJSON_Delete(root);
    ed_document_destroy(&d);puts("Render data: legacy scenes, material/light/exposure round trips, clone, undo, limits, malformed JSON and browser search/assignment passed.");return 0;
}
