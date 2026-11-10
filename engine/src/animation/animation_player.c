/* SPDX-License-Identifier: MPL-2.0 */
#include <afterlight/animation.h>
#include "morph.h"
#include <math.h>
#include <stdlib.h>

af_animation_player af_animation_player_default(void) {
    return (af_animation_player){.speed = 1, .loop = true};
}
void af_animation_player_destroy(af_animation_player *p) {
    if (p) {
        free(p->blend_pose);
        free(p->frozen_pose);
        *p = af_animation_player_default();
    }
}
static void clear_fade(af_animation_player *p) {
    p->fading = false;
    p->fade_elapsed = p->fade_duration = 0;
    free(p->frozen_pose);
    p->frozen_pose = NULL;
    p->frozen_count = 0;
}
void af_animation_player_pause(af_animation_player *p, bool paused) {
    if (p)
        p->playing = !paused;
}
void af_animation_player_stop(af_animation_player *p) {
    if (p) {
        p->playing = false;
        p->finished = false;
        p->time = 0;
        clear_fade(p);
    }
}
static bool valid(const af_animation_player *p, const af_animation_file *file) {
    return p && file && p->clip < file->clip_count && isfinite(p->speed) && fabsf(p->speed) <= 100;
}
bool af_animation_player_play(af_animation_player *p, const af_animation_file *file, size_t clip, float blend_seconds) {
    if (!p || !file || clip >= file->clip_count || !isfinite(blend_seconds) || blend_seconds < 0 ||
        blend_seconds > 10 || !isfinite(p->speed) || fabsf(p->speed) > 100)
        return false;
    af_animation_pose *frozen = NULL;
    if (blend_seconds > 0 && p->clip < file->clip_count) {
        if (file->node_count > p->pose_capacity) {
            af_animation_pose *buffer = realloc(p->blend_pose, file->node_count * sizeof(*buffer));
            if (!buffer)
                return false;
            p->blend_pose = buffer;
            p->pose_capacity = file->node_count;
        }
        if (p->fading) {
            frozen = calloc(file->node_count, sizeof(*frozen));
            if (!frozen)
                return false;
            if (!af_animation_player_sample(p, file, frozen, file->node_count)) {
                free(frozen);
                return false;
            }
        }
        p->previous_clip = p->clip;
        p->previous_time = p->time;
    } else
        blend_seconds = 0;
    clear_fade(p);
    p->frozen_pose = frozen;
    p->frozen_count = frozen ? file->node_count : 0;
    p->fading = blend_seconds > 0;
    p->fade_duration = blend_seconds;
    p->clip = clip;
    p->time = p->speed < 0 ? file->clips[clip].duration : 0;
    p->playing = true;
    p->finished = false;
    return true;
}
bool af_animation_player_seek(af_animation_player *p, const af_animation_file *file, double seconds) {
    if (!valid(p, file) || !isfinite(seconds))
        return false;
    p->time = fmax(0, fmin(file->clips[p->clip].duration, seconds));
    p->finished = false;
    clear_fade(p);
    return true;
}
static double advance(double time, double delta, double duration, bool loop) {
    if (duration <= 0)
        return 0;
    time += delta;
    if (loop) {
        time = fmod(time, duration);
        if (time < 0)
            time += duration;
        return time;
    }
    return fmax(0, fmin(duration, time));
}
bool af_animation_player_update(af_animation_player *p, const af_animation_file *file, double delta) {
    if (!valid(p, file) || !isfinite(delta) || delta < 0 || delta > 86400)
        return false;
    if (!p->playing)
        return true;
    double duration = file->clips[p->clip].duration, step = delta * p->speed;
    p->time = advance(p->time, step, duration, p->loop);
    if (p->fading) {
        if (!p->frozen_pose && p->previous_clip < file->clip_count)
            p->previous_time = advance(p->previous_time, step, file->clips[p->previous_clip].duration, p->loop);
        p->fade_elapsed += (float)delta;
        if (p->fade_elapsed >= p->fade_duration)
            clear_fade(p);
    }
    if (!p->loop && p->speed != 0 && delta > 0 &&
        (duration <= 0 || (p->speed > 0 ? p->time >= duration : p->time <= 0))) {
        p->finished = true;
        p->playing = p->fading;
    }
    return true;
}
bool af_animation_player_sample(af_animation_player *p, const af_animation_file *file, af_animation_pose *out,
                                size_t capacity) {
    if (!valid(p, file) || !af_animation_sample(file, p->clip, p->time, out, capacity))
        return false;
    if (p->fading) {
        if (file->node_count > p->pose_capacity || !p->blend_pose || p->fade_duration <= 0)
            return false;
        const af_animation_pose *from = p->frozen_pose;
        if (from && file->node_count > p->frozen_count)
            return false;
        if (!from) {
            if (!af_animation_sample(file, p->previous_clip, p->previous_time, p->blend_pose, p->pose_capacity))
                return false;
            from = p->blend_pose;
        }
        float t = fminf(1, p->fade_elapsed / p->fade_duration);
        for (size_t i = 0; i < file->node_count; ++i)
            af_animation_blend_pose(&out[i], &from[i], t);
    }
    return true;
}
