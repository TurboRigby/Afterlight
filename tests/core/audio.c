/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <afterlight/audio.h>
#include "audio/audio_codec.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            fprintf(stderr, "Audio %d: %s\n", __LINE__, #c);                                                           \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)

static float block_rms(const float *samples, size_t count) {
    double sum = 0.0;
    for (size_t i = 0; i < count; ++i)
        sum += (double)samples[i] * (double)samples[i];
    return (float)sqrt(sum / (double)(count ? count : 1u));
}

int main(int argc, char **argv) {
    CHECK(argc == 2);
    af_audio *audio = af_audio_create();
    CHECK(audio);
    const char *names[] = {"jump", "land", "finish", "checkpoint"};
    af_asset_error error;
    for (unsigned i = 0; i < 4; ++i) {
        char path[4096];
        snprintf(path, sizeof(path), "%s/%s.wav", argv[1], names[i]);
        CHECK(af_audio_load(audio, names[i], path, &error));
    }
    CHECK(af_audio_get_stats(audio).clips == 4 && !af_audio_get_stats(audio).device_ready);
    CHECK(af_audio_play(audio, "jump", .5f) && af_audio_play(audio, "land", .5f) &&
          af_audio_play(audio, "finish", .5f));
    af_audio_stats stats = af_audio_get_stats(audio);
    CHECK(stats.device_ready && stats.plays == 3 && stats.playing >= 1);
    CHECK(!af_audio_play(audio, "missing", .5f) && !af_audio_play(audio, "jump", NAN) &&
          !af_audio_play(audio, "jump", 2));
    CHECK(!af_audio_load(audio, "jump", "missing.wav", &error) && af_audio_get_stats(audio).clips == 4);
    af_audio_stop(audio);
    CHECK(af_audio_get_stats(audio).playing == 0);
    CHECK(af_audio_play(audio, "jump", .25f) && af_audio_get_stats(audio).plays == 4);
    af_audio_stop(audio);
    CHECK(
        af_audio_play_at(audio, "land",
                         (af_sound_emitter){.position = {2, 0, 0}, .gain = 1, .min_distance = 1, .max_distance = 10}) &&
        af_audio_play_at(audio, "land",
                         (af_sound_emitter){.position = {-2, 0, 0}, .gain = 1, .min_distance = 1, .max_distance = 10}));
    CHECK(af_audio_get_stats(audio).playing == 2);
    float left, right;
    CHECK(af_audio_spatial_gains(
              (af_vec3){0}, (af_vec3){0, 0, -1},
              (af_sound_emitter){.position = {3, 0, 0}, .gain = 1, .min_distance = 1, .max_distance = 10}, &left,
              &right) &&
          right > left);
    CHECK(af_audio_spatial_gains(
              (af_vec3){0}, (af_vec3){0, 0, -1},
              (af_sound_emitter){.position = {0, 0, 11}, .gain = 1, .min_distance = 1, .max_distance = 10}, &left,
              &right) &&
          left == 0 && right == 0);
    float samples[512];
    CHECK(af_audio_mix(audio, (af_vec3){0}, (af_vec3){0, 0, -1}, samples, 256));
    for (unsigned i = 0; i < 512; ++i)
        CHECK(isfinite(samples[i]) && fabsf(samples[i]) <= 1);
    af_audio_stop(audio);
    af_sound_emitter emitter = {.position = {2, 0, 0}, .gain = .7f, .min_distance = 1, .max_distance = 10};
    af_sound_loop loop = af_audio_start_loop(audio, "jump", emitter, .02f);
    CHECK(loop && af_audio_get_stats(audio).looping == 1);
    float block[8192];
    for (unsigned i = 0; i < 40; ++i)
        CHECK(af_audio_mix(audio, (af_vec3){0}, (af_vec3){0, 0, -1}, block, 4096));
    CHECK(af_audio_get_stats(audio).looping == 1);
    CHECK(af_audio_move_loop(audio, loop, (af_vec3){20, 0, 0}));
    CHECK(af_audio_mix(audio, (af_vec3){0}, (af_vec3){0, 0, -1}, block, 4096));
    for (unsigned i = 0; i < 8192; ++i)
        CHECK(block[i] == 0);
    CHECK(af_audio_move_loop(audio, loop, emitter.position) && af_audio_fade_loop(audio, loop, .4f, .04f));
    CHECK(af_audio_stop_loop(audio, loop, .04f));
    af_audio_suspend(audio, true);
    CHECK(af_audio_mix(audio, (af_vec3){0}, (af_vec3){0, 0, -1}, block, 4096));
    CHECK(af_audio_get_stats(audio).looping == 1 && block[0] == 0);
    af_audio_suspend(audio, false);
    CHECK(af_audio_mix(audio, (af_vec3){0}, (af_vec3){0, 0, -1}, block, 4096));
    CHECK(af_audio_get_stats(audio).looping == 0 && !af_audio_move_loop(audio, loop, emitter.position));
    CHECK(af_audio_start_loop(audio, "jump", emitter, 0) != loop);
    af_audio_stop(audio);
    CHECK(af_audio_get_stats(audio).looping == 0);

    /* --- Phase 6 tests: dynamic pools (>32), QOA, IMA-ADPCM WAV, disk streaming (>30s), mix buses, soft clip --- */
    /* 1. Dynamic clip pool (>32) and dynamic voice pool (>32) + soft clipping */
    for (unsigned i = 0; i < 48; ++i) {
        char clip_name[32], path[4096];
        snprintf(clip_name, sizeof(clip_name), "dyn_clip_%u", i);
        snprintf(path, sizeof(path), "%s/jump.wav", argv[1]);
        CHECK(af_audio_load(audio, clip_name, path, &error));
    }
    CHECK(af_audio_get_stats(audio).clips == 52);
    for (unsigned i = 0; i < 48; ++i) {
        char clip_name[32];
        snprintf(clip_name, sizeof(clip_name), "dyn_clip_%u", i);
        CHECK(af_audio_play(audio, clip_name, 0.8f));
    }
    CHECK(af_audio_get_stats(audio).playing == 48);
    CHECK(af_audio_mix(audio, (af_vec3){0}, (af_vec3){0, 0, -1}, block, 1024));
    for (unsigned i = 0; i < 2048; ++i)
        CHECK(isfinite(block[i]) && fabsf(block[i]) <= 1.0f);
    af_audio_stop(audio);

    /* 2. QOA compressed codec (5x compression, 44.1 kHz stereo -> resampled to 48 kHz) */
    const uint32_t qoa_rate = 44100u;
    const uint32_t qoa_frames = 22050u; /* 0.5s */
    int16_t *qoa_pcm = malloc((size_t)qoa_frames * 2u * sizeof(int16_t));
    CHECK(qoa_pcm != NULL);
    for (uint32_t i = 0; i < qoa_frames; ++i) {
        float t = (float)i / (float)qoa_rate;
        int16_t s = (int16_t)(sinf(2.0f * AF_PI * 440.0f * t) * 20000.0f);
        qoa_pcm[i * 2u] = s;
        qoa_pcm[i * 2u + 1u] = s;
    }
    const char *qoa_path = "test_tone.qoa";
    CHECK(af_qoa_encode_file(qoa_path, qoa_pcm, qoa_frames, 2u, qoa_rate));
    free(qoa_pcm);

    CHECK(af_audio_load(audio, "qoa_tone", qoa_path, &error));
    CHECK(af_audio_play(audio, "qoa_tone", 0.5f));
    CHECK(af_audio_mix(audio, (af_vec3){0}, (af_vec3){0, 0, -1}, block, 2048));
    float qoa_rms = block_rms(block, 4096);
    CHECK(qoa_rms > 0.15f && qoa_rms < 0.35f);
    af_audio_stop(audio);

    /* 3. IMA-ADPCM WAV compressed codec (4:1 compression, mono 48 kHz) */
    const uint32_t adpcm_frames = 24000u; /* 0.5s */
    int16_t *adpcm_pcm = malloc((size_t)adpcm_frames * sizeof(int16_t));
    CHECK(adpcm_pcm != NULL);
    for (uint32_t i = 0; i < adpcm_frames; ++i) {
        float t = (float)i / 48000.0f;
        adpcm_pcm[i] = (int16_t)(sinf(2.0f * AF_PI * 220.0f * t) * 18000.0f);
    }
    const char *adpcm_path = "test_ima.wav";
    CHECK(af_ima_adpcm_encode_wav(adpcm_path, adpcm_pcm, adpcm_frames, 1u, 48000u));
    free(adpcm_pcm);

    CHECK(af_audio_load(audio, "ima_tone", adpcm_path, &error));
    CHECK(af_audio_play(audio, "ima_tone", 0.5f));
    CHECK(af_audio_mix(audio, (af_vec3){0}, (af_vec3){0, 0, -1}, block, 2048));
    float ima_rms = block_rms(block, 4096);
    CHECK(ima_rms > 0.12f && ima_rms < 0.32f);
    af_audio_stop(audio);

    /* 4. True disk streaming of a 35-second QOA track (> old 30s ceiling) + Mix Buses + Pause/Fade/Loop */
    const uint32_t long_rate = 16000u;
    const uint32_t long_frames = long_rate * 35u; /* 35 seconds */
    int16_t *long_pcm = malloc((size_t)long_frames * sizeof(int16_t));
    CHECK(long_pcm != NULL);
    for (uint32_t i = 0; i < long_frames; ++i) {
        float t = (float)i / (float)long_rate;
        long_pcm[i] = (int16_t)(sinf(2.0f * AF_PI * 330.0f * t) * 16000.0f);
    }
    const char *long_qoa_path = "test_long_music.qoa";
    CHECK(af_qoa_encode_file(long_qoa_path, long_pcm, long_frames, 1u, long_rate));
    free(long_pcm);

    af_sound_stream music =
        af_audio_stream_start(audio, long_qoa_path, AF_AUDIO_BUS_MUSIC, 0.6f, true, 0.0f, &error);
    CHECK(music != 0 && af_audio_get_stats(audio).streaming == 1);
    CHECK(af_audio_mix(audio, (af_vec3){0}, (af_vec3){0, 0, -1}, block, 2048));
    float music_rms = block_rms(block, 4096);
    CHECK(music_rms > 0.15f);

    /* Mix bus isolation: muting SFX must not silence MUSIC; muting MUSIC must silence MUSIC */
    CHECK(af_audio_set_bus_gain(audio, AF_AUDIO_BUS_SFX, 0.0f));
    CHECK(af_audio_mix(audio, (af_vec3){0}, (af_vec3){0, 0, -1}, block, 1024));
    CHECK(block_rms(block, 2048) > 0.15f);

    CHECK(af_audio_set_bus_gain(audio, AF_AUDIO_BUS_MUSIC, 0.0f));
    CHECK(af_audio_get_bus_gain(audio, AF_AUDIO_BUS_MUSIC) == 0.0f);
    CHECK(af_audio_mix(audio, (af_vec3){0}, (af_vec3){0, 0, -1}, block, 1024));
    CHECK(block_rms(block, 2048) == 0.0f);

    CHECK(af_audio_set_bus_gain(audio, AF_AUDIO_BUS_MUSIC, 1.0f));
    CHECK(af_audio_set_bus_gain(audio, AF_AUDIO_BUS_MASTER, 0.5f));
    CHECK(af_audio_mix(audio, (af_vec3){0}, (af_vec3){0, 0, -1}, block, 1024));
    float half_rms = block_rms(block, 2048);
    CHECK(fabsf(half_rms - music_rms * 0.5f) < 0.03f);
    CHECK(af_audio_set_bus_gain(audio, AF_AUDIO_BUS_MASTER, 1.0f));
    CHECK(af_audio_set_bus_gain(audio, AF_AUDIO_BUS_SFX, 1.0f));

    /* Pause / resume / fade / stop */
    CHECK(af_audio_stream_pause(audio, music, true));
    CHECK(af_audio_mix(audio, (af_vec3){0}, (af_vec3){0, 0, -1}, block, 1024));
    CHECK(block_rms(block, 2048) == 0.0f);
    CHECK(af_audio_stream_pause(audio, music, false));
    CHECK(af_audio_stream_fade(audio, music, 0.4f, 0.02f));
    CHECK(af_audio_mix(audio, (af_vec3){0}, (af_vec3){0, 0, -1}, block, 2048));
    CHECK(block_rms(block, 4096) > 0.05f);
    CHECK(af_audio_stream_stop(audio, music, 0.02f));
    CHECK(af_audio_mix(audio, (af_vec3){0}, (af_vec3){0, 0, -1}, block, 2048));
    CHECK(af_audio_get_stats(audio).streaming == 0);

    /* Non-looping stream terminates cleanly on EOF; looping stream survives >2 full cycles */
    af_sound_stream oneshot = af_audio_stream_start(audio, qoa_path, AF_AUDIO_BUS_AMBIENCE, 0.5f, false, 0.0f, &error);
    CHECK(oneshot != 0);
    for (unsigned i = 0; i < 12; ++i)
        CHECK(af_audio_mix(audio, (af_vec3){0}, (af_vec3){0, 0, -1}, block, 4096));
    CHECK(af_audio_get_stats(audio).streaming == 0);

    af_sound_stream looping = af_audio_stream_start(audio, adpcm_path, AF_AUDIO_BUS_AMBIENCE, 0.5f, true, 0.0f, &error);
    CHECK(looping != 0);
    for (unsigned i = 0; i < 20; ++i)
        CHECK(af_audio_mix(audio, (af_vec3){0}, (af_vec3){0, 0, -1}, block, 4096));
    CHECK(af_audio_get_stats(audio).streaming == 1 && block_rms(block, 8192) > 0.1f);
    CHECK(af_audio_stream_stop(audio, looping, 0.0f));
    CHECK(af_audio_get_stats(audio).streaming == 0);

    remove(qoa_path);
    remove(adpcm_path);
    remove(long_qoa_path);

    af_audio_destroy(audio);
    puts("Audio: WAV/QOA/IMA-ADPCM codecs, >32 dynamic pools, >30s disk streaming, mix buses, soft-clipping, spatial "
         "and looping passed.");
    return 0;
}
