/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "audio_codec.h"

#include <SDL3/SDL_audio.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    AF_CODEC_MAX_CHANNELS = 8u,
    AF_QOA_SLICE_LEN = 20u,
    AF_QOA_MAX_SLICES = 256u,
    AF_QOA_MAX_FRAME_SAMPLES = AF_QOA_MAX_SLICES * AF_QOA_SLICE_LEN,
    AF_CLIP_MAX_SECONDS = 60u,
    AF_CLIP_MAX_FRAMES = AF_AUDIO_OUTPUT_RATE * AF_CLIP_MAX_SECONDS
};

static bool set_err(af_asset_error *err, const char *msg) {
    if (err)
        snprintf(err->message, sizeof(err->message), "%s", msg);
    return false;
}

static inline int16_t clamp_s16(int v) {
    if (v < -32768)
        return -32768;
    if (v > 32767)
        return 32767;
    return (int16_t)v;
}

static inline int arith_rshift(int value, unsigned shift) {
    if (value >= 0)
        return (int)((unsigned)value >> shift);
    unsigned mag = (unsigned)(-(value + 1)) + 1u;
    unsigned shifted = (mag + ((1u << shift) - 1u)) >> shift;
    return -(int)shifted;
}

static uint16_t read_u16_le(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8u));
}

static uint32_t read_u32_le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) | ((uint32_t)p[2] << 16u) | ((uint32_t)p[3] << 24u);
}

static uint32_t read_u32_be(const uint8_t *p) {
    return ((uint32_t)p[0] << 24u) | ((uint32_t)p[1] << 16u) | ((uint32_t)p[2] << 8u) | (uint32_t)p[3];
}

static uint64_t read_u64_be(const uint8_t *p) {
    return ((uint64_t)read_u32_be(p) << 32u) | (uint64_t)read_u32_be(p + 4);
}

static void write_u16_le(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8u) & 0xFFu);
}

static void write_u32_le(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8u) & 0xFFu);
    p[2] = (uint8_t)((v >> 16u) & 0xFFu);
    p[3] = (uint8_t)((v >> 24u) & 0xFFu);
}

static void write_u32_be(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)((v >> 24u) & 0xFFu);
    p[1] = (uint8_t)((v >> 16u) & 0xFFu);
    p[2] = (uint8_t)((v >> 8u) & 0xFFu);
    p[3] = (uint8_t)(v & 0xFFu);
}

static void write_u64_be(uint8_t *p, uint64_t v) {
    write_u32_be(p, (uint32_t)(v >> 32u));
    write_u32_be(p + 4, (uint32_t)(v & 0xFFFFFFFFu));
}

/* --- QOA tables & LMS filter (public QOA specification) --- */

static const int qoa_scalefactor_tab[16] = {
    1, 7, 21, 45, 84, 138, 211, 304, 421, 562, 731, 928, 1157, 1419, 1715, 2048};

static const int qoa_reciprocal_tab[16] = {
    65536, 9363, 3121, 1457, 781, 475, 311, 216, 156, 117, 90, 71, 57, 47, 39, 32};

static const int qoa_quant_tab[17] = {7, 7, 7, 5, 5, 3, 3, 1, 0, 0, 2, 2, 4, 4, 6, 6, 6};

static int qoa_dequant(int sf_quant, int qr) {
    int sf = qoa_scalefactor_tab[sf_quant & 15];
    int mag = 0;
    switch (qr & 6) {
    case 0:
        mag = (sf * 3 + 2) / 4;
        break;
    case 2:
        mag = (sf * 5 + 1) / 2;
        break;
    case 4:
        mag = (sf * 9 + 1) / 2;
        break;
    default:
        mag = sf * 7;
        break;
    }
    return (qr & 1) ? -mag : mag;
}

static int qoa_div(int v, int sf_quant) {
    int reciprocal = qoa_reciprocal_tab[sf_quant & 15];
    int n = arith_rshift(v * reciprocal + (1 << 15), 16);
    n = n + ((v > 0) - (v < 0)) - ((n > 0) - (n < 0));
    return n;
}

typedef struct qoa_lms {
    int history[4];
    int weights[4];
} qoa_lms;

static inline int qoa_lms_predict(const qoa_lms *lms) {
    int pred = 0;
    for (int i = 0; i < 4; ++i)
        pred += lms->weights[i] * lms->history[i];
    return arith_rshift(pred, 13u);
}

static inline void qoa_lms_update(qoa_lms *lms, int sample, int residual) {
    int delta = arith_rshift(residual, 4u);
    for (int i = 0; i < 4; ++i)
        lms->weights[i] += lms->history[i] < 0 ? -delta : delta;
    lms->history[0] = lms->history[1];
    lms->history[1] = lms->history[2];
    lms->history[2] = lms->history[3];
    lms->history[3] = sample;
}

