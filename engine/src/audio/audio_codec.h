/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef AF_AUDIO_CODEC_H
#define AF_AUDIO_CODEC_H

#include <afterlight/asset.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AF_AUDIO_OUTPUT_RATE 48000u
#define AF_AUDIO_OUTPUT_CHANNELS 2u

typedef struct af_audio_decoder af_audio_decoder;

/* Opens a WAV (PCM 8/16/24/32, IEEE float, or IMA-ADPCM) or QOA file for streaming decode
 * directly to interleaved stereo 48 kHz float frames. No duration or file-size ceiling. */
af_audio_decoder *af_audio_decoder_open(const char *path, af_asset_error *error);
void af_audio_decoder_close(af_audio_decoder *decoder);

/* Decodes up to max_frames interleaved stereo 48 kHz float frames into out_stereo.
 * Returns the number of frames decoded; 0 indicates end-of-stream (or read error). */
size_t af_audio_decoder_read(af_audio_decoder *decoder, float *out_stereo, size_t max_frames);

/* Seeks back to the beginning of the audio data for seamless looping. */
bool af_audio_decoder_rewind(af_audio_decoder *decoder);

/* Decodes an entire short sound effect (WAV PCM/float/IMA-ADPCM or QOA) into a newly
 * malloc'd interleaved stereo 48 kHz float buffer. Rejects clips longer than 60 seconds. */
bool af_audio_decode_clip(const char *path, float **out_samples, size_t *out_frames, size_t *out_bytes,
                          af_asset_error *error);

/* Utility encoders (used by tools and tests) for spec-compliant QOA and IMA-ADPCM WAV files. */
bool af_qoa_encode_file(const char *path, const int16_t *pcm, uint32_t frames, uint32_t channels, uint32_t sample_rate);
bool af_ima_adpcm_encode_wav(const char *path, const int16_t *pcm, uint32_t frames, uint32_t channels,
                             uint32_t sample_rate);

#endif /* AF_AUDIO_CODEC_H */
