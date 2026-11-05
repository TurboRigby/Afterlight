/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <afterlight/audio.h>
#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CLIP_LIMIT 32
#define BYTE_LIMIT (64u*1024u*1024u)
typedef struct clip {char name[AF_MESH_NAME_CAPACITY];SDL_AudioSpec spec;Uint8 *data;Uint32 length;SDL_AudioStream *stream;Uint64 ends;} clip;
struct af_audio {clip clips[CLIP_LIMIT];size_t count,bytes;uint64_t plays;bool initialized;};
static bool fail(af_asset_error *error,const char *message){if(error)snprintf(error->message,sizeof(error->message),"%s",message);return false;}
af_audio *af_audio_create(void){return calloc(1,sizeof(af_audio));}
void af_audio_destroy(af_audio *audio){if(!audio)return;for(size_t i=0;i<audio->count;++i){SDL_DestroyAudioStream(audio->clips[i].stream);SDL_free(audio->clips[i].data);}if(audio->initialized)SDL_QuitSubSystem(SDL_INIT_AUDIO);free(audio);}
bool af_audio_load(af_audio *audio,const char *name,const char *path,af_asset_error *error){
    if(error)*error=(af_asset_error){0};if(!audio||!name||!name[0]||strlen(name)>=AF_MESH_NAME_CAPACITY||!path)return fail(error,"Audio requires a name and WAV path.");
    FILE *file=fopen(path,"rb");if(!file)return fail(error,"Cannot open WAV.");long size;bool bounded=!fseek(file,0,SEEK_END)&&(size=ftell(file))>0&&size<=8*1024*1024;if(fclose(file))bounded=false;
    if(!bounded)return fail(error,"WAV file exceeds 8 MiB or is unreadable.");
    clip value={0};if(!SDL_LoadWAV(path,&value.spec,&value.data,&value.length))return fail(error,SDL_GetError());
    double duration=(double)value.length/(double)SDL_AUDIO_FRAMESIZE(value.spec)/(double)value.spec.freq;
    if(!value.length||value.spec.channels<1||value.spec.channels>8||value.spec.freq<8000||value.spec.freq>192000||!isfinite(duration)||duration>30){SDL_free(value.data);return fail(error,"WAV must contain at most 30 seconds, 1-8 channels, 8-192 kHz.");}
    size_t index=audio->count;for(size_t i=0;i<audio->count;++i)if(!strcmp(audio->clips[i].name,name)){index=i;break;}
    size_t previous=index<audio->count?audio->clips[index].length:0;
    if(index==CLIP_LIMIT||audio->bytes-previous+value.length>BYTE_LIMIT){SDL_free(value.data);return fail(error,"Audio clip/cache limit reached.");}
    snprintf(value.name,sizeof(value.name),"%s",name);
    if(index<audio->count){SDL_DestroyAudioStream(audio->clips[index].stream);SDL_free(audio->clips[index].data);}else ++audio->count;
    audio->clips[index]=value;audio->bytes=audio->bytes-previous+value.length;return true;
}
bool af_audio_play(af_audio *audio,const char *name,float gain){
    if(!audio||!name||!isfinite(gain)||gain<0||gain>1)return false;
    clip *value=NULL;for(size_t i=0;i<audio->count;++i)if(!strcmp(audio->clips[i].name,name)){value=&audio->clips[i];break;}if(!value)return false;
    if(!audio->initialized){if(!SDL_InitSubSystem(SDL_INIT_AUDIO))return false;audio->initialized=true;}
    if(!value->stream)value->stream=SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,&value->spec,NULL,NULL);if(!value->stream)return false;
    if(!SDL_ClearAudioStream(value->stream)||!SDL_SetAudioStreamGain(value->stream,gain)||!SDL_PutAudioStreamData(value->stream,value->data,(int)value->length)||!SDL_ResumeAudioStreamDevice(value->stream))return false;
    value->ends=SDL_GetTicks()+(Uint64)((double)value->length/(double)SDL_AUDIO_FRAMESIZE(value->spec)/(double)value->spec.freq*1000)+50;++audio->plays;return true;
}
void af_audio_stop(af_audio *audio){if(!audio)return;for(size_t i=0;i<audio->count;++i){clip *value=&audio->clips[i];if(value->stream){SDL_ClearAudioStream(value->stream);SDL_PauseAudioStreamDevice(value->stream);}value->ends=0;}}
af_audio_stats af_audio_get_stats(const af_audio *audio){af_audio_stats stats={0};if(!audio)return stats;stats.clips=audio->count;stats.plays=audio->plays;
    for(size_t i=0;i<audio->count;++i){const clip *value=&audio->clips[i];stats.device_ready|=value->stream!=NULL;if(value->stream&&value->ends>SDL_GetTicks())++stats.playing;}return stats;
}
