/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "document.h"
#include <afterlight/model.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *ed_path_name(const char *path)
{
    const char *name = path;
    for (const char *p = path; *p; ++p) if (*p == '/' || *p == '\\') name = p + 1;
    return name;
}
static void clear_history(ed_snapshot *history, size_t *count)
{
    for (size_t i = 0; i < *count; ++i) af_scene_destroy(history[i].scene);
    *count = 0;
}
static size_t snapshot_bytes(ed_snapshot snapshot)
{
    return af_scene_object_count(snapshot.scene) * sizeof(af_scene_object) + af_scene_asset_count(snapshot.scene)*sizeof(af_scene_asset) + sizeof(snapshot);
}
static void push_history(ed_snapshot *history, size_t *count, ed_snapshot snapshot)
{
    size_t bytes = snapshot_bytes(snapshot);
    for (size_t i = 0; i < *count; ++i) bytes += snapshot_bytes(history[i]);
    while (*count && (*count == ED_HISTORY_LIMIT || bytes > ED_HISTORY_BYTES)) {
        bytes -= snapshot_bytes(history[0]);
        af_scene_destroy(history[0].scene);
        memmove(history,history+1,(--*count)*sizeof(*history));
    }
    history[(*count)++] = snapshot;
}
static void status(ed_document *d, const char *text) { snprintf(d->status,sizeof(d->status),"%s",text); }
bool ed_document_init(ed_document *d)
{
    memset(d,0,sizeof(*d));
    d->scene = af_scene_create();
    d->revision = d->saved_revision = d->serial = 1;
    status(d,"Ready");
    return d->scene != NULL;
}
void ed_document_destroy(ed_document *d)
{
    af_scene_destroy(d->scene);
    clear_history(d->undo,&d->undo_count); clear_history(d->redo,&d->redo_count);
    memset(d,0,sizeof(*d));
}
bool ed_document_dirty(const ed_document *d) { return d->revision != d->saved_revision; }
void ed_document_end_edit(ed_document *d) { d->group_kind = 0; d->group_object = AF_OBJECT_NONE; }
static void replace_document(ed_document *d, af_scene *scene, const char *path)
{
    af_scene_destroy(d->scene);
    clear_history(d->undo,&d->undo_count); clear_history(d->redo,&d->redo_count);
    d->scene = scene;
    const af_scene_object *first = af_scene_object_at(scene,0);
    d->selection = first ? first->id : AF_OBJECT_NONE;
    d->revision = d->saved_revision = ++d->serial;
    snprintf(d->path,sizeof(d->path),"%s",path);
    ed_document_end_edit(d);
}
bool ed_document_load(ed_document *d, const char *path, bool as_untitled)
{
    if (!path || strlen(path) >= sizeof(d->path)) { status(d,"Scene path is too long."); return false; }
    af_scene_error error;
    af_scene *scene = af_scene_load(path,&error);
    if (!scene) { status(d,error.message); return false; }
    char copied[ED_PATH_CAPACITY]; snprintf(copied,sizeof(copied),"%s",as_untitled ? "" : path);
    replace_document(d,scene,copied);
    status(d,as_untitled ? "Demo scene opened. Save As to create your own scene." : "Scene opened.");
    return true;
}
bool ed_document_save(ed_document *d, const char *path)
{
    if (!path || !path[0] || strlen(path) >= sizeof(d->path)) { status(d,"Choose a valid scene path."); return false; }
    char copied[ED_PATH_CAPACITY]; snprintf(copied,sizeof(copied),"%s",path);
    af_scene_error error;
    if (!af_scene_save(d->scene,copied,&error)) { status(d,error.message); return false; }
    snprintf(d->path,sizeof(d->path),"%s",copied);
    d->saved_revision = d->revision;
    ed_document_end_edit(d);
    status(d,"Scene saved.");
    return true;
}
bool ed_document_new(ed_document *d)
{
    af_scene *scene = af_scene_create();
    if (!scene) { status(d,"Not enough memory to create a scene."); return false; }
    replace_document(d,scene,""); status(d,"New scene. Add a cube to begin."); return true;
}
static bool commit(ed_document *d, af_scene *next, af_object_id selection, int kind)
{
    if (!next) { status(d,"Not enough memory to edit this scene."); return false; }
    bool merge = kind != 0 && d->group_kind == kind && d->group_object == d->selection;
    ed_snapshot old = {d->scene,d->selection,d->revision};
    clear_history(d->redo,&d->redo_count);
    if (merge) af_scene_destroy(d->scene);
    else push_history(d->undo,&d->undo_count,old);
    d->scene = next; d->selection = selection; d->revision = ++d->serial;
    d->group_kind = kind; d->group_object = selection;
    status(d,"Unsaved changes");
    return true;
}
static bool same_vector(af_vec3 a, af_vec3 b) { return a.x==b.x && a.y==b.y && a.z==b.z; }
bool ed_document_transform(ed_document *d, af_transform transform)
{
    const af_scene_object *object = af_scene_get_object(d->scene,d->selection);
    if (!object) return false;
    if (same_vector(object->transform.position,transform.position) && same_vector(object->transform.rotation,transform.rotation) &&
        same_vector(object->transform.scale,transform.scale)) return true;
    af_scene *next = af_scene_clone(d->scene);
    if (!next) return commit(d,NULL,0,0);
    if (!af_scene_set_object_transform(next,d->selection,transform)) { af_scene_destroy(next); status(d,"Transform values must be finite."); return false; }
    return commit(d,next,d->selection,1);
}
bool ed_document_name(ed_document *d, const char *name)
{
    const af_scene_object *object = af_scene_get_object(d->scene,d->selection);
    if (!object || !name) return false;
    if (!strcmp(object->name,name)) return true;
    af_scene *next = af_scene_clone(d->scene);
    if (!next) return commit(d,NULL,0,0);
    if (!af_scene_set_object_name(next,d->selection,name)) { af_scene_destroy(next); status(d,"Name must contain 1 to 63 UTF-8 bytes."); return false; }
    return commit(d,next,d->selection,2);
}
bool ed_document_mesh(ed_document *d, const char *mesh)
{
    const af_scene_object *object = af_scene_get_object(d->scene,d->selection);
    if (!object || !mesh) return false;
    if (!strcmp(object->mesh,mesh)) return true;
    af_scene *next = af_scene_clone(d->scene);
    if (!next) return commit(d,NULL,0,0);
    if (!af_scene_set_object_mesh(next,d->selection,mesh)) { af_scene_destroy(next); status(d,"Mesh name must contain 1 to 63 bytes."); return false; }
    return commit(d,next,d->selection,3);
}
bool ed_document_visible(ed_document *d, bool visible)
{
    const af_scene_object *object = af_scene_get_object(d->scene,d->selection);
    if (!object) return false;
    if (object->visible == visible) return true;
    af_scene *next = af_scene_clone(d->scene);
    if (!next) return commit(d,NULL,0,0);
    if (!af_scene_set_object_visible(next,d->selection,visible)) { af_scene_destroy(next); return false; }
    return commit(d,next,d->selection,0);
}
bool ed_document_add_cube(ed_document *d)
{
    if (af_scene_object_count(d->scene) >= AF_SCENE_FILE_MAX_OBJECTS) { status(d,"Scene object limit reached."); return false; }
    af_scene *next = af_scene_clone(d->scene);
    if (!next) return commit(d,NULL,0,0);
    af_object_id id = af_scene_add_object(next,"Cube","cube");
    if (!id) { af_scene_destroy(next); status(d,"Cannot allocate another object."); return false; }
    return commit(d,next,id,0);
}
bool ed_document_duplicate(ed_document *d)
{
    const af_scene_object *object = af_scene_get_object(d->scene,d->selection);
    if (!object || af_scene_object_count(d->scene) >= AF_SCENE_FILE_MAX_OBJECTS) return false;
    af_object_id root=object->id;size_t count=af_scene_object_count(d->scene),duplicates=0;
    af_object_id *ids=calloc(count,sizeof(*ids));if(!ids)return commit(d,NULL,0,0);
    af_scene *next=af_scene_clone(d->scene);if(!next){free(ids);return commit(d,NULL,0,0);}
    af_object_id selected=0;
    for(size_t i=0;i<count;++i){af_scene_object source=*af_scene_object_at(d->scene,i);
        if(source.id!=root&&!af_scene_is_descendant(d->scene,source.id,root))continue;
        if(count+(++duplicates)>AF_SCENE_FILE_MAX_OBJECTS)goto duplicate_failed;
        ids[i]=af_scene_add_object(next,source.name,source.mesh);if(source.id==root){selected=ids[i];source.transform.position.x+=1.5f;}
        if(!ids[i]||!af_scene_set_object_transform(next,ids[i],source.transform)||!af_scene_set_object_visible(next,ids[i],source.visible)||!af_scene_set_object_material(next,ids[i],source.material)||!af_scene_set_object_collider(next,ids[i],source.collider)||!af_scene_set_object_gameplay(next,ids[i],source.gameplay))goto duplicate_failed;
    }
    for(size_t i=0;i<count;++i)if(ids[i]){const af_scene_object *source=af_scene_object_at(d->scene,i);af_object_id parent=source->parent;
        if(source->id!=root)for(size_t j=0;j<count;++j)if(af_scene_object_at(d->scene,j)->id==parent){parent=ids[j];break;}
        if(!af_scene_set_parent(next,ids[i],parent))goto duplicate_failed;
        af_gameplay_component gameplay=af_scene_get_object(next,ids[i])->gameplay;
        for(size_t j=0;j<count;++j)if(ids[j]&&af_scene_object_at(d->scene,j)->id==gameplay.player.spawn){gameplay.player.spawn=ids[j];break;}
        if(!af_scene_set_object_gameplay(next,ids[i],gameplay))goto duplicate_failed;
    }
    free(ids);return commit(d,next,selected,0);
duplicate_failed:
    free(ids);af_scene_destroy(next);status(d,"Cannot duplicate this subtree.");return false;
}
bool ed_document_delete(ed_document *d)
{
    if (!af_scene_get_object(d->scene,d->selection)) return false;
    af_scene *next = af_scene_clone(d->scene);
    if (!next) return commit(d,NULL,0,0);
    if (!af_scene_remove_object(next,d->selection)) { af_scene_destroy(next); return false; }
    const af_scene_object *first = af_scene_object_at(next,0);
    return commit(d,next,first ? first->id : AF_OBJECT_NONE,0);
}
bool ed_document_undo(ed_document *d)
{
    if (!d->undo_count) return false;
    push_history(d->redo,&d->redo_count,(ed_snapshot){d->scene,d->selection,d->revision});
    ed_snapshot previous = d->undo[--d->undo_count];
    d->scene=previous.scene; d->selection=previous.selection; d->revision=previous.revision;
    ed_document_end_edit(d); status(d,"Undo"); return true;
}
bool ed_document_redo(ed_document *d)
{
    if (!d->redo_count) return false;
    push_history(d->undo,&d->undo_count,(ed_snapshot){d->scene,d->selection,d->revision});
    ed_snapshot next = d->redo[--d->redo_count];
    d->scene=next.scene; d->selection=next.selection; d->revision=next.revision;
    ed_document_end_edit(d); status(d,"Redo"); return true;
}