/* --- IMA-ADPCM tables (DVI / Microsoft WAV format 0x0011) --- */

static const int ima_index_tab[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};

static const int ima_step_tab[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,    25,    28,
    31,    34,    37,    41,    45,    50,    55,    60,    66,    73,    80,    88,    97,    107,   118,
    130,   143,   157,   173,   190,   209,   230,   253,   279,   307,   337,   371,   408,   449,   494,
    544,   598,   658,   724,   796,   876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
    2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,
    9493,  10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};

static inline int clamp_step_idx(int idx) {
    return idx < 0 ? 0 : (idx > 88 ? 88 : idx);
}

static int16_t ima_decode_nibble(uint8_t nibble, int *predictor, int *step_index) {
    int step = ima_step_tab[*step_index];
    int diff = step >> 3;
    if (nibble & 1u)
        diff += step >> 2;
    if (nibble & 2u)
        diff += step >> 1;
    if (nibble & 4u)
        diff += step;
    if (nibble & 8u)
        *predictor -= diff;
    else
        *predictor += diff;
    *predictor = clamp_s16(*predictor);
    *step_index = clamp_step_idx(*step_index + ima_index_tab[nibble & 15u]);
    return (int16_t)(*predictor);
}

static uint8_t ima_encode_sample(int16_t sample, int *predictor, int *step_index) {
    int step = ima_step_tab[*step_index];
    int diff = (int)sample - *predictor;
    uint8_t nibble = 0;
    if (diff < 0) {
        nibble = 8u;
        diff = -diff;
    }
    int vpdiff = step >> 3;
    if (diff >= step) {
        nibble |= 4u;
        diff -= step;
        vpdiff += step;
    }
    step >>= 1;
    if (diff >= step) {
        nibble |= 2u;
        diff -= step;
        vpdiff += step;
    }
    step >>= 1;
    if (diff >= step) {
        nibble |= 1u;
        vpdiff += step;
    }
    if (nibble & 8u)
        *predictor -= vpdiff;
    else
        *predictor += vpdiff;
    *predictor = clamp_s16(*predictor);
    *step_index = clamp_step_idx(*step_index + ima_index_tab[nibble]);
    return nibble;
}

/* --- Unified Streaming Decoder --- */

typedef enum codec_kind {
    CODEC_WAV_PCM = 0,
    CODEC_WAV_FLOAT = 1,
    CODEC_WAV_IMA_ADPCM = 2,
    CODEC_QOA = 3
} codec_kind;

struct af_audio_decoder {
    FILE *file;
    codec_kind kind;
    uint32_t channels;
    uint32_t sample_rate;
    uint16_t bits_per_sample;
    uint16_t block_align;
    long data_offset;
    uint64_t data_bytes;
    uint64_t bytes_remaining;
    bool eof_flushed;

    SDL_AudioStream *resampler;
    uint8_t *io_buf;
    size_t io_buf_cap;
    int16_t *pcm_buf;
    size_t pcm_buf_cap;
    float *flt_buf;
    size_t flt_buf_cap;
};

static bool open_qoa(af_audio_decoder *dec, const uint8_t header[8], af_asset_error *err) {
    (void)header;
    uint8_t fhdr[8];
    if (fread(fhdr, 1, 8, dec->file) != 8)
        return set_err(err, "QOA file has no frames.");
    uint64_t h = read_u64_be(fhdr);
    uint32_t channels = (uint32_t)((h >> 56u) & 0xFFu);
    uint32_t sample_rate = (uint32_t)((h >> 32u) & 0xFFFFFFu);
    uint32_t fsamples = (uint32_t)((h >> 16u) & 0xFFFFu);
    uint32_t fsize = (uint32_t)(h & 0xFFFFu);
    if (channels < 1u || channels > AF_CODEC_MAX_CHANNELS || sample_rate < 8000u || sample_rate > 192000u ||
        fsamples == 0u || fsamples > AF_QOA_MAX_FRAME_SAMPLES || fsize < 8u + channels * 16u)
        return set_err(err, "Invalid QOA frame header.");
    if (fseek(dec->file, 8, SEEK_SET) != 0)
        return set_err(err, "QOA seek failed.");

    dec->kind = CODEC_QOA;
    dec->channels = channels;
    dec->sample_rate = sample_rate;
    dec->bits_per_sample = 16;
    dec->data_offset = 8;
    dec->io_buf_cap = 65536u;
    dec->io_buf = malloc(dec->io_buf_cap);
    dec->pcm_buf_cap = (size_t)AF_QOA_MAX_FRAME_SAMPLES * channels;
    dec->pcm_buf = malloc(dec->pcm_buf_cap * sizeof(int16_t));
    if (!dec->io_buf || !dec->pcm_buf)
        return set_err(err, "Out of memory initializing QOA decoder.");
    return true;
}

