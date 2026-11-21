/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <afterlight/audio.h>
#include "audio_codec.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    AF_AUDIO_MAX_CLIPS = 512u,
    AF_AUDIO_MAX_VOICES = 256u,
    AF_AUDIO_MAX_STREAMS = 32u,
    AF_AUDIO_STREAM_BUFFER_FRAMES = 2048u,
    AF_AUDIO_CACHE_BYTE_LIMIT = 128u * 1024u * 1024u,
    AF_AUDIO_SAMPLE_RATE = AF_AUDIO_OUTPUT_RATE
};

typedef struct audio_clip {
    char name[64];
    float *samples;
    size_t frames, bytes;
} audio_clip;

typedef struct audio_voice {
    size_t clip, cursor;
    float gain;
    af_audio_bus bus;
    bool active, spatial;
    af_sound_emitter emitter;
    Uint64 ends;
    af_sound_loop loop;
    float fade_gain;
    size_t fade_frames;
    bool stopping;
} audio_voice;

typedef struct audio_stream_track {
    af_sound_stream id;
    af_audio_decoder *decoder;
    af_audio_bus bus;
    float gain;
    float fade_gain;
    size_t fade_frames;
    bool active, paused, loop, stopping;
    float buffer[AF_AUDIO_STREAM_BUFFER_FRAMES * 2u];
    size_t buffer_cursor;
    size_t buffer_frames;
} audio_stream_track;

struct af_audio {
    audio_clip *clips;
    size_t clip_count, clip_capacity;
    audio_voice *voices;
    size_t voice_capacity;
    audio_stream_track *streams;
    size_t stream_capacity;
    size_t bytes;
    uint64_t plays;
    uint64_t next_stream_id;
    float bus_gains[AF_AUDIO_BUS_COUNT];
    bool initialized, suspended;
    SDL_AudioStream *stream;
};

static bool finite3(af_vec3 v) {
    return isfinite(v.x) && isfinite(v.y) && isfinite(v.z);
}

static bool valid_bus(af_audio_bus bus) {
    return (unsigned)bus < (unsigned)AF_AUDIO_BUS_COUNT;
}

static bool valid_fade(float seconds) {
    return isfinite(seconds) && seconds >= 0.0f && seconds <= 60.0f;
}

static bool emitter_valid(af_sound_emitter e) {
    return finite3(e.position) && isfinite(e.gain) && e.gain >= 0.0f && e.gain <= 1.0f && isfinite(e.min_distance) &&
           isfinite(e.max_distance) && e.min_distance >= 0.01f && e.max_distance > e.min_distance &&
           e.max_distance <= 10000.0f;
}

static bool fail(af_asset_error *e, const char *message) {
    if (e)
        snprintf(e->message, sizeof(e->message), "%s", message);
    return false;
}

static inline float soft_clip(float x) {
    float ax = fabsf(x);
    if (ax <= 0.85f)
        return x;
    float knee = 0.85f + 0.15f * tanhf((ax - 0.85f) / 0.15f);
    return x < 0.0f ? -knee : knee;
}

static float effective_bus_gain(const af_audio *a, af_audio_bus bus) {
    float master = a->bus_gains[AF_AUDIO_BUS_MASTER];
    if (bus == AF_AUDIO_BUS_MASTER || !valid_bus(bus))
        return master;
    return master * a->bus_gains[bus];
}

static void close_stream_track(audio_stream_track *track) {
    if (track->decoder) {
        af_audio_decoder_close(track->decoder);
        track->decoder = NULL;
    }
    track->active = false;
    track->paused = false;
    track->stopping = false;
    track->id = 0;
    track->buffer_cursor = 0;
    track->buffer_frames = 0;
}

af_audio *af_audio_create(void) {
    af_audio *a = calloc(1, sizeof(*a));
    if (!a)
        return NULL;
    for (unsigned i = 0; i < AF_AUDIO_BUS_COUNT; ++i)
        a->bus_gains[i] = 1.0f;
    a->next_stream_id = 1;
    return a;
}

void af_audio_destroy(af_audio *a) {
    if (!a)
        return;
    SDL_DestroyAudioStream(a->stream);
    for (size_t i = 0; i < a->stream_capacity; ++i)
        close_stream_track(&a->streams[i]);
    free(a->streams);
    for (size_t i = 0; i < a->clip_count; ++i)
        free(a->clips[i].samples);
    free(a->clips);
    free(a->voices);
    if (a->initialized)
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    free(a);
}