bool ed_document_import_mesh(ed_document *d,const char *name,const char *path){
    af_scene *next=af_scene_clone(d->scene);if(!next)return commit(d,NULL,0,0);
    if(af_asset_is_model_path(path)){af_object_id root;af_asset_error error;
        if(!af_scene_import_model(next,name,path,&root,&error)){af_scene_destroy(next);status(d,error.message);return false;}return commit(d,next,root,0);}
    if(!af_scene_set_asset(next,name,AF_ASSET_MESH,path)){af_scene_destroy(next);status(d,"Cannot add mesh reference.");return false;}
    af_object_id id=af_scene_add_object(next,ed_path_name(path),name);
    if(!id){af_scene_destroy(next);return false;}return commit(d,next,id,0);
}

bool ed_document_material(ed_document *d,af_material material){
    const af_scene_object *o=af_scene_get_object(d->scene,d->selection);if(!o)return false;
    const af_material *a=&o->material;
    if(a->base_color.r==material.base_color.r&&a->base_color.g==material.base_color.g&&a->base_color.b==material.base_color.b&&a->base_color.a==material.base_color.a&&a->unlit==material.unlit&&a->cast_shadows==material.cast_shadows&&a->receive_shadows==material.receive_shadows&&a->physically_based==material.physically_based&&a->roughness==material.roughness&&a->metallic==material.metallic&&same_vector(a->emission,material.emission)&&a->emission_strength==material.emission_strength&&a->normal_strength==material.normal_strength&&!strcmp(a->metallic_roughness_map,material.metallic_roughness_map)&&!strcmp(a->normal_map,material.normal_map)&&!strcmp(a->texture,material.texture))return true;
    af_scene *next=af_scene_clone(d->scene);if(!next)return commit(d,NULL,0,0);
    if(!af_scene_set_object_material(next,d->selection,material)){af_scene_destroy(next);status(d,"Invalid material values.");return false;}
    return commit(d,next,d->selection,4);
}
bool ed_document_light(ed_document *d,af_directional_light light){
    af_scene *next=af_scene_clone(d->scene);if(!next)return commit(d,NULL,0,0);
    if(!af_scene_set_light(next,light)){af_scene_destroy(next);status(d,"Light direction must be nonzero and values within range.");return false;}
    return commit(d,next,d->selection,5);
}
bool ed_document_import_texture(ed_document *d,const char *name,const char *path){
    const af_scene_object *o=af_scene_get_object(d->scene,d->selection);af_material material=o?o->material:af_material_default();
    if(!name||strlen(name)>=sizeof(material.texture))return false;snprintf(material.texture,sizeof(material.texture),"%s",name);
    af_scene *next=af_scene_clone(d->scene);if(!next)return commit(d,NULL,0,0);
    if(!af_scene_set_asset(next,name,AF_ASSET_TEXTURE,path)||(o&&!af_scene_set_object_material(next,d->selection,material))){af_scene_destroy(next);return false;}
    return commit(d,next,d->selection,0);
}

