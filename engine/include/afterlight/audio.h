/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef AFTERLIGHT_AUDIO_H
#define AFTERLIGHT_AUDIO_H
#include <afterlight/asset.h>
typedef struct af_audio af_audio;
typedef struct af_audio_stats {
    size_t clips, playing;
    uint64_t plays;
    bool device_ready;
} af_audio_stats;
typedef struct af_sound_emitter {
    af_vec3 position;
    float gain, min_distance, max_distance;
} af_sound_emitter;
bool af_audio_play_at(af_audio *, const char *, af_sound_emitter);
bool af_audio_spatial_gains(af_vec3 listener, af_vec3 forward, af_sound_emitter, float *left, float *right);
/* Interleaved stereo 48 kHz floats. Main-thread mixer, at most 4096 frames. */
bool af_audio_mix(af_audio *, af_vec3 listener, af_vec3 forward, float *samples, size_t frames);
void af_audio_update(af_audio *, af_vec3 listener, af_vec3 forward);
/* Main-thread API. WAV decoding works without a device; playback opens the
   default SDL device lazily. Up to 32 independent voices mix, including overlapping instances of one clip. */
af_audio *af_audio_create(void);
void af_audio_destroy(af_audio *audio);
bool af_audio_load(af_audio *audio, const char *name, const char *path, af_asset_error *error);
bool af_audio_play(af_audio *audio, const char *name, float gain);
void af_audio_stop(af_audio *audio);
af_audio_stats af_audio_get_stats(const af_audio *audio);
#endif