bool af_audio_set_bus_gain(af_audio *a, af_audio_bus bus, float gain) {
    if (!a || !valid_bus(bus) || !isfinite(gain) || gain < 0.0f || gain > 2.0f)
        return false;
    a->bus_gains[bus] = gain;
    return true;
}

float af_audio_get_bus_gain(const af_audio *a, af_audio_bus bus) {
    if (!a || !valid_bus(bus))
        return 0.0f;
    return a->bus_gains[bus];
}

bool af_audio_load(af_audio *a, const char *name, const char *path, af_asset_error *e) {
    if (e)
        *e = (af_asset_error){0};
    if (!a || !name || !name[0] || strlen(name) >= 64 || !path)
        return fail(e, "Audio requires a name and audio file path.");

    float *samples = NULL;
    size_t frames = 0;
    size_t bytes = 0;
    if (!af_audio_decode_clip(path, &samples, &frames, &bytes, e))
        return false;

    size_t at = 0;
    while (at < a->clip_count && strcmp(a->clips[at].name, name) != 0)
        ++at;
    size_t previous = at < a->clip_count ? a->clips[at].bytes : 0;
    if (at >= AF_AUDIO_MAX_CLIPS || bytes > AF_AUDIO_CACHE_BYTE_LIMIT ||
        a->bytes - previous > AF_AUDIO_CACHE_BYTE_LIMIT - bytes) {
        free(samples);
        return fail(e, "Audio cache exceeds 512 clips / 128 MiB.");
    }

    if (at == a->clip_count) {
        if (a->clip_count == a->clip_capacity) {
            size_t next = a->clip_capacity ? a->clip_capacity * 2u : 32u;
            if (next > AF_AUDIO_MAX_CLIPS)
                next = AF_AUDIO_MAX_CLIPS;
            audio_clip *grown = realloc(a->clips, next * sizeof(*grown));
            if (!grown) {
                free(samples);
                return fail(e, "Out of memory growing audio clip cache.");
            }
            a->clips = grown;
            a->clip_capacity = next;
        }
        ++a->clip_count;
    } else {
        for (size_t i = 0; i < a->voice_capacity; ++i) {
            audio_voice *v = &a->voices[i];
            if (v->active && v->clip == at) {
                if (v->loop)
                    v->cursor %= frames;
                else
                    v->active = false;
            }
        }
        free(a->clips[at].samples);
    }

    a->clips[at] = (audio_clip){.samples = samples, .frames = frames, .bytes = bytes};
    snprintf(a->clips[at].name, sizeof(a->clips[at].name), "%s", name);
    a->bytes = a->bytes - previous + bytes;
    return true;
}

static bool ensure_device(af_audio *a) {
    if (!a->initialized) {
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
            return false;
        a->initialized = true;
    }
    if (!a->stream) {
        SDL_AudioSpec spec = {.format = SDL_AUDIO_F32, .channels = 2, .freq = (int)AF_AUDIO_SAMPLE_RATE};
        a->stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    }
    return a->stream && SDL_ResumeAudioStreamDevice(a->stream);
}

static audio_voice *play_internal(af_audio *a, const char *name, af_audio_bus bus, float gain,
                                  const af_sound_emitter *emitter) {
    if (!a || !name || !valid_bus(bus) || !isfinite(gain) || gain < 0.0f || gain > 1.0f)
        return NULL;
    size_t at = 0;
    while (at < a->clip_count && strcmp(a->clips[at].name, name) != 0)
        ++at;
    if (at == a->clip_count)
        return NULL;

    Uint64 now = SDL_GetTicks();
    size_t slot = 0;
    while (slot < a->voice_capacity && a->voices[slot].active && a->voices[slot].ends > now)
        ++slot;
    if (slot == a->voice_capacity) {
        if (a->voice_capacity >= AF_AUDIO_MAX_VOICES)
            return NULL;
        size_t next = a->voice_capacity ? a->voice_capacity * 2u : 32u;
        if (next > AF_AUDIO_MAX_VOICES)
            next = AF_AUDIO_MAX_VOICES;
        audio_voice *grown = realloc(a->voices, next * sizeof(*grown));
        if (!grown)
            return NULL;
        memset(grown + a->voice_capacity, 0, (next - a->voice_capacity) * sizeof(*grown));
        a->voices = grown;
        a->voice_capacity = next;
    }
    if (!ensure_device(a))
        return NULL;

    a->voices[slot] = (audio_voice){.clip = at,
                                    .active = true,
                                    .gain = gain,
                                    .bus = bus,
                                    .spatial = emitter != NULL,
                                    .ends = now + (Uint64)(a->clips[at].frames * 1000u / AF_AUDIO_SAMPLE_RATE) + 100u};
    if (emitter)
        a->voices[slot].emitter = *emitter;
    ++a->plays;
    return &a->voices[slot];
}

