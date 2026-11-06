/* SPDX-License-Identifier: MPL-2.0 */
#include <afterlight/audio.h>
#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CLIP_LIMIT 32
#define VOICE_LIMIT 32
#define BYTE_LIMIT (64u*1024u*1024u)
typedef struct clip {char name[64];float *samples;size_t frames,bytes;} clip;
typedef struct voice {size_t clip,cursor;float gain;bool active,spatial;af_sound_emitter emitter;Uint64 ends;} voice;
struct af_audio {clip clips[CLIP_LIMIT];voice voices[VOICE_LIMIT];size_t count,bytes;uint64_t plays;bool initialized;SDL_AudioStream *stream;};
static bool finite3(af_vec3 v){return isfinite(v.x)&&isfinite(v.y)&&isfinite(v.z);}
static bool emitter_valid(af_sound_emitter e){return finite3(e.position)&&isfinite(e.gain)&&e.gain>=0&&e.gain<=1&&isfinite(e.min_distance)&&isfinite(e.max_distance)&&e.min_distance>=.01f&&e.max_distance>e.min_distance&&e.max_distance<=10000;}
static bool fail(af_asset_error *e,const char *message){if(e)snprintf(e->message,sizeof(e->message),"%s",message);return false;}
af_audio *af_audio_create(void){return calloc(1,sizeof(af_audio));}
void af_audio_destroy(af_audio *a){if(!a)return;SDL_DestroyAudioStream(a->stream);for(size_t i=0;i<a->count;++i)SDL_free(a->clips[i].samples);if(a->initialized)SDL_QuitSubSystem(SDL_INIT_AUDIO);free(a);}
bool af_audio_load(af_audio *a,const char *name,const char *path,af_asset_error *e){
    if(e)*e=(af_asset_error){0};if(!a||!name||!name[0]||strlen(name)>=64||!path)return fail(e,"Audio requires a name and WAV path.");FILE *f=fopen(path,"rb");if(!f)return fail(e,"Cannot open WAV.");long size;bool ok=!fseek(f,0,SEEK_END)&&(size=ftell(f))>0&&size<=8*1024*1024;fclose(f);if(!ok)return fail(e,"WAV exceeds 8 MiB.");
    SDL_AudioSpec spec;Uint8 *bytes=NULL;Uint32 length=0;if(!SDL_LoadWAV(path,&spec,&bytes,&length))return fail(e,SDL_GetError());double duration=spec.freq>0&&SDL_AUDIO_FRAMESIZE(spec)>0?(double)length/SDL_AUDIO_FRAMESIZE(spec)/spec.freq:0;
    if(!length||spec.channels<1||spec.channels>8||spec.freq<8000||spec.freq>192000||!isfinite(duration)||duration<=0||duration>30){SDL_free(bytes);return fail(e,"WAV must contain at most 30 seconds, 1-8 channels, 8-192 kHz.");}
    SDL_AudioSpec output={.format=SDL_AUDIO_F32,.channels=2,.freq=48000};Uint8 *converted=NULL;int count=0;ok=SDL_ConvertAudioSamples(&spec,bytes,(int)length,&output,&converted,&count);SDL_free(bytes);if(!ok||count<=0||count%8){SDL_free(converted);return fail(e,"WAV conversion failed.");}
    for(size_t i=0;i<(size_t)count/sizeof(float);++i)if(!isfinite(((float*)converted)[i])){SDL_free(converted);return fail(e,"WAV contains non-finite samples.");}
    size_t at=0;while(at<a->count&&strcmp(a->clips[at].name,name))++at;size_t previous=at<a->count?a->clips[at].bytes:0;if(at==CLIP_LIMIT||(size_t)count>BYTE_LIMIT||a->bytes-previous>BYTE_LIMIT-(size_t)count){SDL_free(converted);return fail(e,"Audio cache exceeds 32 clips / 64 MiB.");}
    if(at<a->count){for(unsigned i=0;i<VOICE_LIMIT;++i)if(a->voices[i].clip==at)a->voices[i].active=false;SDL_free(a->clips[at].samples);}else ++a->count;a->clips[at]=(clip){.samples=(float*)converted,.frames=(size_t)count/8,.bytes=(size_t)count};snprintf(a->clips[at].name,64,"%s",name);a->bytes=a->bytes-previous+(size_t)count;return true;
}
static bool device(af_audio *a){if(!a->initialized){if(!SDL_InitSubSystem(SDL_INIT_AUDIO))return false;a->initialized=true;}if(!a->stream){SDL_AudioSpec spec={.format=SDL_AUDIO_F32,.channels=2,.freq=48000};a->stream=SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,&spec,NULL,NULL);}return a->stream&&SDL_ResumeAudioStreamDevice(a->stream);}
static bool play(af_audio *a,const char *name,float gain,const af_sound_emitter *emitter){if(!a||!name||!isfinite(gain)||gain<0||gain>1)return false;size_t at=0;while(at<a->count&&strcmp(a->clips[at].name,name))++at;if(at==a->count)return false;Uint64 now=SDL_GetTicks();unsigned slot=0;while(slot<VOICE_LIMIT&&a->voices[slot].active&&a->voices[slot].ends>now)++slot;if(slot==VOICE_LIMIT||!device(a))return false;a->voices[slot]=(voice){.clip=at,.active=true,.gain=gain,.spatial=emitter!=NULL,.ends=now+(Uint64)(a->clips[at].frames*1000/48000)+100};if(emitter)a->voices[slot].emitter=*emitter;++a->plays;return true;}
bool af_audio_play(af_audio *a,const char *name,float gain){return play(a,name,gain,NULL);}
bool af_audio_play_at(af_audio *a,const char *name,af_sound_emitter e){return emitter_valid(e)&&play(a,name,e.gain,&e);}
bool af_audio_spatial_gains(af_vec3 listener,af_vec3 forward,af_sound_emitter e,float *left,float *right){if(!left||!right||!finite3(listener)||!finite3(forward)||!emitter_valid(e))return false;af_vec3 d=af_vec3_sub(e.position,listener);float distance=af_vec3_length(d);if(!isfinite(distance))return false;af_vec3 axis=af_vec3_normalize((af_vec3){-forward.z,0,forward.x});float pan=distance>.0001f?af_vec3_dot(af_vec3_scale(d,1/distance),axis):0;pan=fminf(1,fmaxf(-1,pan));float t=fminf(1,fmaxf(0,(distance-e.min_distance)/(e.max_distance-e.min_distance))),gain=e.gain*(1-t)*(1-t);*left=gain*sqrtf((1-pan)*.5f);*right=gain*sqrtf((1+pan)*.5f);return true;}
bool af_audio_mix(af_audio *a,af_vec3 listener,af_vec3 forward,float *out,size_t frames){if(!a||!out||frames>4096||!finite3(listener)||!finite3(forward))return false;memset(out,0,frames*2*sizeof(float));for(unsigned v=0;v<VOICE_LIMIT;++v){voice *voice=&a->voices[v];if(!voice->active)continue;clip *c=&a->clips[voice->clip];float left=voice->gain,right=left;if(voice->spatial&&!af_audio_spatial_gains(listener,forward,voice->emitter,&left,&right))return false;size_t remaining=c->frames-voice->cursor,n=frames<remaining?frames:remaining;for(size_t i=0;i<n;++i){out[i*2]+=c->samples[(voice->cursor+i)*2]*left;out[i*2+1]+=c->samples[(voice->cursor+i)*2+1]*right;}voice->cursor+=n;if(voice->cursor==c->frames)voice->active=false;}
    for(size_t i=0;i<frames*2;++i)out[i]=fminf(1,fmaxf(-1,out[i]));return true;}
void af_audio_update(af_audio *a,af_vec3 listener,af_vec3 forward){if(!a||!a->stream)return;int queued=SDL_GetAudioStreamQueued(a->stream);if(queued<0||queued>=2048*8)return;float samples[2048*2];if(af_audio_mix(a,listener,forward,samples,2048-(size_t)queued/8))SDL_PutAudioStreamData(a->stream,samples,(int)((2048-(size_t)queued/8)*8));}
void af_audio_stop(af_audio *a){if(!a)return;memset(a->voices,0,sizeof(a->voices));if(a->stream)SDL_ClearAudioStream(a->stream);}
af_audio_stats af_audio_get_stats(const af_audio *a){if(!a)return (af_audio_stats){0};af_audio_stats s={.clips=a->count,.plays=a->plays,.device_ready=a->stream!=NULL};Uint64 now=SDL_GetTicks();for(unsigned i=0;i<VOICE_LIMIT;++i)if(a->voices[i].active&&a->voices[i].ends>now)++s.playing;return s;}
