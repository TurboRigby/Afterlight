/* SPDX-License-Identifier: MPL-2.0 */
#include <afterlight/animation.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *copy_data(const void *data, size_t count, size_t size) {
    if (!count)
        return NULL;
    void *out = malloc(count * size);
    if (out)
        memcpy(out, data, count * size);
    return out;
}
bool af_animation_file_copy(const af_animation_file *source, af_animation_file *out, af_asset_error *error) {
    if (error)
        *error = (af_asset_error){0};
    if (!out || out == source)
        return false;
    *out = (af_animation_file){0};
    if (!af_animation_file_validate(source))
        return false;
    af_animation_file f = {
        .node_count = source->node_count, .skin_count = source->skin_count, .clip_count = source->clip_count};
    f.bind_pose = copy_data(source->bind_pose, f.node_count, sizeof(*f.bind_pose));
    f.parents = copy_data(source->parents, f.node_count, sizeof(*f.parents));
    f.skins = copy_data(source->skins, f.skin_count, sizeof(*f.skins));
    f.clips = calloc(f.clip_count ? f.clip_count : 1, sizeof(*f.clips));
    bool ok = f.bind_pose && f.parents && (!f.skin_count || f.skins) && f.clips;
    for (size_t i = 0; ok && i < f.clip_count; ++i) {
        const af_animation_clip *from = &source->clips[i];
        af_animation_clip *to = &f.clips[i];
        *to = *from;
        to->channels = calloc(from->channel_count ? from->channel_count : 1, sizeof(*to->channels));
        ok = to->channels != NULL;
        for (size_t j = 0; ok && j < from->channel_count; ++j) {
            const af_animation_channel *c = &from->channels[j];
            af_animation_channel *d = &to->channels[j];
            *d = *c;
            size_t stride = c->interpolation == AF_ANIM_CUBIC ? 3u : 1u;
            bool morph = c->path == AF_ANIM_WEIGHTS;
            d->times = copy_data(c->times, c->key_count, sizeof(float));
            d->values = morph ? NULL : copy_data(c->values, c->key_count * stride, sizeof(af_vec4));
            d->morph_values = morph ? copy_data(c->morph_values,
                                                c->key_count * stride * f.bind_pose[c->node].morph_count, sizeof(float))
                                    : NULL;
            ok = d->times && (morph ? d->morph_values != NULL : d->values != NULL);
        }
    }
    if (!ok) {
        af_animation_file_free(&f);
        if (error)
            snprintf(error->message, sizeof(error->message), "Not enough memory to copy animation.");
        return false;
    }
    *out = f;
    return true;
}
bool af_animation_clip_add(af_animation_file *f, const char *name, double duration) {
    if (!f || !name || !name[0] || strlen(name) >= sizeof(f->clips[0].name) || !isfinite(duration) || duration <= 0 ||
        f->clip_count >= AF_ANIMATION_MAX_CLIPS)
        return false;
    af_animation_clip *clips = realloc(f->clips, (f->clip_count + 1) * sizeof(*clips));
    if (!clips)
        return false;
    f->clips = clips;
    af_animation_clip *clip = &clips[f->clip_count++];
    *clip = (af_animation_clip){.duration = duration};
    snprintf(clip->name, sizeof(clip->name), "%s", name);
    return true;
}
static af_vec4 bind_value(const af_animation_pose *p, af_animation_path path) {
    if (path == AF_ANIM_ROTATION)
        return p->rotation;
    af_vec3 v = path == AF_ANIM_TRANSLATION ? p->translation : p->scale;
    return (af_vec4){v.x, v.y, v.z, 0};
}
bool af_animation_key_set(af_animation_file *f, size_t index, uint32_t node, af_animation_path path, float time,
                          af_vec4 value, af_animation_interpolation interpolation) {
    float values[] = {value.x, value.y, value.z, value.w};
    if (!f || index >= f->clip_count || node >= f->node_count || path < AF_ANIM_TRANSLATION || path > AF_ANIM_SCALE ||
        interpolation < AF_ANIM_LINEAR || interpolation > AF_ANIM_STEP || !isfinite(time) || time < 0 ||
        time > f->clips[index].duration)
        return false;
    for (unsigned i = 0; i < 4; ++i)
        if (!isfinite(values[i]))
            return false;
    if (path == AF_ANIM_ROTATION)
        value = af_quaternion_normalize(value);
    af_animation_clip *clip = &f->clips[index];
    size_t at = 0;
    while (at < clip->channel_count && (clip->channels[at].node != node || clip->channels[at].path != path))
        ++at;
    af_animation_channel empty = {0};
    const af_animation_channel *old = at < clip->channel_count ? &clip->channels[at] : &empty;
    size_t position = 0;
    while (position < old->key_count && old->times[position] < time)
        ++position;
    bool replace = position < old->key_count && old->times[position] == time;
    bool bind = !old->key_count && time > 0;
    size_t count = old->key_count + !replace + bind;
    if (count > 65536 || (at == clip->channel_count && clip->channel_count >= 1024))
        return false;
    size_t total_channels = at == clip->channel_count, total_bytes = count * (sizeof(float) + sizeof(af_vec4));
    for (size_t i = 0; i < f->clip_count; ++i) {
        total_channels += f->clips[i].channel_count;
        for (size_t j = 0; j < f->clips[i].channel_count; ++j) {
            const af_animation_channel *channel = &f->clips[i].channels[j];
            if (channel == old)
                continue;
            size_t components = channel->path == AF_ANIM_WEIGHTS ? f->bind_pose[channel->node].morph_count : 4u;
            size_t stride = channel->interpolation == AF_ANIM_CUBIC ? 3u : 1u;
            total_bytes += channel->key_count * (sizeof(float) + stride * components * sizeof(float));
        }
    }
    if (total_channels > 4096 || total_bytes > AF_ASSET_FILE_MAX_BYTES)
        return false;
    af_animation_channel next = {.node = node, .path = path, .interpolation = interpolation, .key_count = count};
    next.times = calloc(count, sizeof(float));
    next.values = calloc(count, sizeof(af_vec4));
    if (!next.times || !next.values) {
        free(next.times);
        free(next.values);
        return false;
    }
    size_t stride = old->interpolation == AF_ANIM_CUBIC ? 3u : 1u, offset = stride == 3 ? 1u : 0u;
    for (size_t i = 0; i < old->key_count; ++i) {
        size_t to = i + (i >= position && !replace);
        next.times[to] = old->times[i];
        next.values[to] = old->values[i * stride + offset];
    }
    if (bind) {
        next.values[0] = bind_value(&f->bind_pose[node], path);
        position = 1;
    }
    next.times[position] = time;
    next.values[position] = value;
    if (at == clip->channel_count) {
        af_animation_channel *channels = realloc(clip->channels, (at + 1) * sizeof(*channels));
        if (!channels) {
            free(next.times);
            free(next.values);
            return false;
        }
        clip->channels = channels;
        channels[at] = (af_animation_channel){0};
        ++clip->channel_count;
    }
    af_animation_channel *target = &clip->channels[at];
    free(target->times);
    free(target->values);
    free(target->morph_values);
    *target = next;
    return true;
}
bool af_animation_key_remove(af_animation_file *f, size_t index, uint32_t node, af_animation_path path, float time) {
    if (!f || index >= f->clip_count || path < AF_ANIM_TRANSLATION || path > AF_ANIM_SCALE || !isfinite(time))
        return false;
    af_animation_clip *clip = &f->clips[index];
    for (size_t i = 0; i < clip->channel_count; ++i) {
        af_animation_channel *c = &clip->channels[i];
        if (c->node != node || c->path != path)
            continue;
        size_t key = 0;
        while (key < c->key_count && c->times[key] != time)
            ++key;
        if (key == c->key_count)
            return false;
        --c->key_count;
        memmove(c->times + key, c->times + key + 1, (c->key_count - key) * sizeof(float));
        size_t stride = c->interpolation == AF_ANIM_CUBIC ? 3u : 1u;
        memmove(c->values + key * stride, c->values + (key + 1) * stride,
                (c->key_count - key) * stride * sizeof(af_vec4));
        if (stride == 3 && c->key_count == 1) {
            c->values[0] = c->values[1];
            c->interpolation = AF_ANIM_LINEAR;
        }
        if (!c->key_count) {
            free(c->times);
            free(c->values);
            free(c->morph_values);
            memmove(c, c + 1, (clip->channel_count - i - 1) * sizeof(*c));
            --clip->channel_count;
        }
        return true;
    }
    return false;
}
