/* SPDX-License-Identifier: MPL-2.0 */
#include <afterlight/prefab.h>
#include "scene_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static bool fail(af_scene_error *e,const char *text){if(e){e->code=AF_SCENE_ERROR_FORMAT;snprintf(e->message,sizeof(e->message),"%s",text);}return false;}
static af_object_id mapped(const af_scene *s,const af_object_id *ids,af_object_id old){if(!old)return 0;for(size_t i=0;i<s->count;++i)if(s->objects[i].id==old)return ids[i];return 0;}
static void exchange(af_scene *a,af_scene *b){af_scene old=*a;*a=*b;*b=old;}
static bool uses(const af_scene_object *o,const char *name){return !strcmp(o->mesh,name)||!strcmp(o->material.texture,name)||!strcmp(o->material.normal_map,name)||!strcmp(o->material.metallic_roughness_map,name)||!strcmp(o->animation.asset,name);}
bool af_prefab_save(const af_scene *s,af_object_id root,const char *source_path,const char *path,af_scene_error *e){
    if(e)*e=(af_scene_error){0};if(!s||!path||!af_scene_get_object(s,root))return fail(e,"Select a hierarchy to save.");af_scene *p=af_scene_clone(s);if(!p)return false;size_t count=0;
    for(size_t i=0;i<s->count;++i)if(s->objects[i].id==root||af_scene_is_descendant(s,s->objects[i].id,root)){af_scene_object o=s->objects[i];if(o.id==root)o.parent=0;p->objects[count++]=o;}p->count=count;if(count>4096){af_scene_destroy(p);return fail(e,"Prefab exceeds 4096 objects.");}
    for(size_t i=0;i<count;++i){af_scene_object *o=&p->objects[i];if(o->gameplay.player.spawn&&!af_scene_get_object(p,o->gameplay.player.spawn)){af_scene_destroy(p);return fail(e,"Prefab spawn must belong to the saved hierarchy.");}if(o->model.root&&!af_scene_get_object(p,o->model.root)){af_scene_destroy(p);return fail(e,"Save the complete imported model hierarchy.");}}
    p->asset_count=0;p->local_light_count=0;for(size_t i=0;i<s->asset_count;++i){bool used=false;for(size_t j=0;j<count;++j)used|=uses(&p->objects[j],s->assets[i].name);if(!used)continue;af_scene_asset a=s->assets[i];char resolved[4096];if(!af_asset_resolve_path(source_path,a.path,resolved,sizeof(resolved))){af_scene_destroy(p);return fail(e,"Cannot resolve prefab asset.");}snprintf(a.path,sizeof(a.path),"%s",resolved);p->assets[p->asset_count++]=a;}
    bool ok=af_scene_save(p,path,e);af_scene_destroy(p);return ok;
}
bool af_prefab_instantiate(af_scene *s,const char *path,af_object_id parent,af_transform placement,af_object_id *out,af_scene_error *e){
    if(e)*e=(af_scene_error){0};if(out)*out=0;if(!s||!path||(parent&&!af_scene_get_object(s,parent)))return fail(e,"Invalid prefab destination.");af_scene *p=af_scene_load(path,e);if(!p)return false;af_scene *next=af_scene_clone(s);af_object_id *ids=calloc(p->count?p->count:1,sizeof(*ids));char (*names)[64]=calloc(p->asset_count?p->asset_count:1,64);bool ok=false;af_object_id old_root=0,new_root=0;
    if(!next||!ids||!names||!p->count||p->count>4096||s->count>AF_SCENE_FILE_MAX_OBJECTS-p->count)goto done;
    for(size_t i=0;i<p->count;++i)if(!p->objects[i].parent){if(old_root)goto done;old_root=p->objects[i].id;}if(!old_root)goto done;
    for(size_t i=0;i<p->asset_count;++i){af_scene_asset a=p->assets[i];char absolute[4096];if(!af_asset_resolve_path(path,a.path,absolute,sizeof(absolute)))goto done;const af_scene_asset *same=NULL;
        for(size_t j=0;j<next->asset_count;++j)if(next->assets[j].kind==a.kind&&!strcmp(next->assets[j].path,absolute)){same=&next->assets[j];break;}
        if(same)snprintf(names[i],64,"%s",same->name);else{snprintf(names[i],64,"%s",a.name);for(unsigned serial=0;af_scene_find_asset(next,names[i]);++serial){if(serial>1024)goto done;snprintf(names[i],64,"pf%llu_%u_%.30s",(unsigned long long)next->next_id,serial,a.name);}if(!af_scene_set_asset(next,names[i],a.kind,absolute))goto done;}}
    for(size_t i=0;i<p->count;++i)if(!(ids[i]=af_scene_add_object(next,p->objects[i].name,p->objects[i].mesh)))goto done;
    for(size_t i=0;i<p->count;++i){af_scene_object o=p->objects[i];af_object_id old=o.id;o.id=ids[i];o.parent=old==old_root?parent:mapped(p,ids,o.parent);o.model.root=mapped(p,ids,o.model.root);o.gameplay.player.spawn=mapped(p,ids,o.gameplay.player.spawn);
        char *refs[]={o.mesh,o.material.texture,o.material.normal_map,o.material.metallic_roughness_map,o.animation.asset};for(unsigned ref=0;ref<5;++ref)for(size_t j=0;j<p->asset_count;++j)if(!strcmp(refs[ref],p->assets[j].name)){snprintf(refs[ref],64,"%s",names[j]);break;}
        if(old==old_root){o.transform=placement;new_root=o.id;}af_scene_object *target=(af_scene_object*)af_scene_get_object(next,o.id);*target=o;}
    if(!af_scene_set_object_transform(next,new_root,placement)||!af_scene_set_parent(next,new_root,parent))goto done;for(size_t i=0;i<p->count;++i){af_mat4 matrix;if(!af_scene_world_matrix(next,ids[i],&matrix))goto done;}
    exchange(s,next);if(out)*out=new_root;ok=true;
done:free(ids);free(names);af_scene_destroy(next);af_scene_destroy(p);return ok||fail(e,"Invalid prefab or instance limits exceeded; destination unchanged.");
}
