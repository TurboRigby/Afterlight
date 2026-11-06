/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <afterlight/audio.h>
#include <math.h>
#include <stdio.h>
#define CHECK(c) do{if(!(c)){fprintf(stderr,"Audio %d: %s\n",__LINE__,#c);return 1;}}while(0)
int main(int argc,char **argv){CHECK(argc==2);af_audio *audio=af_audio_create();CHECK(audio);const char *names[]={"jump","land","finish","checkpoint"};af_asset_error error;
    for(unsigned i=0;i<4;++i){char path[4096];snprintf(path,sizeof(path),"%s/%s.wav",argv[1],names[i]);CHECK(af_audio_load(audio,names[i],path,&error));}
    CHECK(af_audio_get_stats(audio).clips==4&&!af_audio_get_stats(audio).device_ready);
    CHECK(af_audio_play(audio,"jump",.5f)&&af_audio_play(audio,"land",.5f)&&af_audio_play(audio,"finish",.5f));af_audio_stats stats=af_audio_get_stats(audio);CHECK(stats.device_ready&&stats.plays==3&&stats.playing>=1);
    CHECK(!af_audio_play(audio,"missing",.5f)&&!af_audio_play(audio,"jump",NAN)&&!af_audio_play(audio,"jump",2));
    CHECK(!af_audio_load(audio,"jump","missing.wav",&error)&&af_audio_get_stats(audio).clips==4);af_audio_stop(audio);CHECK(af_audio_get_stats(audio).playing==0);
    CHECK(af_audio_play(audio,"jump",.25f)&&af_audio_get_stats(audio).plays==4);af_audio_stop(audio);CHECK(af_audio_play_at(audio,"land",(af_sound_emitter){.position={2,0,0},.gain=1,.min_distance=1,.max_distance=10})&&af_audio_play_at(audio,"land",(af_sound_emitter){.position={-2,0,0},.gain=1,.min_distance=1,.max_distance=10}));CHECK(af_audio_get_stats(audio).playing==2);float left,right;CHECK(af_audio_spatial_gains((af_vec3){0},(af_vec3){0,0,-1},(af_sound_emitter){.position={3,0,0},.gain=1,.min_distance=1,.max_distance=10},&left,&right)&&right>left);CHECK(af_audio_spatial_gains((af_vec3){0},(af_vec3){0,0,-1},(af_sound_emitter){.position={0,0,11},.gain=1,.min_distance=1,.max_distance=10},&left,&right)&&left==0&&right==0);float samples[512];CHECK(af_audio_mix(audio,(af_vec3){0},(af_vec3){0,0,-1},samples,256));for(unsigned i=0;i<512;++i)CHECK(isfinite(samples[i])&&fabsf(samples[i])<=1);af_audio_destroy(audio);puts("Audio: actual WAV decoding, lazy dummy playback, mixed clips, gain, stop/replay, invalid input and replacement preservation passed.");return 0;
}
