/* SPDX-License-Identifier: MPL-2.0 */
#include "animation_runtime.h"
#include "scene_internal.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RUNTIME_ASSETS 32u
#define RUNTIME_INSTANCES 128u
#define RUNTIME_NODES 65536u
#define RUNTIME_BYTES (256u*1024u*1024u)
typedef struct animation_asset {char name[64];af_animation_file file;size_t bytes;uint64_t generation;} animation_asset;
typedef struct animation_instance {
    af_object_id root;uint64_t generation;af_animation_component configuration;
    af_locomotion_player locomotion;bool driven;af_animation_events events;uint64_t emitted;char last_event[32];
    af_animation_player player;af_animation_pose *poses;af_object_id *nodes;size_t node_count;
} animation_instance;
typedef struct object_slot {af_object_id id;size_t index;} object_slot;
struct af_animation_runtime {
    af_renderer *renderer;const af_scene *scene;
    animation_asset assets[RUNTIME_ASSETS];size_t asset_count,bytes;uint64_t serial;
    animation_instance instances[RUNTIME_INSTANCES];size_t instance_count,total_nodes;
    object_slot *index;size_t index_capacity;
    af_mat4 *local,*world;unsigned char *ready;size_t capacity,world_count;
    af_object_id palette_objects[RUNTIME_INSTANCES];size_t palette_count;
};
static animation_asset *asset(af_animation_runtime *r,const char *name){for(size_t i=0;i<r->asset_count;++i)if(!strcmp(r->assets[i].name,name))return &r->assets[i];return NULL;}
static void free_instance(animation_instance *i){af_animation_player_destroy(&i->player);af_locomotion_player_destroy(&i->locomotion);free(i->poses);free(i->nodes);*i=(animation_instance){0};}
static void clear_instances(af_animation_runtime *r){for(size_t i=0;i<r->instance_count;++i)free_instance(&r->instances[i]);r->instance_count=r->total_nodes=0;r->world_count=0;}
af_animation_runtime *af_animation_runtime_create(af_renderer *renderer){af_animation_runtime *r=calloc(1,sizeof(*r));if(r)r->renderer=renderer;return r;}
void af_animation_runtime_destroy(af_animation_runtime *r){if(!r)return;clear_instances(r);for(size_t i=0;i<r->asset_count;++i)af_animation_file_free(&r->assets[i].file);free(r->index);free(r->local);free(r->world);free(r->ready);free(r);}
void af_animation_runtime_scene(af_animation_runtime *r,const af_scene *scene){if(r&&r->scene!=scene){clear_instances(r);r->scene=scene;r->palette_count=0;af_renderer_set_object_worlds(r->renderer,NULL,NULL,0);}}
static size_t file_bytes(const af_animation_file *f){size_t bytes=f->node_count*(sizeof(af_animation_pose)+sizeof(int32_t))+f->skin_count*sizeof(af_animation_skin);
    for(size_t i=0;i<f->clip_count;++i){bytes+=sizeof(af_animation_clip)+f->clips[i].channel_count*sizeof(af_animation_channel);for(size_t j=0;j<f->clips[i].channel_count;++j){const af_animation_channel *c=&f->clips[i].channels[j];bytes+=c->key_count*(sizeof(float)+(c->interpolation==AF_ANIM_CUBIC?3u:1u)*sizeof(af_vec4));}}return bytes;}