bool af_audio_play(af_audio *a, const char *name, float gain) {
    return play_internal(a, name, AF_AUDIO_BUS_SFX, gain, NULL) != NULL;
}

bool af_audio_play_bus(af_audio *a, const char *name, af_audio_bus bus, float gain) {
    return play_internal(a, name, bus, gain, NULL) != NULL;
}

bool af_audio_play_at(af_audio *a, const char *name, af_sound_emitter e) {
    return emitter_valid(e) && play_internal(a, name, AF_AUDIO_BUS_SFX, e.gain, &e) != NULL;
}

bool af_audio_play_at_bus(af_audio *a, const char *name, af_audio_bus bus, af_sound_emitter e) {
    return emitter_valid(e) && play_internal(a, name, bus, e.gain, &e) != NULL;
}

static audio_voice *find_loop(af_audio *a, af_sound_loop id) {
    if (!a || !id)
        return NULL;
    for (size_t i = 0; i < a->voice_capacity; ++i)
        if (a->voices[i].active && a->voices[i].loop == id)
            return &a->voices[i];
    return NULL;
}

static void apply_voice_fade(audio_voice *v, float gain, float seconds) {
    v->fade_gain = gain;
    v->fade_frames = (size_t)ceilf(seconds * (float)AF_AUDIO_SAMPLE_RATE);
    if (!v->fade_frames) {
        v->gain = gain;
        if (v->stopping)
            v->active = false;
    }
}

af_sound_loop af_audio_start_loop(af_audio *a, const char *clip_name, af_sound_emitter e, float seconds) {
    if (!emitter_valid(e) || !valid_fade(seconds))
        return 0;
    audio_voice *v = play_internal(a, clip_name, AF_AUDIO_BUS_SFX, seconds > 0.0f ? 0.0f : e.gain, &e);
    if (!v)
        return 0;
    v->loop = a->plays;
    v->ends = UINT64_MAX;
    apply_voice_fade(v, e.gain, seconds);
    return v->loop;
}

bool af_audio_move_loop(af_audio *a, af_sound_loop id, af_vec3 position) {
    audio_voice *v = find_loop(a, id);
    if (!v || !finite3(position))
        return false;
    v->emitter.position = position;
    return true;
}

bool af_audio_fade_loop(af_audio *a, af_sound_loop id, float gain, float seconds) {
    audio_voice *v = find_loop(a, id);
    if (!v || !valid_fade(seconds) || !isfinite(gain) || gain < 0.0f || gain > 1.0f)
        return false;
    v->stopping = false;
    apply_voice_fade(v, gain, seconds);
    return true;
}

bool af_audio_stop_loop(af_audio *a, af_sound_loop id, float seconds) {
    audio_voice *v = find_loop(a, id);
    if (!v || !valid_fade(seconds))
        return false;
    v->stopping = true;
    apply_voice_fade(v, 0.0f, seconds);
    return true;
}

/* --- Streaming Track API --- */

static audio_stream_track *find_stream(af_audio *a, af_sound_stream id) {
    if (!a || !id)
        return NULL;
    for (size_t i = 0; i < a->stream_capacity; ++i)
        if (a->streams[i].active && a->streams[i].id == id)
            return &a->streams[i];
    return NULL;
}

static void apply_stream_fade(audio_stream_track *t, float gain, float seconds) {
    t->fade_gain = gain;
    t->fade_frames = (size_t)ceilf(seconds * (float)AF_AUDIO_SAMPLE_RATE);
    if (!t->fade_frames) {
        t->gain = gain;
        if (t->stopping)
            close_stream_track(t);
    }
}