bool ed_document_parent(ed_document *d,af_object_id parent){
    const af_scene_object *o=af_scene_get_object(d->scene,d->selection);if(!o)return false;if(o->parent==parent)return true;
    af_scene *next=af_scene_clone(d->scene);if(!next)return commit(d,NULL,0,0);
    if(!af_scene_set_parent(next,d->selection,parent)){af_scene_destroy(next);status(d,"Parent would create a cycle or excessive depth.");return false;}
    return commit(d,next,d->selection,0);
}

bool ed_document_shadows(ed_document *d,af_shadow_settings settings){
    af_scene *next=af_scene_clone(d->scene);if(!next)return commit(d,NULL,0,0);
    if(!af_scene_set_shadows(next,settings)){af_scene_destroy(next);status(d,"Invalid shadow settings.");return false;}
    return commit(d,next,d->selection,6);
}

bool ed_document_import_normal_map(ed_document *d,const char *name,const char *path){
    const af_scene_object *o=af_scene_get_object(d->scene,d->selection);
    if(!name||strlen(name)>=AF_MESH_NAME_CAPACITY)return false;
    af_material material=o?o->material:af_material_default();snprintf(material.normal_map,sizeof(material.normal_map),"%s",name);
    af_scene *next=af_scene_clone(d->scene);if(!next)return commit(d,NULL,0,0);
    if(!af_scene_set_asset(next,name,AF_ASSET_NORMAL_MAP,path)||(o&&!af_scene_set_object_material(next,d->selection,material))){af_scene_destroy(next);return false;}
    return commit(d,next,d->selection,0);
}