static bool open_wav(af_audio_decoder *dec, const uint8_t riff_hdr[12], af_asset_error *err) {
    if (memcmp(riff_hdr + 8, "WAVE", 4) != 0)
        return set_err(err, "Not a valid WAVE file.");
    bool got_fmt = false, got_data = false;
    uint16_t fmt_tag = 0;

    while (!got_data) {
        uint8_t ch[8];
        if (fread(ch, 1, 8, dec->file) != 8)
            break;
        uint32_t chunk_size = read_u32_le(ch + 4);
        long chunk_start = ftell(dec->file);
        if (chunk_start < 0)
            return set_err(err, "WAV tell failed.");

        if (memcmp(ch, "fmt ", 4) == 0) {
            if (chunk_size < 16u)
                return set_err(err, "WAV fmt chunk too small.");
            uint8_t fmt[40];
            size_t to_read = chunk_size < sizeof(fmt) ? chunk_size : sizeof(fmt);
            if (fread(fmt, 1, to_read, dec->file) != to_read)
                return set_err(err, "Truncated WAV fmt chunk.");
            fmt_tag = read_u16_le(fmt + 0);
            dec->channels = read_u16_le(fmt + 2);
            dec->sample_rate = read_u32_le(fmt + 4);
            dec->block_align = read_u16_le(fmt + 12);
            dec->bits_per_sample = read_u16_le(fmt + 14);
            if (fmt_tag == 0xFFFEu && to_read >= 40u)
                fmt_tag = read_u16_le(fmt + 24);
            got_fmt = true;
        } else if (memcmp(ch, "data", 4) == 0) {
            dec->data_offset = chunk_start;
            dec->data_bytes = chunk_size;
            dec->bytes_remaining = chunk_size;
            got_data = true;
            break;
        }

        long next_pos = chunk_start + (long)chunk_size + (long)(chunk_size & 1u);
        if (fseek(dec->file, next_pos, SEEK_SET) != 0)
            break;
    }

    if (!got_fmt || !got_data || dec->data_bytes == 0u)
        return set_err(err, "WAV missing fmt or data chunk.");
    if (dec->channels < 1u || dec->channels > AF_CODEC_MAX_CHANNELS || dec->sample_rate < 8000u ||
        dec->sample_rate > 192000u)
        return set_err(err, "WAV must use 1-8 channels and 8-192 kHz sample rate.");

    if (fmt_tag == 1u) {
        if (dec->bits_per_sample != 8u && dec->bits_per_sample != 16u && dec->bits_per_sample != 24u &&
            dec->bits_per_sample != 32u)
            return set_err(err, "Unsupported WAV PCM bit depth.");
        dec->kind = CODEC_WAV_PCM;
        if (dec->block_align == 0u)
            dec->block_align = (uint16_t)(dec->channels * (dec->bits_per_sample / 8u));
        dec->io_buf_cap = (size_t)dec->block_align * 1024u;
        dec->flt_buf_cap = (size_t)dec->channels * 1024u;
        dec->io_buf = malloc(dec->io_buf_cap);
        dec->flt_buf = malloc(dec->flt_buf_cap * sizeof(float));
        if (!dec->io_buf || !dec->flt_buf)
            return set_err(err, "Out of memory initializing PCM decoder.");
    } else if (fmt_tag == 3u) {
        if (dec->bits_per_sample != 32u)
            return set_err(err, "Only 32-bit IEEE float WAV is supported.");
        dec->kind = CODEC_WAV_FLOAT;
        if (dec->block_align == 0u)
            dec->block_align = (uint16_t)(dec->channels * 4u);
        dec->io_buf_cap = (size_t)dec->block_align * 1024u;
        dec->flt_buf_cap = (size_t)dec->channels * 1024u;
        dec->io_buf = malloc(dec->io_buf_cap);
        dec->flt_buf = malloc(dec->flt_buf_cap * sizeof(float));
        if (!dec->io_buf || !dec->flt_buf)
            return set_err(err, "Out of memory initializing float WAV decoder.");
    } else if (fmt_tag == 0x0011u) {
        if (dec->bits_per_sample != 4u || (dec->channels != 1u && dec->channels != 2u) ||
            dec->block_align < dec->channels * 4u || ((dec->block_align - dec->channels * 4u) % (dec->channels * 4u)) != 0u)
            return set_err(err, "Invalid IMA-ADPCM WAV parameters.");
        dec->kind = CODEC_WAV_IMA_ADPCM;
        size_t samples_per_block = 1u + ((size_t)(dec->block_align - dec->channels * 4u) * 2u) / dec->channels;
        dec->io_buf_cap = dec->block_align;
        dec->pcm_buf_cap = samples_per_block * dec->channels;
        dec->io_buf = malloc(dec->io_buf_cap);
        dec->pcm_buf = malloc(dec->pcm_buf_cap * sizeof(int16_t));
        if (!dec->io_buf || !dec->pcm_buf)
            return set_err(err, "Out of memory initializing IMA-ADPCM decoder.");
    } else {
        return set_err(err, "Unsupported WAV audio format.");
    }

    return true;
}

