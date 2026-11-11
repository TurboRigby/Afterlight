/* SPDX-License-Identifier: MPL-2.0 */
#include <afterlight/animation.h>
#include "assets/gltf.h"
#include <errno.h>
#include <float.h>
#include <inttypes.h>
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

static const unsigned char magic[8] = {'A', 'F', 'A', 'N', 'I', 'M', 1, 0};
_Static_assert(sizeof(float) == 4 && sizeof(double) == 8, "Native animation stores 32-bit floats and 64-bit seconds");
static bool failure(af_asset_error *error, const char *message) {
    if (error)
        snprintf(error->message, sizeof(error->message), "%s", message);
    return false;
}
static bool finite_values(const float *values, size_t count) {
    for (size_t i = 0; i < count; ++i)
        if (!isfinite(values[i]))
            return false;
    return true;
}
static bool valid_rotation(af_vec4 q) {
    float squared = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
    return isfinite(squared) && fabsf(squared - 1) <= .021f;
}
bool af_animation_pose_valid(const af_animation_pose *p) {
    if (!p || p->morph_count > AF_ANIMATION_MAX_MORPHS)
        return false;
    float trs[] = {p->translation.x, p->translation.y, p->translation.z, p->scale.x, p->scale.y, p->scale.z};
    return finite_values(trs, 6) && valid_rotation(p->rotation) && finite_values(p->morph_weights, p->morph_count);
}
bool af_animation_file_validate(const af_animation_file *f) {
    if (!f || !f->node_count || f->node_count > AF_ANIMATION_MAX_NODES || !f->bind_pose || !f->parents ||
        f->skin_count > 128 || (f->skin_count && !f->skins) || f->clip_count > AF_ANIMATION_MAX_CLIPS ||
        (f->clip_count && !f->clips))
        return false;
    for (size_t i = 0; i < f->node_count; ++i)
        if (!af_animation_pose_valid(&f->bind_pose[i]) || f->parents[i] < -1 || f->parents[i] >= (int32_t)f->node_count)
            return false;
    af_mat4 *world = calloc(f->node_count, sizeof(*world));
    bool hierarchy = world && af_animation_world_matrices(f, f->bind_pose, world, f->node_count);
    free(world);
    if (!hierarchy)
        return false;
    for (size_t i = 0; i < f->skin_count; ++i) {
        const af_animation_skin *skin = &f->skins[i];
        if (!skin->joint_count || skin->joint_count > AF_ANIMATION_MAX_JOINTS)
            return false;
        for (size_t j = 0; j < skin->joint_count; ++j) {
            const float *m = skin->inverse_bind[j].elements;
            if (skin->joints[j] >= f->node_count || !finite_values(m, 16) ||
                fabsf(m[3]) + fabsf(m[7]) + fabsf(m[11]) + fabsf(m[15] - 1) > 1e-5f)
                return false;
            for (size_t k = 0; k < j; ++k)
                if (skin->joints[k] == skin->joints[j])
                    return false;
        }
    }
    size_t channels = 0, bytes = 0;
    for (size_t i = 0; i < f->clip_count; ++i) {
        const af_animation_clip *clip = &f->clips[i];
        if (!memchr(clip->name, 0, sizeof(clip->name)) || !clip->name[0] || !isfinite(clip->duration) ||
            clip->duration < 0 || clip->duration > FLT_MAX || clip->channel_count > 1024 ||
            (clip->channel_count && !clip->channels) || clip->event_count > AF_ANIMATION_MAX_EVENTS ||
            channels > 4096 - clip->channel_count)
            return false;
        channels += clip->channel_count;
        for (size_t j = 0; j < clip->event_count; ++j) {
            const af_animation_event *event = &clip->events[j];
            if (!memchr(event->name, 0, sizeof(event->name)) || !event->name[0] || !isfinite(event->time) ||
                event->time < 0 || event->time > clip->duration || (j && event->time < clip->events[j - 1].time))
                return false;
        }
        for (size_t j = 0; j < clip->channel_count; ++j) {
            const af_animation_channel *c = &clip->channels[j];
            if (c->node >= f->node_count || c->path < AF_ANIM_TRANSLATION || c->path > AF_ANIM_WEIGHTS ||
                c->interpolation < AF_ANIM_LINEAR || c->interpolation > AF_ANIM_CUBIC || !c->times || !c->key_count ||
                c->key_count > 65536 || (c->interpolation == AF_ANIM_CUBIC && c->key_count < 2))
                return false;
            for (size_t k = 0; k < j; ++k)
                if (clip->channels[k].node == c->node && clip->channels[k].path == c->path)
                    return false;
            size_t stride = c->interpolation == AF_ANIM_CUBIC ? 3u : 1u;
            size_t components = c->path == AF_ANIM_WEIGHTS ? f->bind_pose[c->node].morph_count : 4u;
            size_t scalars = c->key_count * stride * components;
            size_t needed = c->key_count * sizeof(float) + scalars * sizeof(float);
            if (!components || needed > AF_ASSET_FILE_MAX_BYTES - bytes)
                return false;
            bytes += needed;
            if (c->path == AF_ANIM_WEIGHTS) {
                if (!c->morph_values || !finite_values(c->morph_values, scalars))
                    return false;
            } else {
                if (!c->values)
                    return false;
                for (size_t k = 0; k < c->key_count * stride; ++k) {
                    af_vec4 v = c->values[k];
                    float values[] = {v.x, v.y, v.z, v.w};
                    if (!finite_values(values, 4) ||
                        (c->path == AF_ANIM_ROTATION && (stride == 1 || k % 3 == 1) && !valid_rotation(v)))
                        return false;
                }
            }
            for (size_t k = 0; k < c->key_count; ++k)
                if (!isfinite(c->times[k]) || c->times[k] < 0 || c->times[k] > clip->duration ||
                    (k && c->times[k] <= c->times[k - 1]))
                    return false;
        }
    }
    return true;
}

