/* SPDX-License-Identifier: MPL-2.0 */
#include "animation_editor.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t file_bytes(const af_animation_file *f) {
    size_t bytes =
        f->node_count * (sizeof(af_animation_pose) + sizeof(int32_t)) + f->skin_count * sizeof(af_animation_skin);
    for (size_t i = 0; i < f->clip_count; ++i) {
        const af_animation_clip *clip = &f->clips[i];
        bytes += sizeof(*clip) + clip->channel_count * sizeof(af_animation_channel);
        for (size_t j = 0; j < clip->channel_count; ++j) {
            const af_animation_channel *c = &clip->channels[j];
            size_t value =
                c->path == AF_ANIM_WEIGHTS ? f->bind_pose[c->node].morph_count * sizeof(float) : sizeof(af_vec4);
            bytes += c->key_count * (sizeof(float) + value * (c->interpolation == AF_ANIM_CUBIC ? 3u : 1u));
        }
    }
    return bytes;
}
static void clear_history(ed_animation_snapshot *history, size_t *count) {
    for (size_t i = 0; i < *count; ++i)
        af_animation_file_free(&history[i].file);
    *count = 0;
}
static void push_history(ed_animation_snapshot *history, size_t *count, af_animation_file file, uint64_t revision) {
    size_t bytes = file_bytes(&file), retained = bytes;
    for (size_t i = 0; i < *count; ++i)
        retained += history[i].bytes;
    while (*count && (*count >= ED_ANIMATION_HISTORY || retained > 16u * 1024u * 1024u)) {
        retained -= history[0].bytes;
        af_animation_file_free(&history[0].file);
        memmove(history, history + 1, --*count * sizeof(*history));
    }
    if (bytes > 16u * 1024u * 1024u) {
        af_animation_file_free(&file);
        return;
    }
    history[(*count)++] = (ed_animation_snapshot){file, revision, bytes};
}
void ed_animation_editor_destroy(ed_animation_editor *e) {
    af_animation_file_free(&e->file);
    clear_history(e->undo, &e->undo_count);
    clear_history(e->redo, &e->redo_count);
    free(e->poses);
    *e = (ed_animation_editor){0};
}
bool ed_animation_editor_dirty(const ed_animation_editor *e) {
    return e->file.node_count && (e->pose_dirty || e->revision != e->saved_revision);
}
void ed_animation_editor_select(ed_animation_editor *e, uint32_t node) {
    if (node >= e->file.node_count)
        return;
    e->node = node;
    af_vec4 q = e->poses[node].rotation;
    float pitch = fmaxf(-1, fminf(1, 2 * (q.w * q.y - q.z * q.x)));
    e->rotation_degrees = (af_vec3){atan2f(2 * (q.w * q.x + q.y * q.z), 1 - 2 * (q.x * q.x + q.y * q.y)) * 180 / AF_PI,
                                    asinf(pitch) * 180 / AF_PI,
                                    atan2f(2 * (q.w * q.z + q.x * q.y), 1 - 2 * (q.y * q.y + q.z * q.z)) * 180 / AF_PI};
}
void ed_animation_editor_sample(ed_animation_editor *e) {
    if (!e->file.node_count)
        return;
    if (e->clip >= e->file.clip_count || !af_animation_sample(&e->file, e->clip, e->time, e->poses, e->file.node_count))
        memcpy(e->poses, e->file.bind_pose, e->file.node_count * sizeof(*e->poses));
    e->pose_dirty = false;
    ed_animation_editor_select(e, e->node);
}
bool ed_animation_editor_open(ed_animation_editor *e, af_object_id root, const char *path, size_t clip,
                              af_asset_error *error) {
    ed_animation_editor next = {
        .root = root, .open = true, .loop = true, .revision = 1, .serial = 1, .saved_revision = 1};
    if (!af_animation_file_load(path, &next.file, error))
        return false;
    next.poses = calloc(next.file.node_count, sizeof(*next.poses));
    if (!next.poses || (!next.file.clip_count && !af_animation_clip_add(&next.file, "Clip 1", 1))) {
        ed_animation_editor_destroy(&next);
        return false;
    }
    next.clip = clip < next.file.clip_count ? clip : 0;
    if (next.file.skin_count)
        next.node = next.file.skins[0].joints[0];
    const char *extension = strrchr(path, '.');
    if (extension && !strcmp(extension, ".afanim"))
        snprintf(next.path, sizeof(next.path), "%s", path);
    ed_animation_editor_sample(&next);
    ed_animation_editor_destroy(e);
    *e = next;
    return true;
}
static void commit(ed_animation_editor *e, af_animation_file *next) {
    push_history(e->undo, &e->undo_count, e->file, e->revision);
    clear_history(e->redo, &e->redo_count);
    e->file = *next;
    e->revision = ++e->serial;
    e->playing = false;
    ed_animation_editor_sample(e);
}
bool ed_animation_editor_new_clip(ed_animation_editor *e) {
    af_animation_file next;
    if (!af_animation_file_copy(&e->file, &next, NULL))
        return false;
    char name[64];
    snprintf(name, sizeof(name), "Clip %zu", next.clip_count + 1);
    if (!af_animation_clip_add(&next, name, 1)) {
        af_animation_file_free(&next);
        return false;
    }
    e->clip = next.clip_count - 1;
    e->time = 0;
    commit(e, &next);
    return true;
}
bool ed_animation_editor_record(ed_animation_editor *e) {
    af_animation_file next;
    if (e->clip >= e->file.clip_count || !af_animation_file_copy(&e->file, &next, NULL))
        return false;
    af_animation_pose *sampled = calloc(e->file.node_count, sizeof(*sampled));
    if (!sampled || !af_animation_sample(&e->file, e->clip, e->time, sampled, e->file.node_count)) {
        free(sampled);
        af_animation_file_free(&next);
        return false;
    }
    /* Capture edited bones together so changing selection keeps the whole pose. */
    for (size_t node = 0; node < e->file.node_count; ++node) {
        const af_animation_pose *pose = &e->poses[node];
        if (node != e->node && !memcmp(&sampled[node], pose, sizeof(*pose)))
            continue;
        af_vec4 values[] = {{pose->translation.x, pose->translation.y, pose->translation.z, 0},
                            pose->rotation,
                            {pose->scale.x, pose->scale.y, pose->scale.z, 0}};
        for (unsigned path = 0; path < 3; ++path)
            if (!af_animation_key_set(&next, e->clip, (uint32_t)node, (af_animation_path)path, e->time, values[path],
                                      (af_animation_interpolation)e->interpolation)) {
                free(sampled);
                af_animation_file_free(&next);
                return false;
            }
    }
    free(sampled);
    commit(e, &next);
    return true;
}
bool ed_animation_editor_delete_key(ed_animation_editor *e) {
    af_animation_file next;
    if (!af_animation_file_copy(&e->file, &next, NULL))
        return false;
    bool changed = false;
    for (unsigned path = 0; path < 3; ++path)
        changed |= af_animation_key_remove(&next, e->clip, e->node, (af_animation_path)path, e->time);
    if (!changed) {
        af_animation_file_free(&next);
        return false;
    }
    commit(e, &next);
    return true;
}
bool ed_animation_editor_clip_settings(ed_animation_editor *e, const char *name, double duration) {
    if (e->clip >= e->file.clip_count || !name || !name[0] || strlen(name) >= 64 || !isfinite(duration) ||
        duration <= 0)
        return false;
    af_animation_clip *clip = &e->file.clips[e->clip];
    if (!strcmp(clip->name, name) && clip->duration == duration)
        return true;
    for (size_t i = 0; i < clip->channel_count; ++i) {
        const af_animation_channel *c = &clip->channels[i];
        if (c->times[c->key_count - 1] > duration)
            return false;
    }
    for (size_t i = 0; i < clip->event_count; ++i)
        if (clip->events[i].time > duration)
            return false;
    af_animation_file next;
    if (!af_animation_file_copy(&e->file, &next, NULL))
        return false;
    snprintf(next.clips[e->clip].name, sizeof(next.clips[e->clip].name), "%s", name);
    next.clips[e->clip].duration = duration;
    e->time = fminf(e->time, (float)duration);
    commit(e, &next);
    return true;
}
bool ed_animation_editor_undo(ed_animation_editor *e, bool redo) {
    ed_animation_snapshot *from = redo ? e->redo : e->undo, *to = redo ? e->undo : e->redo;
    size_t *from_count = redo ? &e->redo_count : &e->undo_count, *to_count = redo ? &e->undo_count : &e->redo_count;
    if (!*from_count)
        return false;
    ed_animation_snapshot snapshot = from[--*from_count];
    push_history(to, to_count, e->file, e->revision);
    e->file = snapshot.file;
    e->revision = snapshot.revision;
    if (e->clip >= e->file.clip_count)
        e->clip = e->file.clip_count - 1;
    e->time = fminf(e->time, (float)e->file.clips[e->clip].duration);
    e->playing = false;
    ed_animation_editor_sample(e);
    return true;
}
void ed_animation_editor_saved(ed_animation_editor *e, const char *path) {
    if (path != e->path)
        snprintf(e->path, sizeof(e->path), "%s", path);
    e->saved_revision = e->revision;
}