static bool decode_next_chunk_to_resampler(af_audio_decoder *dec) {
    if (dec->kind == CODEC_QOA) {
        uint8_t fhdr[8];
        if (fread(fhdr, 1, 8, dec->file) != 8)
            return false;
        uint64_t h = read_u64_be(fhdr);
        uint32_t channels = (uint32_t)((h >> 56u) & 0xFFu);
        uint32_t sample_rate = (uint32_t)((h >> 32u) & 0xFFFFFFu);
        uint32_t fsamples = (uint32_t)((h >> 16u) & 0xFFFFu);
        uint32_t fsize = (uint32_t)(h & 0xFFFFu);
        if (channels != dec->channels || sample_rate != dec->sample_rate || fsamples == 0u ||
            fsamples > AF_QOA_MAX_FRAME_SAMPLES || fsize < 8u + channels * 16u || fsize > dec->io_buf_cap)
            return false;
        size_t payload = fsize - 8u;
        if (fread(dec->io_buf, 1, payload, dec->file) != payload)
            return false;

        qoa_lms lms[AF_CODEC_MAX_CHANNELS];
        size_t pos = 0;
        for (uint32_t c = 0; c < channels; ++c) {
            uint64_t hist = read_u64_be(dec->io_buf + pos);
            pos += 8u;
            uint64_t wts = read_u64_be(dec->io_buf + pos);
            pos += 8u;
            for (int i = 0; i < 4; ++i) {
                unsigned shift = (unsigned)(48 - i * 16);
                lms[c].history[i] = (int)(int16_t)((hist >> shift) & 0xFFFFu);
                lms[c].weights[i] = (int)(int16_t)((wts >> shift) & 0xFFFFu);
            }
        }

        for (uint32_t sample_idx = 0; sample_idx < fsamples; sample_idx += AF_QOA_SLICE_LEN) {
            uint32_t slice_len = fsamples - sample_idx;
            if (slice_len > AF_QOA_SLICE_LEN)
                slice_len = AF_QOA_SLICE_LEN;
            for (uint32_t c = 0; c < channels; ++c) {
                if (pos + 8u > payload)
                    return false;
                uint64_t slice = read_u64_be(dec->io_buf + pos);
                pos += 8u;
                int sf_quant = (int)((slice >> 60u) & 0xFu);
                for (uint32_t si = 0; si < slice_len; ++si) {
                    int qr = (int)((slice >> (57u - si * 3u)) & 0x7u);
                    int predicted = qoa_lms_predict(&lms[c]);
                    int dequant = qoa_dequant(sf_quant, qr);
                    int16_t sample = clamp_s16(predicted + dequant);
                    dec->pcm_buf[(sample_idx + si) * channels + c] = sample;
                    qoa_lms_update(&lms[c], (int)sample, dequant);
                }
            }
        }
        return SDL_PutAudioStreamData(dec->resampler, dec->pcm_buf,
                                      (int)(fsamples * channels * sizeof(int16_t)));
    }

    if (dec->bytes_remaining == 0u)
        return false;

    if (dec->kind == CODEC_WAV_IMA_ADPCM) {
        size_t to_read = dec->block_align;
        if (dec->bytes_remaining < to_read)
            to_read = (size_t)dec->bytes_remaining;
        if (to_read < dec->channels * 4u) {
            dec->bytes_remaining = 0u;
            return false;
        }
        size_t got = fread(dec->io_buf, 1, to_read, dec->file);
        dec->bytes_remaining -= got;
        if (got < dec->channels * 4u)
            return false;

        int predictor[2] = {0, 0};
        int step_index[2] = {0, 0};
        size_t pos = 0;
        for (uint32_t c = 0; c < dec->channels; ++c) {
            predictor[c] = (int)(int16_t)read_u16_le(dec->io_buf + pos);
            step_index[c] = clamp_step_idx((int)dec->io_buf[pos + 2u]);
            pos += 4u;
            dec->pcm_buf[c] = (int16_t)predictor[c];
        }
        size_t frame = 1u;
        size_t chunk_bytes = (size_t)dec->channels * 4u;
        while (pos + chunk_bytes <= got) {
            for (uint32_t c = 0; c < dec->channels; ++c) {
                for (unsigned b = 0; b < 4u; ++b) {
                    uint8_t byte = dec->io_buf[pos + b];
                    size_t f0 = frame + b * 2u;
                    size_t f1 = f0 + 1u;
                    dec->pcm_buf[f0 * dec->channels + c] =
                        ima_decode_nibble((uint8_t)(byte & 0x0Fu), &predictor[c], &step_index[c]);
                    dec->pcm_buf[f1 * dec->channels + c] =
                        ima_decode_nibble((uint8_t)((byte >> 4u) & 0x0Fu), &predictor[c], &step_index[c]);
                }
                pos += 4u;
            }
            frame += 8u;
        }
        return SDL_PutAudioStreamData(dec->resampler, dec->pcm_buf,
                                      (int)(frame * dec->channels * sizeof(int16_t)));
    }

    /* PCM or IEEE Float WAV */
    size_t max_bytes = dec->io_buf_cap;
    if (dec->bytes_remaining < max_bytes)
        max_bytes = (size_t)dec->bytes_remaining;
    max_bytes -= max_bytes % dec->block_align;
    if (max_bytes == 0u) {
        dec->bytes_remaining = 0u;
        return false;
    }
    size_t got = fread(dec->io_buf, 1, max_bytes, dec->file);
    dec->bytes_remaining -= got;
    size_t frames = got / dec->block_align;
    if (frames == 0u)
        return false;

    size_t total_samples = frames * dec->channels;
    if (dec->kind == CODEC_WAV_FLOAT) {
        for (size_t i = 0; i < total_samples; ++i) {
            uint32_t bits = read_u32_le(dec->io_buf + i * 4u);
            float sample = 0.0f;
            memcpy(&sample, &bits, sizeof(sample));
            if (!isfinite(sample))
                return false;
            dec->flt_buf[i] = sample;
        }
    } else if (dec->bits_per_sample == 8u) {
        for (size_t i = 0; i < total_samples; ++i)
            dec->flt_buf[i] = ((float)dec->io_buf[i] - 128.0f) * (1.0f / 128.0f);
    } else if (dec->bits_per_sample == 16u) {
        for (size_t i = 0; i < total_samples; ++i) {
            int16_t s = (int16_t)read_u16_le(dec->io_buf + i * 2u);
            dec->flt_buf[i] = (float)s * (1.0f / 32768.0f);
        }
    } else if (dec->bits_per_sample == 24u) {
        for (size_t i = 0; i < total_samples; ++i) {
            const uint8_t *p = dec->io_buf + i * 3u;
            int32_t v = (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8u) | ((uint32_t)p[2] << 16u));
            if (v & 0x800000)
                v |= ~0xFFFFFF;
            dec->flt_buf[i] = (float)v * (1.0f / 8388608.0f);
        }
    } else {
        for (size_t i = 0; i < total_samples; ++i) {
            int32_t v = (int32_t)read_u32_le(dec->io_buf + i * 4u);
            dec->flt_buf[i] = (float)((double)v / 2147483648.0);
        }
    }

    return SDL_PutAudioStreamData(dec->resampler, dec->flt_buf, (int)(total_samples * sizeof(float)));
}