af_sound_stream af_audio_stream_start(af_audio *a, const char *path, af_audio_bus bus, float gain, bool loop,
                                      float fade_seconds, af_asset_error *error) {
    if (error)
        *error = (af_asset_error){0};
    if (!a || !path || !path[0] || !valid_bus(bus) || !isfinite(gain) || gain < 0.0f || gain > 1.0f ||
        !valid_fade(fade_seconds)) {
        fail(error, "Invalid audio stream parameters.");
        return 0;
    }

    size_t slot = 0;
    while (slot < a->stream_capacity && a->streams[slot].active)
        ++slot;
    if (slot == a->stream_capacity) {
        if (a->stream_capacity >= AF_AUDIO_MAX_STREAMS) {
            fail(error, "Maximum concurrent audio streams reached.");
            return 0;
        }
        size_t next = a->stream_capacity ? a->stream_capacity * 2u : 4u;
        if (next > AF_AUDIO_MAX_STREAMS)
            next = AF_AUDIO_MAX_STREAMS;
        audio_stream_track *grown = realloc(a->streams, next * sizeof(*grown));
        if (!grown) {
            fail(error, "Out of memory allocating audio stream slot.");
            return 0;
        }
        memset(grown + a->stream_capacity, 0, (next - a->stream_capacity) * sizeof(*grown));
        a->streams = grown;
        a->stream_capacity = next;
    }

    af_audio_decoder *decoder = af_audio_decoder_open(path, error);
    if (!decoder)
        return 0;
    if (!ensure_device(a)) {
        af_audio_decoder_close(decoder);
        fail(error, "Audio playback device unavailable.");
        return 0;
    }

    audio_stream_track *track = &a->streams[slot];
    memset(track, 0, sizeof(*track));
    track->id = a->next_stream_id++;
    if (a->next_stream_id == 0)
        a->next_stream_id = 1;
    track->decoder = decoder;
    track->bus = bus;
    track->gain = fade_seconds > 0.0f ? 0.0f : gain;
    track->loop = loop;
    track->active = true;
    apply_stream_fade(track, gain, fade_seconds);
    ++a->plays;
    return track->id;
}

bool af_audio_stream_pause(af_audio *a, af_sound_stream handle, bool paused) {
    audio_stream_track *t = find_stream(a, handle);
    if (!t)
        return false;
    t->paused = paused;
    return true;
}

bool af_audio_stream_fade(af_audio *a, af_sound_stream handle, float gain, float fade_seconds) {
    audio_stream_track *t = find_stream(a, handle);
    if (!t || !valid_fade(fade_seconds) || !isfinite(gain) || gain < 0.0f || gain > 1.0f)
        return false;
    t->stopping = false;
    apply_stream_fade(t, gain, fade_seconds);
    return true;
}

bool af_audio_stream_stop(af_audio *a, af_sound_stream handle, float fade_seconds) {
    audio_stream_track *t = find_stream(a, handle);
    if (!t || !valid_fade(fade_seconds))
        return false;
    t->stopping = true;
    apply_stream_fade(t, 0.0f, fade_seconds);
    return true;
}

void af_audio_suspend(af_audio *a, bool suspended) {
    if (!a || a->suspended == suspended)
        return;
    a->suspended = suspended;
    if (suspended) {
        for (size_t i = 0; i < a->voice_capacity; ++i)
            if (!a->voices[i].loop)
                a->voices[i].active = false;
        if (a->stream)
            SDL_ClearAudioStream(a->stream);
    }
}

bool af_audio_spatial_gains(af_vec3 listener, af_vec3 forward, af_sound_emitter e, float *left, float *right) {
    if (!left || !right || !finite3(listener) || !finite3(forward) || !emitter_valid(e))
        return false;
    af_vec3 d = af_vec3_sub(e.position, listener);
    float distance = af_vec3_length(d);
    if (!isfinite(distance))
        return false;
    af_vec3 axis = af_vec3_normalize((af_vec3){-forward.z, 0.0f, forward.x});
    float pan = distance > 0.0001f ? af_vec3_dot(af_vec3_scale(d, 1.0f / distance), axis) : 0.0f;
    pan = fminf(1.0f, fmaxf(-1.0f, pan));
    float t = fminf(1.0f, fmaxf(0.0f, (distance - e.min_distance) / (e.max_distance - e.min_distance)));
    float gain = e.gain * (1.0f - t) * (1.0f - t);
    *left = gain * sqrtf((1.0f - pan) * 0.5f);
    *right = gain * sqrtf((1.0f + pan) * 0.5f);
    return true;
}