bool ed_document_render_settings(ed_document *d,af_render_settings settings){
    af_render_settings old=af_scene_get_render_settings(d->scene);if(old.exposure==settings.exposure&&old.tone_mapping==settings.tone_mapping)return true;
    af_scene *next=af_scene_clone(d->scene);if(!next)return commit(d,NULL,0,0);
    if(!af_scene_set_render_settings(next,settings)){af_scene_destroy(next);return false;}return commit(d,next,d->selection,7);
}

bool ed_document_add_light(ed_document *d,af_light_type type){
    af_scene *next=af_scene_clone(d->scene);if(!next)return commit(d,NULL,0,0);
    if(!af_scene_add_local_light(next,af_local_light_default(type))){af_scene_destroy(next);status(d,"Local light limit reached.");return false;}
    return commit(d,next,d->selection,0);
}
bool ed_document_local_light(ed_document *d,size_t index,af_local_light light){
    af_scene *next=af_scene_clone(d->scene);if(!next)return commit(d,NULL,0,0);
    if(!af_scene_set_local_light(next,index,light)){af_scene_destroy(next);status(d,"Invalid light: check name, direction, range and cone angles.");return false;}
    return commit(d,next,d->selection,8+(int)index);
}
bool ed_document_remove_light(ed_document *d,size_t index){
    af_scene *next=af_scene_clone(d->scene);if(!next)return commit(d,NULL,0,0);
    if(!af_scene_remove_local_light(next,index)){af_scene_destroy(next);return false;}return commit(d,next,d->selection,0);
}