/* Version 1 uses little-endian IEEE floats and fixed-width integers, without C struct padding. */
typedef struct animation_io {
    FILE *file;
    bool writing, ok;
    size_t bytes, allocated;
} animation_io;
static void block(animation_io *io, void *data, size_t bytes) {
    if (!io->ok || bytes > AF_ASSET_FILE_MAX_BYTES - io->bytes) {
        io->ok = false;
        return;
    }
    io->bytes += bytes;
    size_t done = io->writing ? fwrite(data, 1, bytes, io->file) : fread(data, 1, bytes, io->file);
    io->ok = done == bytes;
}
static void integer(animation_io *io, uint32_t *value) {
    unsigned char bytes[4];
    for (unsigned i = 0; i < 4; ++i)
        bytes[i] = (unsigned char)(*value >> (8 * i));
    block(io, bytes, sizeof(bytes));
    if (!io->writing && io->ok)
        *value = (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}
static void real(animation_io *io, float *value) {
    uint32_t bits;
    memcpy(&bits, value, sizeof(bits));
    integer(io, &bits);
    if (!io->writing)
        memcpy(value, &bits, sizeof(bits));
}
static void seconds(animation_io *io, double *value) {
    uint64_t bits;
    memcpy(&bits, value, sizeof(bits));
    uint32_t lo = (uint32_t)bits, hi = (uint32_t)(bits >> 32);
    integer(io, &lo);
    integer(io, &hi);
    if (!io->writing) {
        bits = (uint64_t)lo | (uint64_t)hi << 32;
        memcpy(value, &bits, sizeof(bits));
    }
}
static bool count_value(animation_io *io, size_t *count, uint32_t limit) {
    uint32_t value = (uint32_t)*count;
    integer(io, &value);
    if (!io->writing)
        *count = value;
    io->ok = io->ok && value <= limit;
    return io->ok;
}
static void *allocate(animation_io *io, size_t count, size_t size) {
    if (!io->ok || count > (AF_ASSET_FILE_MAX_BYTES - io->allocated) / size) {
        io->ok = false;
        return NULL;
    }
    io->allocated += count * size;
    void *data = calloc(count ? count : 1, size);
    io->ok = data != NULL;
    return data;
}
static void vector(animation_io *io, af_vec4 *v) {
    real(io, &v->x);
    real(io, &v->y);
    real(io, &v->z);
    real(io, &v->w);
}
static void pose_data(animation_io *io, af_animation_pose *p) {
    real(io, &p->translation.x);
    real(io, &p->translation.y);
    real(io, &p->translation.z);
    vector(io, &p->rotation);
    real(io, &p->scale.x);
    real(io, &p->scale.y);
    real(io, &p->scale.z);
    integer(io, &p->morph_count);
    if (p->morph_count > AF_ANIMATION_MAX_MORPHS) {
        io->ok = false;
        return;
    }
    for (size_t i = 0; io->ok && i < p->morph_count; ++i)
        real(io, &p->morph_weights[i]);
}
static void channel_data(animation_io *io, af_animation_file *f, af_animation_channel *c) {
    integer(io, &c->node);
    uint32_t path = (uint32_t)c->path, interpolation = (uint32_t)c->interpolation;
    integer(io, &path);
    integer(io, &interpolation);
    if (!io->writing) {
        c->path = (af_animation_path)path;
        c->interpolation = (af_animation_interpolation)interpolation;
    }
    if (c->node >= f->node_count || path > AF_ANIM_WEIGHTS || interpolation > AF_ANIM_CUBIC ||
        !count_value(io, &c->key_count, 65536)) {
        io->ok = false;
        return;
    }
    size_t values = c->key_count * (interpolation == AF_ANIM_CUBIC ? 3u : 1u);
    bool weights = path == AF_ANIM_WEIGHTS;
    if (weights)
        values *= f->bind_pose[c->node].morph_count;
    if (!io->writing) {
        c->times = allocate(io, c->key_count, sizeof(float));
        if (weights)
            c->morph_values = allocate(io, values, sizeof(float));
        else
            c->values = allocate(io, values, sizeof(af_vec4));
    }
    for (size_t i = 0; io->ok && i < c->key_count; ++i)
        real(io, &c->times[i]);
    for (size_t i = 0; io->ok && i < values; ++i)
        if (weights)
            real(io, &c->morph_values[i]);
        else
            vector(io, &c->values[i]);
}
static void native_data(animation_io *io, af_animation_file *f) {
    if (!count_value(io, &f->node_count, AF_ANIMATION_MAX_NODES) || !count_value(io, &f->skin_count, 128) ||
        !count_value(io, &f->clip_count, AF_ANIMATION_MAX_CLIPS))
        return;
    if (!io->writing) {
        f->bind_pose = allocate(io, f->node_count, sizeof(*f->bind_pose));
        f->parents = allocate(io, f->node_count, sizeof(*f->parents));
        f->skins = allocate(io, f->skin_count, sizeof(*f->skins));
        f->clips = allocate(io, f->clip_count, sizeof(*f->clips));
    }
    for (size_t i = 0; io->ok && i < f->node_count; ++i) {
        pose_data(io, &f->bind_pose[i]);
        uint32_t parent = (uint32_t)f->parents[i];
        integer(io, &parent);
        if (!io->writing)
            f->parents[i] = parent == UINT32_MAX ? -1 : parent < f->node_count ? (int32_t)parent : -2;
    }
    for (size_t i = 0; io->ok && i < f->skin_count; ++i) {
        af_animation_skin *skin = &f->skins[i];
        if (!count_value(io, &skin->joint_count, AF_ANIMATION_MAX_JOINTS))
            break;
        for (size_t j = 0; io->ok && j < skin->joint_count; ++j) {
            integer(io, &skin->joints[j]);
            for (unsigned k = 0; k < 16; ++k)
                real(io, &skin->inverse_bind[j].elements[k]);
        }
    }
    for (size_t i = 0; io->ok && i < f->clip_count; ++i) {
        af_animation_clip *clip = &f->clips[i];
        block(io, clip->name, sizeof(clip->name));
        seconds(io, &clip->duration);
        if (!count_value(io, &clip->channel_count, 1024) ||
            !count_value(io, &clip->event_count, AF_ANIMATION_MAX_EVENTS))
            break;
        if (!io->writing)
            clip->channels = allocate(io, clip->channel_count, sizeof(*clip->channels));
        for (size_t j = 0; io->ok && j < clip->event_count; ++j) {
            block(io, clip->events[j].name, sizeof(clip->events[j].name));
            seconds(io, &clip->events[j].time);
        }
        for (size_t j = 0; io->ok && j < clip->channel_count; ++j)
            channel_data(io, f, &clip->channels[j]);
    }
}
bool af_animation_file_load(const char *path, af_animation_file *out, af_asset_error *error) {
    if (error)
        *error = (af_asset_error){0};
    if (!out)
        return failure(error, "Animation output is required.");
    *out = (af_animation_file){0};
    FILE *file = path ? fopen(path, "rb") : NULL;
    if (!file)
        return failure(error, "Cannot open animation asset.");
    unsigned char header[sizeof(magic)] = {0};
    bool native = fread(header, 1, sizeof(header), file) == sizeof(header) && !memcmp(header, magic, sizeof(magic));
    if (!native) {
        fclose(file);
        return af_animation_gltf_load(path, out, error);
    }
    animation_io io = {.file = file, .ok = true, .bytes = sizeof(magic)};
    native_data(&io, out);
    bool ok = io.ok && fgetc(file) == EOF && !ferror(file) && af_animation_file_validate(out);
    if (fclose(file))
        ok = false;
    if (!ok) {
        af_animation_file_free(out);
        return failure(error, "Invalid native animation. Check the rig, key times, values and asset limits.");
    }
    return true;
}
bool af_animation_file_save(const char *path, const af_animation_file *file, af_asset_error *error) {
    if (error)
        *error = (af_asset_error){0};
    if (!path || !path[0] || strlen(path) >= AF_ASSET_PATH_CAPACITY || !af_animation_file_validate(file))
        return failure(error, "A valid animation and output path are required.");
    char temporary[AF_ASSET_PATH_CAPACITY + 80];
    static _Atomic uint64_t sequence;
    FILE *output = NULL;
    for (unsigned attempt = 0; attempt < 64; ++attempt) {
        snprintf(temporary, sizeof(temporary), "%s.afterlight-%" PRIx64 "-%" PRIx64 ".tmp", path, (uint64_t)time(NULL),
                 atomic_fetch_add(&sequence, 1));
        output = fopen(temporary, "wbx");
        if (output || errno != EEXIST)
            break;
    }
    if (!output)
        return failure(error, "Cannot create animation asset in this folder.");
    unsigned char header[sizeof(magic)];
    memcpy(header, magic, sizeof(header));
    animation_io io = {.file = output, .writing = true, .ok = true};
    block(&io, header, sizeof(header));
    af_animation_file borrowed = *file;
    native_data(&io, &borrowed);
    bool ok = io.ok && fflush(output) == 0;
    if (fclose(output))
        ok = false;
    if (ok) {
#ifdef _WIN32
        ok = MoveFileExA(temporary, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
        ok = rename(temporary, path) == 0;
#endif
    }
    if (!ok) {
        remove(temporary);
        return failure(error, "Writing complete animation asset failed.");
    }
    return true;
}