static bool refill_stream_track(audio_stream_track *t) {
    t->buffer_cursor = 0;
    t->buffer_frames = af_audio_decoder_read(t->decoder, t->buffer, AF_AUDIO_STREAM_BUFFER_FRAMES);
    if (t->buffer_frames > 0)
        return true;
    if (t->loop && af_audio_decoder_rewind(t->decoder)) {
        t->buffer_frames = af_audio_decoder_read(t->decoder, t->buffer, AF_AUDIO_STREAM_BUFFER_FRAMES);
        if (t->buffer_frames > 0)
            return true;
    }
    close_stream_track(t);
    return false;
}

bool af_audio_mix(af_audio *a, af_vec3 listener, af_vec3 forward, float *out, size_t frames) {
    if (!a || !out || frames > 4096u || !finite3(listener) || !finite3(forward))
        return false;
    memset(out, 0, frames * 2u * sizeof(float));

    for (size_t vi = 0; vi < a->voice_capacity; ++vi) {
        audio_voice *v = &a->voices[vi];
        if (!v->active || (v->loop && a->suspended))
            continue;
        audio_clip *c = &a->clips[v->clip];
        float left = 1.0f, right = 1.0f;
        af_sound_emitter emitter = v->emitter;
        emitter.gain = 1.0f;
        if (v->spatial && !af_audio_spatial_gains(listener, forward, emitter, &left, &right))
            return false;
        float bus_scale = effective_bus_gain(a, v->bus);
        for (size_t i = 0; i < frames && v->active; ++i) {
            float g = v->gain * bus_scale;
            out[i * 2u] += c->samples[v->cursor * 2u] * (g * left);
            out[i * 2u + 1u] += c->samples[v->cursor * 2u + 1u] * (g * right);
            if (++v->cursor == c->frames) {
                v->cursor = 0;
                v->active = v->loop != 0;
            }
            if (v->fade_frames) {
                v->gain += (v->fade_gain - v->gain) / (float)v->fade_frames;
                if (!--v->fade_frames && v->stopping)
                    v->active = false;
            }
        }
    }

    for (size_t si = 0; si < a->stream_capacity; ++si) {
        audio_stream_track *t = &a->streams[si];
        if (!t->active || t->paused || a->suspended)
            continue;
        float bus_scale = effective_bus_gain(a, t->bus);
        for (size_t i = 0; i < frames && t->active; ++i) {
            if (t->buffer_cursor >= t->buffer_frames) {
                if (!refill_stream_track(t))
                    break;
            }
            float g = t->gain * bus_scale;
            out[i * 2u] += t->buffer[t->buffer_cursor * 2u] * g;
            out[i * 2u + 1u] += t->buffer[t->buffer_cursor * 2u + 1u] * g;
            ++t->buffer_cursor;
            if (t->fade_frames) {
                t->gain += (t->fade_gain - t->gain) / (float)t->fade_frames;
                if (!--t->fade_frames && t->stopping)
                    close_stream_track(t);
            }
        }
    }

    for (size_t i = 0; i < frames * 2u; ++i)
        out[i] = soft_clip(out[i]);
    return true;
}

void af_audio_update(af_audio *a, af_vec3 listener, af_vec3 forward) {
    if (!a || !a->stream)
        return;
    int queued = SDL_GetAudioStreamQueued(a->stream);
    if (queued < 0 || queued >= 2048 * 8)
        return;
    float samples[2048 * 2];
    size_t want = 2048u - (size_t)queued / 8u;
    if (af_audio_mix(a, listener, forward, samples, want))
        SDL_PutAudioStreamData(a->stream, samples, (int)(want * 8u));
}

void af_audio_stop(af_audio *a) {
    if (!a)
        return;
    if (a->voices && a->voice_capacity)
        memset(a->voices, 0, a->voice_capacity * sizeof(*a->voices));
    for (size_t i = 0; i < a->stream_capacity; ++i)
        close_stream_track(&a->streams[i]);
    if (a->stream)
        SDL_ClearAudioStream(a->stream);
}

af_audio_stats af_audio_get_stats(const af_audio *a) {
    if (!a)
        return (af_audio_stats){0};
    af_audio_stats s = {.clips = a->clip_count, .plays = a->plays, .device_ready = a->stream != NULL};
    Uint64 now = SDL_GetTicks();
    for (size_t i = 0; i < a->voice_capacity; ++i) {
        if (a->voices[i].active && a->voices[i].ends > now) {
            ++s.playing;
            s.looping += a->voices[i].loop != 0;
        }
    }
    for (size_t i = 0; i < a->stream_capacity; ++i) {
        if (a->streams[i].active)
            ++s.streaming;
    }
    return s;
}