bool af_animation_runtime_load(af_animation_runtime *r,const char *name,const char *path,af_asset_error *error){
    if(error)*error=(af_asset_error){0};if(!r||!name||!name[0]||strlen(name)>=64){if(error)snprintf(error->message,sizeof(error->message),"An animation name of 1–63 bytes is required.");return false;}animation_asset *old=asset(r,name);if(!old&&r->asset_count==RUNTIME_ASSETS){if(error)snprintf(error->message,sizeof(error->message),"Animation library exceeds 32 resident assets (including staged reloads).");return false;}
    af_animation_file file;if(!af_animation_file_load(path,&file,error))return false;size_t bytes=file_bytes(&file),retained=r->bytes-(old?old->bytes:0);
    if(bytes>RUNTIME_BYTES||retained>RUNTIME_BYTES-bytes){af_animation_file_free(&file);if(error)snprintf(error->message,sizeof(error->message),"Animation library exceeds 256 MiB.");return false;}
    if(old)af_animation_file_free(&old->file);else old=&r->assets[r->asset_count++];*old=(animation_asset){.file=file,.bytes=bytes,.generation=++r->serial};snprintf(old->name,sizeof(old->name),"%s",name);r->bytes=retained+bytes;return true;
}
bool af_animation_runtime_commit(af_animation_runtime *r,const char *staged,const char *target){
    animation_asset *from=asset(r,staged),*to=asset(r,target);if(!from||strlen(target)>=64)return false;if(to){animation_asset swap=*to;*to=*from;*from=swap;snprintf(from->name,sizeof(from->name),"%s",staged);}else to=from;snprintf(to->name,sizeof(to->name),"%s",target);return true;
}
void af_animation_runtime_remove(af_animation_runtime *r,const char *name){for(size_t i=0;i<r->asset_count;++i)if(!strcmp(r->assets[i].name,name)){r->bytes-=r->assets[i].bytes;af_animation_file_free(&r->assets[i].file);r->assets[i]=r->assets[--r->asset_count];return;}}
af_object_id af_animation_runtime_root(const af_animation_runtime *r,af_object_id object){const af_scene_object *o=af_scene_get_object(r?r->scene:NULL,object);return o?(o->animation.asset[0]?o->id:o->model.root):0;}
static animation_instance *instance(af_animation_runtime *r,af_object_id object){
    af_object_id root=af_animation_runtime_root(r,object);const af_scene_object *o=af_scene_get_object(r->scene,root);if(!o||!o->animation.asset[0])return NULL;animation_asset *a=asset(r,o->animation.asset);if(!a)return NULL;
    size_t at=0;while(at<r->instance_count&&r->instances[at].root!=root)++at;if(at==RUNTIME_INSTANCES)return NULL;
    animation_instance *i=&r->instances[at];bool initialize=at==r->instance_count||i->generation!=a->generation||strcmp(i->configuration.asset,o->animation.asset);
    if(initialize){size_t retained=r->total_nodes-(at<r->instance_count?i->node_count:0);if(a->file.node_count>RUNTIME_NODES-retained)return NULL;
        af_animation_pose *poses=calloc(a->file.node_count,sizeof(*poses));af_object_id *nodes=calloc(a->file.node_count,sizeof(*nodes));if(!poses||!nodes){free(poses);free(nodes);return NULL;}
        if(at<r->instance_count)free_instance(i);else ++r->instance_count;*i=(animation_instance){.root=root,.generation=a->generation,.poses=poses,.nodes=nodes,.node_count=a->file.node_count,.player=af_animation_player_default()};r->total_nodes=retained+a->file.node_count;
        i->player.speed=o->animation.speed;i->player.loop=o->animation.loop;if(a->file.clip_count){af_animation_player_play(&i->player,&a->file,o->animation.clip<a->file.clip_count?o->animation.clip:0,0);af_animation_player_pause(&i->player,!o->animation.autoplay);}
    }else{
        if(i->configuration.speed!=o->animation.speed)i->player.speed=o->animation.speed;if(i->configuration.loop!=o->animation.loop)i->player.loop=o->animation.loop;
        if(i->configuration.clip!=o->animation.clip&&o->animation.clip<a->file.clip_count)af_animation_player_play(&i->player,&a->file,o->animation.clip,0);
        if(i->configuration.autoplay!=o->animation.autoplay)af_animation_player_pause(&i->player,!o->animation.autoplay);
    }i->configuration=o->animation;return i;
}
af_animation_status af_animation_runtime_status(af_animation_runtime *r,af_object_id object){
    animation_instance *i=instance(r,object);if(!i)return (af_animation_status){0};animation_asset *a=asset(r,i->configuration.asset);af_animation_player *p=&i->player;
    af_animation_status s={.available=true,.playing=p->playing,.loop=p->loop,.finished=p->finished,.fading=p->fading,.clip=(uint32_t)p->clip,.clip_count=a->file.clip_count,.time=p->time,.speed=p->speed,.blend=p->fading?p->fade_elapsed/p->fade_duration:1};
    if(i->driven){const af_locomotion *m=&i->locomotion.motion;s.clip=(uint32_t)i->locomotion.clips[m->state];s.playing=true;s.fading=m->transition<1;s.blend=m->transition;s.duration=a->file.clips[s.clip].duration;s.time=m->state<=AF_LOCOMOTION_RUN?m->phase*s.duration:m->time;snprintf(s.name,sizeof(s.name),"%s",af_locomotion_state_name(m->state));return s;}
    if(p->clip<a->file.clip_count){s.duration=a->file.clips[p->clip].duration;snprintf(s.name,sizeof(s.name),"%s",a->file.clips[p->clip].name);}return s;
}
bool af_animation_runtime_clip(af_animation_runtime *r,af_object_id object,size_t clip,af_animation_clip_info *info){animation_instance *i=instance(r,object);animation_asset *a=i?asset(r,i->configuration.asset):NULL;if(!a||!info||clip>=a->file.clip_count)return false;snprintf(info->name,sizeof(info->name),"%s",a->file.clips[clip].name);info->duration=a->file.clips[clip].duration;return true;}
static void queue_events(animation_instance *i,const af_animation_events *events){i->events.dropped+=events->dropped;i->emitted+=events->count+events->dropped;if(events->count)snprintf(i->last_event,sizeof(i->last_event),"%s",events->items[events->count-1].name);for(size_t j=0;j<events->count;++j){if(i->events.count<AF_ANIMATION_EVENT_BATCH)i->events.items[i->events.count++]=events->items[j];else ++i->events.dropped;}}
bool af_animation_runtime_events(af_animation_runtime *r,af_object_id object,af_animation_events *out){animation_instance *i=instance(r,object);if(!i||!out)return false;*out=i->events;i->events=(af_animation_events){0};return true;}
bool af_animation_runtime_root_motion(af_animation_runtime *r,af_object_id object,uint32_t node,bool enabled){
    animation_instance *i=instance(r,object);if(!i)return false;animation_asset *a=asset(r,i->configuration.asset);if(node>=a->file.node_count)return false;
    if(!i->locomotion.ready){const af_scene_object *root=af_scene_get_object(r->scene,i->root);if(!(root->rig.enabled?af_locomotion_player_init_rig(&i->locomotion,&a->file,root->rig):af_locomotion_player_init(&i->locomotion,&a->file)))return false;}i->locomotion.root_node=node;i->locomotion.root_motion=enabled;return true;
}
bool af_animation_runtime_root_delta(af_animation_runtime *r,af_object_id object,af_vec3 *out){animation_instance *i=instance(r,object);if(!i||!out||!i->driven)return false;*out=i->locomotion.root_delta;return true;}
bool af_animation_runtime_locomotion(af_animation_runtime *r,af_object_id object,af_locomotion_input input,float dt,af_locomotion *status){
    animation_instance *i=instance(r,object);if(!i)return false;animation_asset *a=asset(r,i->configuration.asset);
    if(!i->locomotion.ready){const af_scene_object *root=af_scene_get_object(r->scene,i->root);if(!(root->rig.enabled?af_locomotion_player_init_rig(&i->locomotion,&a->file,root->rig):af_locomotion_player_init(&i->locomotion,&a->file)))return false;}
    if(!af_locomotion_player_step(&i->locomotion,&a->file,input,dt))return false;i->driven=true;queue_events(i,&i->locomotion.events);if(status)*status=i->locomotion.motion;return true;
}
bool af_animation_runtime_play(af_animation_runtime *r,af_object_id object,size_t clip,float blend){animation_instance *i=instance(r,object);if(i){i->driven=false;i->events=(af_animation_events){0};}return i&&af_animation_player_play(&i->player,&asset(r,i->configuration.asset)->file,clip,blend);}
bool af_animation_runtime_pause(af_animation_runtime *r,af_object_id object,bool paused){animation_instance *i=instance(r,object);if(!i)return false;af_animation_player_pause(&i->player,paused);return true;}
bool af_animation_runtime_stop(af_animation_runtime *r,af_object_id object){animation_instance *i=instance(r,object);if(!i)return false;i->driven=false;i->events=(af_animation_events){0};af_animation_player_stop(&i->player);return true;}
bool af_animation_runtime_seek(af_animation_runtime *r,af_object_id object,double seconds){animation_instance *i=instance(r,object);if(i){i->driven=false;i->events=(af_animation_events){0};}return i&&af_animation_player_seek(&i->player,&asset(r,i->configuration.asset)->file,seconds);}
bool af_animation_runtime_options(af_animation_runtime *r,af_object_id object,float speed,bool loop){animation_instance *i=instance(r,object);if(!i||!isfinite(speed)||fabsf(speed)>100)return false;i->player.speed=speed;i->player.loop=loop;return true;}
static animation_instance *existing_instance(af_animation_runtime *r,af_object_id root){for(size_t i=0;i<r->instance_count;++i)if(r->instances[i].root==root)return &r->instances[i];return NULL;}
static size_t hash_id(af_object_id id,size_t capacity){return (size_t)((id*UINT64_C(11400714819323198485))^(id>>32))&(capacity-1);}
static bool build_index(af_animation_runtime *r){size_t capacity=16;while(capacity<r->scene->count*2)capacity*=2;if(capacity>r->index_capacity){object_slot *slots=calloc(capacity,sizeof(*slots));if(!slots)return false;free(r->index);r->index=slots;r->index_capacity=capacity;}memset(r->index,0,r->index_capacity*sizeof(*r->index));
    for(size_t i=0;i<r->scene->count;++i){af_object_id id=r->scene->objects[i].id;size_t at=hash_id(id,r->index_capacity);while(r->index[at].id)at=(at+1)&(r->index_capacity-1);r->index[at]=(object_slot){id,i};}return true;}
