/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef AFTERLIGHT_AUDIO_H
#define AFTERLIGHT_AUDIO_H
#include <afterlight/asset.h>
typedef struct af_audio af_audio;
typedef struct af_audio_stats {size_t clips,playing;uint64_t plays;bool device_ready;} af_audio_stats;
/* Main-thread API. WAV decoding works without a device; playback opens the
   default SDL device lazily. Different clips mix; replay restarts the same clip. */
af_audio *af_audio_create(void);
void af_audio_destroy(af_audio *audio);
bool af_audio_load(af_audio *audio,const char *name,const char *path,af_asset_error *error);
bool af_audio_play(af_audio *audio,const char *name,float gain);
void af_audio_stop(af_audio *audio);
af_audio_stats af_audio_get_stats(const af_audio *audio);
#endif