af_audio_decoder *af_audio_decoder_open(const char *path, af_asset_error *error) {
    if (error)
        *error = (af_asset_error){0};
    if (!path || !path[0]) {
        set_err(error, "Audio path is required.");
        return NULL;
    }
    FILE *f = fopen(path, "rb");
    if (!f) {
        set_err(error, "Cannot open audio file.");
        return NULL;
    }
    uint8_t magic[12] = {0};
    size_t hdr_read = fread(magic, 1, 12, f);
    if (hdr_read < 8u) {
        fclose(f);
        set_err(error, "Audio file is too small.");
        return NULL;
    }

    af_audio_decoder *dec = calloc(1, sizeof(*dec));
    if (!dec) {
        fclose(f);
        set_err(error, "Out of memory opening audio decoder.");
        return NULL;
    }
    dec->file = f;

    bool ok = false;
    if (memcmp(magic, "qoaf", 4) == 0) {
        if (fseek(f, 8, SEEK_SET) == 0)
            ok = open_qoa(dec, magic, error);
    } else if (hdr_read >= 12u && memcmp(magic, "RIFF", 4) == 0) {
        ok = open_wav(dec, magic, error);
    } else {
        set_err(error, "Unsupported audio container (expected WAV or QOA).");
    }

    if (!ok) {
        af_audio_decoder_close(dec);
        return NULL;
    }

    SDL_AudioSpec src = {.format = (dec->kind == CODEC_QOA || dec->kind == CODEC_WAV_IMA_ADPCM) ? SDL_AUDIO_S16
                                                                                                : SDL_AUDIO_F32,
                         .channels = (int)dec->channels,
                         .freq = (int)dec->sample_rate};
    SDL_AudioSpec dst = {.format = SDL_AUDIO_F32, .channels = AF_AUDIO_OUTPUT_CHANNELS, .freq = AF_AUDIO_OUTPUT_RATE};
    dec->resampler = SDL_CreateAudioStream(&src, &dst);
    if (!dec->resampler) {
        set_err(error, SDL_GetError());
        af_audio_decoder_close(dec);
        return NULL;
    }
    return dec;
}