static size_t object_index(const af_animation_runtime *r,af_object_id id){if(!id||!r->index_capacity)return r->scene->count;size_t at=hash_id(id,r->index_capacity);while(r->index[at].id){if(r->index[at].id==id)return r->index[at].index;at=(at+1)&(r->index_capacity-1);}return r->scene->count;}
static bool world(af_animation_runtime *r,size_t at,unsigned depth){
    if(r->ready[at])return true;if(depth>=AF_SCENE_MAX_DEPTH)return false;af_mat4 m=r->local[at];af_object_id parent=r->scene->objects[at].parent;
    if(parent){size_t p=object_index(r,parent);if(p==r->scene->count||!world(r,p,depth+1))return false;m=af_mat4_mul(r->world[p],m);}for(unsigned j=0;j<16;++j)if(!isfinite(m.elements[j]))return false;r->world[at]=m;r->ready[at]=1;return true;
}
bool af_animation_runtime_world(const af_animation_runtime *r,af_object_id object,af_mat4 *out){if(!r||!r->scene||!out||r->world_count!=r->scene->count)return false;size_t at=object_index(r,object);if(at>=r->world_count)return false;*out=r->world[at];return true;}
bool af_animation_runtime_update(af_animation_runtime *r,const af_scene *previous,float alpha,double delta,bool animate){
    if(!r)return true;
    for(size_t j=r->instance_count;j>0;--j){animation_instance *i=&r->instances[j-1];const af_scene_object *o=af_scene_get_object(r->scene,i->root);if(!o||!o->animation.asset[0]||!asset(r,o->animation.asset)){r->total_nodes-=i->node_count;free_instance(i);*i=r->instances[--r->instance_count];}}
    for(size_t j=0;j<af_scene_object_count(r->scene);++j)if(r->scene->objects[j].animation.asset[0])(void)instance(r,r->scene->objects[j].id);
    if(!r->instance_count){for(size_t j=0;j<r->palette_count;++j)af_renderer_set_skin_palette(r->renderer,r->palette_objects[j],NULL,0);r->palette_count=r->world_count=0;af_renderer_set_object_worlds(r->renderer,NULL,NULL,0);return true;}
    size_t count=r->scene->count;if(!build_index(r))return false;if(count>r->capacity){af_mat4 *local=calloc(count,sizeof(*local)),*worlds=calloc(count,sizeof(*worlds));unsigned char *ready=calloc(count,1);if(!local||!worlds||!ready){free(local);free(worlds);free(ready);return false;}free(r->local);free(r->world);free(r->ready);r->local=local;r->world=worlds;r->ready=ready;r->capacity=count;}memset(r->ready,0,count);
    for(size_t j=0;j<r->instance_count;++j){animation_instance *i=&r->instances[j];animation_asset *a=asset(r,i->configuration.asset);if(!a||i->generation!=a->generation||i->node_count!=a->file.node_count)continue;memset(i->nodes,0,i->node_count*sizeof(*i->nodes));
        if(animate&&!i->driven&&i->player.playing&&i->player.clip<a->file.clip_count){af_animation_events events;if(af_animation_collect_events(&a->file.clips[i->player.clip],i->player.time,delta*i->player.speed,i->player.loop,&events))queue_events(i,&events);}
        if(!animate||!a->file.clip_count)memcpy(i->poses,a->file.bind_pose,i->node_count*sizeof(*i->poses));else if(i->driven){if(!af_locomotion_player_sample(&i->locomotion,&a->file,i->poses,i->node_count))return false;}else if(!af_animation_player_update(&i->player,&a->file,delta)||!af_animation_player_sample(&i->player,&a->file,i->poses,i->node_count))memcpy(i->poses,a->file.bind_pose,i->node_count*sizeof(*i->poses));}
    for(size_t j=0;j<count;++j){const af_scene_object *o=&r->scene->objects[j],*old=af_scene_get_object(previous,o->id);af_transform t=o->transform;
        if(old&&old->parent==o->parent){t.position=af_vec3_add(old->transform.position,af_vec3_scale(af_vec3_sub(t.position,old->transform.position),alpha));t.scale=af_vec3_add(old->transform.scale,af_vec3_scale(af_vec3_sub(t.scale,old->transform.scale),alpha));t.rotation=(af_vec3){old->transform.rotation.x+remainderf(t.rotation.x-old->transform.rotation.x,2*AF_PI)*alpha,old->transform.rotation.y+remainderf(t.rotation.y-old->transform.rotation.y,2*AF_PI)*alpha,old->transform.rotation.z+remainderf(t.rotation.z-old->transform.rotation.z,2*AF_PI)*alpha};}
        r->local[j]=af_transform_matrix(t);if(old&&old->parent==o->parent&&o->body.enabled&&o->body.type==AF_BODY_DYNAMIC)r->local[j]=af_animation_pose_matrix((af_animation_pose){.translation=t.position,.scale=t.scale,.rotation=af_quaternion_slerp(af_quaternion_from_euler(old->transform.rotation),af_quaternion_from_euler(o->transform.rotation),alpha)});if(o->model.root&&!o->model.primitive){animation_instance *i=existing_instance(r,o->model.root);if(i&&o->model.node<i->node_count){i->nodes[o->model.node]=o->id;af_mat4 inverse;animation_asset *a=asset(r,i->configuration.asset);if(af_mat4_inverse(af_animation_pose_matrix(a->file.bind_pose[o->model.node]),&inverse))r->local[j]=af_mat4_mul(r->local[j],af_mat4_mul(inverse,af_animation_pose_matrix(i->poses[o->model.node])));}}
    }
    for(size_t j=0;j<count;++j)if(!world(r,j,0))return false;r->world_count=count;af_renderer_set_object_worlds(r->renderer,r->scene,r->world,count);
    af_object_id palettes[RUNTIME_INSTANCES];size_t palette_count=0;
    for(size_t j=0;j<count&&palette_count<RUNTIME_INSTANCES;++j){const af_scene_object *o=&r->scene->objects[j];if(!o->model.root||!o->model.primitive||o->model.skin<0)continue;animation_instance *i=existing_instance(r,o->model.root);animation_asset *a=i?asset(r,i->configuration.asset):NULL;if(!a||(size_t)o->model.skin>=a->file.skin_count)continue;
        const af_animation_skin *skin=&a->file.skins[o->model.skin];af_mat4 inverse,palette[AF_ANIMATION_MAX_JOINTS];if(!af_mat4_inverse(r->world[j],&inverse))continue;bool valid=true;
        for(size_t k=0;k<skin->joint_count;++k){size_t node=skin->joints[k]<i->node_count?object_index(r,i->nodes[skin->joints[k]]):count;if(node==count){valid=false;break;}palette[k]=af_mat4_mul(inverse,af_mat4_mul(r->world[node],skin->inverse_bind[k]));}
        if(valid&&af_renderer_set_skin_palette(r->renderer,o->id,palette,skin->joint_count))palettes[palette_count++]=o->id;
    }
    for(size_t j=0;j<r->palette_count;++j){bool found=false;for(size_t k=0;k<palette_count;++k)if(palettes[k]==r->palette_objects[j])found=true;if(!found)af_renderer_set_skin_palette(r->renderer,r->palette_objects[j],NULL,0);}memcpy(r->palette_objects,palettes,palette_count*sizeof(*palettes));r->palette_count=palette_count;return true;
}