bool ed_document_create_mesh_object(ed_document *d,const char *mesh){
    if(!mesh)return false;const af_scene_asset *asset=af_scene_find_asset(d->scene,mesh);
    if((!asset&&strcmp(mesh,"cube"))||(asset&&asset->kind!=AF_ASSET_MESH)||af_scene_object_count(d->scene)>=AF_SCENE_FILE_MAX_OBJECTS)return false;
    af_scene *next=af_scene_clone(d->scene);if(!next)return commit(d,NULL,0,0);
    af_object_id id=af_scene_add_object(next,mesh,mesh);if(!id){af_scene_destroy(next);return false;}return commit(d,next,id,0);
}

bool ed_document_collider(ed_document *d,af_collider collider){
    const af_scene_object *o=af_scene_get_object(d->scene,d->selection);if(!o)return false;
    af_scene *next=af_scene_clone(d->scene);if(!next)return commit(d,NULL,0,0);
    if(!af_scene_set_object_collider(next,d->selection,collider)){af_scene_destroy(next);status(d,"Invalid collider dimensions or layer.");return false;}return commit(d,next,d->selection,30);
}

bool ed_document_gameplay(ed_document *d,af_gameplay_component component){
    const af_scene_object *object=af_scene_get_object(d->scene,d->selection);if(!object)return false;
    if(!memcmp(&object->gameplay,&component,sizeof(component)))return true;
    af_scene *next=af_scene_clone(d->scene);if(!next)return commit(d,NULL,0,0);
    if(!af_scene_set_object_gameplay(next,d->selection,component)){af_scene_destroy(next);status(d,"Invalid player/camera settings.");return false;}
    if(component.camera.enabled&&component.camera.primary)for(size_t i=0;i<af_scene_object_count(next);++i){const af_scene_object *other=af_scene_object_at(next,i);if(other->id!=d->selection&&other->gameplay.camera.primary){af_gameplay_component value=other->gameplay;value.camera.primary=false;af_scene_set_object_gameplay(next,other->id,value);}}
    if(component.player.enabled){af_collider collider=object->collider;collider.type=AF_COLLIDER_CAPSULE;collider.enabled=true;collider.trigger=false;collider.center=(af_vec3){0};collider.radius=component.player.controller.radius;collider.half_height=component.player.controller.half_height;
        if(!af_scene_set_object_collider(next,d->selection,collider)){af_scene_destroy(next);return false;}}
    return commit(d,next,d->selection,31);
}
bool ed_document_import_sound(ed_document *d,const char *name,const char *path){
    af_scene *next=af_scene_clone(d->scene);if(!next)return commit(d,NULL,0,0);
    if(!af_scene_set_asset(next,name,AF_ASSET_AUDIO,path)){af_scene_destroy(next);return false;}return commit(d,next,d->selection,0);
}

bool ed_document_import_surface(ed_document *d,const char *name,const char *path){
    if(!name||strlen(name)>=AF_MESH_NAME_CAPACITY)return false;af_scene *next=af_scene_clone(d->scene);if(!next)return commit(d,NULL,0,0);
    if(!af_scene_set_asset(next,name,AF_ASSET_METALLIC_ROUGHNESS,path)){af_scene_destroy(next);return false;}
    const af_scene_object *object=af_scene_get_object(next,d->selection);if(object){af_material m=object->material;snprintf(m.metallic_roughness_map,sizeof(m.metallic_roughness_map),"%s",name);if(!af_scene_set_object_material(next,object->id,m)){af_scene_destroy(next);return false;}}
    return commit(d,next,d->selection,0);
}