void af_audio_decoder_close(af_audio_decoder *dec) {
    if (!dec)
        return;
    if (dec->resampler)
        SDL_DestroyAudioStream(dec->resampler);
    if (dec->file)
        fclose(dec->file);
    free(dec->io_buf);
    free(dec->pcm_buf);
    free(dec->flt_buf);
    free(dec);
}

size_t af_audio_decoder_read(af_audio_decoder *dec, float *out_stereo, size_t max_frames) {
    if (!dec || !out_stereo || max_frames == 0u)
        return 0;
    size_t want_bytes = max_frames * AF_AUDIO_OUTPUT_CHANNELS * sizeof(float);
    while ((size_t)SDL_GetAudioStreamAvailable(dec->resampler) < want_bytes && !dec->eof_flushed) {
        if (!decode_next_chunk_to_resampler(dec)) {
            SDL_FlushAudioStream(dec->resampler);
            dec->eof_flushed = true;
            break;
        }
    }
    int avail = SDL_GetAudioStreamAvailable(dec->resampler);
    if (avail <= 0)
        return 0;
    size_t pull_bytes = (size_t)avail < want_bytes ? (size_t)avail : want_bytes;
    pull_bytes -= pull_bytes % (AF_AUDIO_OUTPUT_CHANNELS * sizeof(float));
    if (pull_bytes == 0u)
        return 0;
    int got = SDL_GetAudioStreamData(dec->resampler, out_stereo, (int)pull_bytes);
    if (got <= 0)
        return 0;
    size_t frames = (size_t)got / (AF_AUDIO_OUTPUT_CHANNELS * sizeof(float));
    for (size_t i = 0; i < frames * AF_AUDIO_OUTPUT_CHANNELS; ++i) {
        if (!isfinite(out_stereo[i]))
            out_stereo[i] = 0.0f;
    }
    return frames;
}

bool af_audio_decoder_rewind(af_audio_decoder *dec) {
    if (!dec || !dec->file)
        return false;
    if (fseek(dec->file, dec->data_offset, SEEK_SET) != 0)
        return false;
    dec->bytes_remaining = dec->data_bytes;
    dec->eof_flushed = false;
    SDL_ClearAudioStream(dec->resampler);
    return true;
}

bool af_audio_decode_clip(const char *path, float **out_samples, size_t *out_frames, size_t *out_bytes,
                          af_asset_error *error) {
    if (!out_samples || !out_frames || !out_bytes)
        return set_err(error, "Invalid clip output pointers.");
    *out_samples = NULL;
    *out_frames = 0;
    *out_bytes = 0;

    af_audio_decoder *dec = af_audio_decoder_open(path, error);
    if (!dec)
        return false;

    size_t cap = 4096u;
    size_t count = 0;
    float *buf = malloc(cap * AF_AUDIO_OUTPUT_CHANNELS * sizeof(float));
    if (!buf) {
        af_audio_decoder_close(dec);
        return set_err(error, "Out of memory decoding audio clip.");
    }

    for (;;) {
        if (count + 2048u > AF_CLIP_MAX_FRAMES) {
            /* Check if any more frames remain; clips over 60 s should use streaming. */
            float probe[64];
            if (af_audio_decoder_read(dec, probe, 32u) > 0u) {
                free(buf);
                af_audio_decoder_close(dec);
                return set_err(error, "Audio clip exceeds 60 seconds; use af_audio_stream_start for long tracks.");
            }
            break;
        }
        if (count + 2048u > cap) {
            size_t next_cap = cap * 2u;
            if (next_cap > AF_CLIP_MAX_FRAMES)
                next_cap = AF_CLIP_MAX_FRAMES;
            float *grown = realloc(buf, next_cap * AF_AUDIO_OUTPUT_CHANNELS * sizeof(float));
            if (!grown) {
                free(buf);
                af_audio_decoder_close(dec);
                return set_err(error, "Out of memory growing audio clip buffer.");
            }
            buf = grown;
            cap = next_cap;
        }
        size_t got = af_audio_decoder_read(dec, buf + count * AF_AUDIO_OUTPUT_CHANNELS, 2048u);
        if (got == 0u)
            break;
        count += got;
    }

    af_audio_decoder_close(dec);
    if (count == 0u) {
        free(buf);
        return set_err(error, "Audio file contains no decodable samples.");
    }
    *out_samples = buf;
    *out_frames = count;
    *out_bytes = count * AF_AUDIO_OUTPUT_CHANNELS * sizeof(float);
    return true;
}

