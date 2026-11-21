/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef AFTERLIGHT_AUDIO_H
#define AFTERLIGHT_AUDIO_H
#include <afterlight/asset.h>
typedef struct af_audio af_audio;
typedef uint64_t af_sound_loop;
typedef uint64_t af_sound_stream;
typedef enum af_audio_bus {
    AF_AUDIO_BUS_MASTER = 0,
    AF_AUDIO_BUS_SFX = 1,
    AF_AUDIO_BUS_MUSIC = 2,
    AF_AUDIO_BUS_AMBIENCE = 3,
    AF_AUDIO_BUS_COUNT = 4
} af_audio_bus;
typedef struct af_audio_stats {
    size_t clips, playing;
    uint64_t plays;
    bool device_ready;
    size_t looping;
    size_t streaming;
} af_audio_stats;
typedef struct af_sound_emitter {
    af_vec3 position;
    float gain, min_distance, max_distance;
} af_sound_emitter;
bool af_audio_set_bus_gain(af_audio *audio, af_audio_bus bus, float gain);
float af_audio_get_bus_gain(const af_audio *audio, af_audio_bus bus);
bool af_audio_play_at(af_audio *, const char *, af_sound_emitter);
bool af_audio_play_bus(af_audio *, const char *, af_audio_bus bus, float gain);
bool af_audio_play_at_bus(af_audio *, const char *, af_audio_bus bus, af_sound_emitter);
/* Zero is an invalid handle. A loop continues across clip boundaries until stopped. */
af_sound_loop af_audio_start_loop(af_audio *, const char *, af_sound_emitter, float fade_seconds);
bool af_audio_move_loop(af_audio *, af_sound_loop, af_vec3 position);
bool af_audio_fade_loop(af_audio *, af_sound_loop, float gain, float seconds);
bool af_audio_stop_loop(af_audio *, af_sound_loop, float fade_seconds);
/* Disk-streaming playback for long music/ambience tracks (WAV PCM/float/IMA-ADPCM or QOA).
 * Zero duration and file-size ceiling; decodes in bounded chunks directly into the mixer. */
af_sound_stream af_audio_stream_start(af_audio *audio, const char *path, af_audio_bus bus, float gain, bool loop,
                                      float fade_seconds, af_asset_error *error);
bool af_audio_stream_pause(af_audio *audio, af_sound_stream handle, bool paused);
bool af_audio_stream_fade(af_audio *audio, af_sound_stream handle, float gain, float fade_seconds);
bool af_audio_stream_stop(af_audio *audio, af_sound_stream handle, float fade_seconds);
void af_audio_suspend(af_audio *, bool suspended);
bool af_audio_spatial_gains(af_vec3 listener, af_vec3 forward, af_sound_emitter, float *left, float *right);
/* Interleaved stereo 48 kHz floats. Main-thread mixer, at most 4096 frames. */
bool af_audio_mix(af_audio *, af_vec3 listener, af_vec3 forward, float *samples, size_t frames);
void af_audio_update(af_audio *, af_vec3 listener, af_vec3 forward);
/* Main-thread API. WAV (PCM/float/IMA-ADPCM) and QOA decoding work without a device; playback opens
 * the default SDL device lazily. Dynamically grows clip, voice, and stream pools. */
af_audio *af_audio_create(void);
void af_audio_destroy(af_audio *audio);
bool af_audio_load(af_audio *audio, const char *name, const char *path, af_asset_error *error);
bool af_audio_play(af_audio *audio, const char *name, float gain);
void af_audio_stop(af_audio *audio);
af_audio_stats af_audio_get_stats(const af_audio *audio);
#endif