af_animation_debug af_animation_runtime_debug(af_animation_runtime *r,af_object_id object){animation_instance *i=instance(r,object);if(!i)return (af_animation_debug){0};
    af_animation_debug d={.available=true,.driven=i->driven,.root_motion=i->locomotion.root_motion,.motion=i->locomotion.motion,.root_delta=i->locomotion.root_delta,.pending_events=i->events.count,.emitted_events=i->emitted,.dropped_events=i->events.dropped};memcpy(d.weights,i->locomotion.weights,sizeof(d.weights));snprintf(d.last_event,sizeof(d.last_event),"%s",i->last_event);return d;
}
size_t af_animation_runtime_bones(af_animation_runtime *r,af_object_id object,af_animation_bone *out,size_t capacity){
    animation_instance *i=instance(r,object);if(!i||r->world_count!=r->scene->count)return 0;animation_asset *a=asset(r,i->configuration.asset);unsigned char seen[AF_ANIMATION_MAX_NODES]={0};size_t count=0;
    for(size_t skin=0;skin<a->file.skin_count;++skin)for(size_t j=0;j<a->file.skins[skin].joint_count;++j){uint32_t node=a->file.skins[skin].joints[j];if(node>=i->node_count||seen[node])continue;seen[node]=1;af_mat4 m;if(!af_animation_runtime_world(r,i->nodes[node],&m))continue;
        af_animation_bone b={.node=node,.position={m.elements[12],m.elements[13],m.elements[14]}};int32_t parent=a->file.parents[node];if(parent>=0&&(size_t)parent<i->node_count&&af_animation_runtime_world(r,i->nodes[parent],&m)){b.parent_position=(af_vec3){m.elements[12],m.elements[13],m.elements[14]};b.has_parent=true;}if(out&&count<capacity)out[count]=b;++count;}
    return count;
}