/* --- Utility Encoders (QOA + IMA-ADPCM WAV) --- */

bool af_qoa_encode_file(const char *path, const int16_t *pcm, uint32_t frames, uint32_t channels,
                        uint32_t sample_rate) {
    if (!path || !pcm || frames == 0u || channels < 1u || channels > AF_CODEC_MAX_CHANNELS || sample_rate < 8000u ||
        sample_rate > 192000u)
        return false;
    FILE *f = fopen(path, "wb");
    if (!f)
        return false;

    uint8_t file_hdr[8];
    memcpy(file_hdr, "qoaf", 4);
    write_u32_be(file_hdr + 4, frames);
    if (fwrite(file_hdr, 1, 8, f) != 8) {
        fclose(f);
        return false;
    }

    qoa_lms lms[AF_CODEC_MAX_CHANNELS];
    for (uint32_t c = 0; c < channels; ++c) {
        memset(&lms[c], 0, sizeof(lms[c]));
        lms[c].weights[2] = -(1 << 13);
        lms[c].weights[3] = (1 << 14);
    }

    uint32_t frame_start = 0;
    while (frame_start < frames) {
        uint32_t fsamples = frames - frame_start;
        if (fsamples > AF_QOA_MAX_FRAME_SAMPLES)
            fsamples = AF_QOA_MAX_FRAME_SAMPLES;
        uint32_t slices = (fsamples + AF_QOA_SLICE_LEN - 1u) / AF_QOA_SLICE_LEN;
        uint32_t fsize = 8u + channels * 16u + slices * channels * 8u;

        uint64_t fhdr_u64 = ((uint64_t)channels << 56u) | ((uint64_t)sample_rate << 32u) |
                            ((uint64_t)fsamples << 16u) | (uint64_t)fsize;
        uint8_t fhdr[8];
        write_u64_be(fhdr, fhdr_u64);
        if (fwrite(fhdr, 1, 8, f) != 8) {
            fclose(f);
            return false;
        }

        for (uint32_t c = 0; c < channels; ++c) {
            uint64_t hist = 0, wts = 0;
            for (int i = 0; i < 4; ++i) {
                unsigned shift = (unsigned)(48 - i * 16);
                hist |= ((uint64_t)(uint16_t)(int16_t)lms[c].history[i]) << shift;
                wts |= ((uint64_t)(uint16_t)(int16_t)lms[c].weights[i]) << shift;
            }
            uint8_t lms_bytes[16];
            write_u64_be(lms_bytes, hist);
            write_u64_be(lms_bytes + 8, wts);
            if (fwrite(lms_bytes, 1, 16, f) != 16) {
                fclose(f);
                return false;
            }
        }

        for (uint32_t si = 0; si < fsamples; si += AF_QOA_SLICE_LEN) {
            uint32_t slen = fsamples - si;
            if (slen > AF_QOA_SLICE_LEN)
                slen = AF_QOA_SLICE_LEN;
            for (uint32_t c = 0; c < channels; ++c) {
                uint64_t best_rank = UINT64_MAX;
                uint64_t best_slice = 0;
                qoa_lms best_lms = lms[c];

                for (int sf = 0; sf < 16; ++sf) {
                    qoa_lms trial = lms[c];
                    uint64_t slice = (uint64_t)sf;
                    uint64_t current_rank = 0;
                    for (uint32_t k = 0; k < slen; ++k) {
                        int sample = (int)pcm[(frame_start + si + k) * channels + c];
                        int pred = qoa_lms_predict(&trial);
                        int residual = sample - pred;
                        int scaled = qoa_div(residual, sf);
                        int clamped = scaled < -8 ? -8 : (scaled > 8 ? 8 : scaled);
                        int qr = qoa_quant_tab[clamped + 8];
                        int deq = qoa_dequant(sf, qr);
                        int recon = (int)clamp_s16(pred + deq);

                        int weights_penalty = ((trial.weights[0] * trial.weights[0] + trial.weights[1] * trial.weights[1] +
                                                trial.weights[2] * trial.weights[2] + trial.weights[3] * trial.weights[3]) >>
                                               18) -
                                              0x8FF;
                        if (weights_penalty < 0)
                            weights_penalty = 0;
                        int64_t err = (int64_t)sample - (int64_t)recon;
                        current_rank += (uint64_t)(err * err) + (uint64_t)weights_penalty * (uint64_t)weights_penalty;
                        if (current_rank >= best_rank)
                            break;
                        qoa_lms_update(&trial, recon, deq);
                        slice = (slice << 3u) | (uint64_t)qr;
                    }
                    if (current_rank < best_rank) {
                        best_rank = current_rank;
                        best_slice = slice << ((AF_QOA_SLICE_LEN - slen) * 3u);
                        best_lms = trial;
                    }
                }

                lms[c] = best_lms;
                uint8_t sbytes[8];
                write_u64_be(sbytes, best_slice);
                if (fwrite(sbytes, 1, 8, f) != 8) {
                    fclose(f);
                    return false;
                }
            }
        }
        frame_start += fsamples;
    }

    fclose(f);
    return true;
}

