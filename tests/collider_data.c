/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "document.h"
#include "collider_view.h"
#include "cJSON.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do{if(!(c)){fprintf(stderr,"Collider data %d: %s\n",__LINE__,#c);return 1;}}while(0)
static bool rejects(cJSON *root){char *text=cJSON_Print(root);if(!text)return false;FILE *file=fopen("bad.json","wb");bool ok=file&&fputs(text,file)>=0;if(file)ok=fclose(file)==0&&ok;cJSON_free(text);cJSON_Delete(root);af_scene_error error;af_scene *scene=ok?af_scene_load("bad.json",&error):NULL;if(scene)af_scene_destroy(scene);return ok&&!scene&&error.code==AF_SCENE_ERROR_FORMAT;}
static cJSON *collider(cJSON *root){return cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(root,"objects"),0),"collider");}
int main(int argc,char **argv){CHECK(argc==2);af_scene_error error;af_scene *legacy=af_scene_load(argv[1],&error);CHECK(legacy&&af_scene_object_at(legacy,0)->collider.type==AF_COLLIDER_NONE);af_scene_destroy(legacy);
    ed_document d;CHECK(ed_document_init(&d)&&ed_document_add_cube(&d));af_object_id original=d.selection;af_collider value=af_collider_default();value.type=AF_COLLIDER_CAPSULE;value.trigger=true;value.layer=UINT32_MAX;value.radius=.3f;value.half_height=.7f;value.center=(af_vec3){1,2,3};CHECK(ed_document_collider(&d,value));ed_document_end_edit(&d);CHECK(ed_document_undo(&d)&&af_scene_get_object(d.scene,original)->collider.type==AF_COLLIDER_NONE);CHECK(ed_document_redo(&d)&&af_scene_get_object(d.scene,original)->collider.trigger);
    CHECK(ed_document_duplicate(&d));CHECK(af_scene_get_object(d.scene,d.selection)->collider.layer==UINT32_MAX&&af_scene_get_object(d.scene,d.selection)->collider.radius==.3f);
    CHECK(ed_document_save(&d,"colliders.json"));af_scene *copy=af_scene_load("colliders.json",&error);CHECK(copy);const af_collider *restored=&af_scene_get_object(copy,original)->collider;CHECK(restored->type==AF_COLLIDER_CAPSULE&&restored->radius==.3f&&restored->half_height==.7f&&restored->center.y==2&&restored->layer==UINT32_MAX);
    af_world_collider world;CHECK(af_scene_world_collider(copy,original,&world));ed_line3 lines[ED_COLLIDER_MAX_LINES];CHECK(ed_collider_lines(world,lines,ED_COLLIDER_MAX_LINES)==196);value.type=AF_COLLIDER_BOX;CHECK(af_scene_set_object_collider(copy,original,value)&&af_scene_world_collider(copy,original,&world));CHECK(ed_collider_lines(world,lines,ED_COLLIDER_MAX_LINES)==12);
    value.type=AF_COLLIDER_SPHERE;CHECK(af_scene_set_object_collider(copy,original,value)&&af_scene_world_collider(copy,original,&world));CHECK(ed_collider_lines(world,lines,ED_COLLIDER_MAX_LINES)==96);
    af_transform transform=af_transform_identity();transform.scale.x=0;CHECK(af_scene_set_object_transform(copy,original,transform)&&!af_scene_world_collider(copy,original,&world));value.radius=NAN;CHECK(!af_scene_set_object_collider(copy,original,value));
    af_scene_destroy(copy);FILE *file=fopen("colliders.json","rb");CHECK(file&&!fseek(file,0,SEEK_END));long length=ftell(file);CHECK(length>0&&!fseek(file,0,SEEK_SET));char *text=malloc((size_t)length+1);CHECK(text&&fread(text,1,(size_t)length,file)==(size_t)length&&fclose(file)==0);text[length]=0;cJSON *root=cJSON_Parse(text);free(text);CHECK(root);
    cJSON *bad=cJSON_Duplicate(root,1);cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(collider(bad),"radius"),-1);CHECK(rejects(bad));
    bad=cJSON_Duplicate(root,1);cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(collider(bad),"layer"),4294967296.0);CHECK(rejects(bad));
    bad=cJSON_Duplicate(root,1);cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(collider(bad),"layer"),1.5);CHECK(rejects(bad));
    bad=cJSON_Duplicate(root,1);cJSON_SetValuestring(cJSON_GetObjectItemCaseSensitive(collider(bad),"type"),"mesh");CHECK(rejects(bad));
    bad=cJSON_Duplicate(root,1);cJSON_DeleteItemFromObjectCaseSensitive(collider(bad),"enabled");CHECK(rejects(bad));cJSON_Delete(root);ed_document_destroy(&d);puts("Collider data: v4 defaults, all primitive outlines, clone/duplicate, undo, round trip, singular transforms and strict JSON passed.");return 0;
}
