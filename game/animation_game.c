/* SPDX-License-Identifier: MPL-2.0 */
#include <afterlight/game_module.h>
#include <math.h>
typedef struct state {af_object_id actors[8];unsigned count,manual_clip;double elapsed;bool manual;} state;
static bool init(void *memory,const af_game_host *host){
    state *s=memory;if(!host->animation_play||!host->animation_status)return false;
    for(size_t i=0;i<host->object_count(host->scene)&&s->count<8;++i){const af_scene_object *o=host->object_at(host->scene,i);if(!o->animation.asset[0])continue;
        af_animation_status status=host->animation_status(host->engine,o->id);if(!status.available||!status.clip_count)return false;s->actors[s->count++]=o->id;
        if(!host->animation_play(host->engine,o->id,o->animation.clip<status.clip_count?o->animation.clip:0,0)||!host->animation_options(host->engine,o->id,o->animation.speed,o->animation.loop))return false;
    }host->log("Animation demo: clips cycle every four seconds. Space chooses a clip; WASD moves the first actor. Stop restores the scene.");return s->count>0;
}
static void frame_update(void *memory,const af_game_host *host,double dt){
    (void)dt;state *s=memory;if(!s->count)return;
    if(host->key_pressed(host,AF_KEY_SPACE)){af_animation_status status=host->animation_status(host->engine,s->actors[0]);if(status.clip_count){s->manual=true;s->manual_clip=(status.clip+1u)%(unsigned)status.clip_count;host->animation_play(host->engine,s->actors[0],s->manual_clip,.25f);}}
}
static void update(void *memory,const af_game_host *host,double dt){
    state *s=memory;s->elapsed+=dt;if(!s->count)return;const af_scene_object *o=host->get_object(host->scene,s->actors[0]);if(!o)return;
    float x=(float)host->key_down(host,AF_KEY_D)-(float)host->key_down(host,AF_KEY_A),z=(float)host->key_down(host,AF_KEY_S)-(float)host->key_down(host,AF_KEY_W);
    bool moving=x!=0||z!=0;af_animation_status status=host->animation_status(host->engine,o->id);if(!status.clip_count)return;
    unsigned clip=moving&&status.clip_count>1?1u:s->manual?s->manual_clip:(unsigned)fmod(floor(s->elapsed/4),(double)status.clip_count);
    if(clip!=status.clip)host->animation_play(host->engine,o->id,clip,.25f);
    if(moving){af_transform t=o->transform;t.position.x=fmaxf(-4,fminf(4,t.position.x+x*(float)dt*2));t.position.z=fmaxf(-2,fminf(2,t.position.z+z*(float)dt*2));host->set_transform(host->scene,o->id,t);}
}
static void shutdown(void *memory,const af_game_host *host){(void)memory;host->log("Animation demo stopped.");}
AF_GAME_EXPORT const af_game_module *af_game_get_api(uint32_t version){
    static const af_game_module module={AF_GAME_ABI_VERSION,sizeof(af_game_module),sizeof(af_game_host),sizeof(af_scene_object),"Afterlight animation demos",sizeof(state),init,update,shutdown,frame_update};return version==AF_GAME_ABI_VERSION?&module:NULL;
}