bool af_ima_adpcm_encode_wav(const char *path, const int16_t *pcm, uint32_t frames, uint32_t channels,
                             uint32_t sample_rate) {
    if (!path || !pcm || frames == 0u || (channels != 1u && channels != 2u) || sample_rate < 8000u ||
        sample_rate > 192000u)
        return false;
    FILE *f = fopen(path, "wb");
    if (!f)
        return false;

    const uint16_t block_align = (uint16_t)(256u * channels);
    const uint16_t samples_per_block = (uint16_t)(1u + ((block_align - channels * 4u) * 2u) / channels);
    const uint32_t block_count = (frames + samples_per_block - 1u) / samples_per_block;
    const uint32_t data_bytes = block_count * block_align;
    const uint32_t riff_size = 4u + (8u + 20u) + (8u + 4u) + (8u + data_bytes);
    const uint32_t avg_bytes_per_sec = (uint32_t)((uint64_t)sample_rate * block_align / samples_per_block);

    uint8_t hdr[60];
    memcpy(hdr + 0, "RIFF", 4);
    write_u32_le(hdr + 4, riff_size);
    memcpy(hdr + 8, "WAVE", 4);
    memcpy(hdr + 12, "fmt ", 4);
    write_u32_le(hdr + 16, 20u);
    write_u16_le(hdr + 20, 0x0011u);
    write_u16_le(hdr + 22, (uint16_t)channels);
    write_u32_le(hdr + 24, sample_rate);
    write_u32_le(hdr + 28, avg_bytes_per_sec);
    write_u16_le(hdr + 32, block_align);
    write_u16_le(hdr + 34, 4u);
    write_u16_le(hdr + 36, 2u);
    write_u16_le(hdr + 38, samples_per_block);
    memcpy(hdr + 40, "fact", 4);
    write_u32_le(hdr + 44, 4u);
    write_u32_le(hdr + 48, frames);
    memcpy(hdr + 52, "data", 4);
    write_u32_le(hdr + 56, data_bytes);
    if (fwrite(hdr, 1, sizeof(hdr), f) != sizeof(hdr)) {
        fclose(f);
        return false;
    }

    uint8_t block[512];
    int step_index[2] = {0, 0};
    for (uint32_t bi = 0; bi < block_count; ++bi) {
        memset(block, 0, block_align);
        uint32_t base = bi * samples_per_block;
        int predictor[2] = {0, 0};
        size_t pos = 0;
        for (uint32_t c = 0; c < channels; ++c) {
            int16_t first = base < frames ? pcm[base * channels + c] : 0;
            predictor[c] = (int)first;
            write_u16_le(block + pos, (uint16_t)first);
            block[pos + 2u] = (uint8_t)step_index[c];
            block[pos + 3u] = 0u;
            pos += 4u;
        }
        uint32_t f_idx = 1u;
        while (pos + channels * 4u <= block_align) {
            for (uint32_t c = 0; c < channels; ++c) {
                for (unsigned b = 0; b < 4u; ++b) {
                    uint32_t s0 = base + f_idx + b * 2u;
                    uint32_t s1 = s0 + 1u;
                    int16_t v0 = s0 < frames ? pcm[s0 * channels + c] : 0;
                    int16_t v1 = s1 < frames ? pcm[s1 * channels + c] : 0;
                    uint8_t n0 = ima_encode_sample(v0, &predictor[c], &step_index[c]);
                    uint8_t n1 = ima_encode_sample(v1, &predictor[c], &step_index[c]);
                    block[pos + b] = (uint8_t)((n0 & 0x0Fu) | ((n1 & 0x0Fu) << 4u));
                }
                pos += 4u;
            }
            f_idx += 8u;
        }
        if (fwrite(block, 1, block_align, f) != block_align) {
            fclose(f);
            return false;
        }
    }

    fclose(f);
    return true;
}
