/* SPDX-License-Identifier: MPL-2.0 */
#include <afterlight/blueprint.h>
#include <math.h>
#include <string.h>

static bool key_time(float time, float previous, float duration) {
    return isfinite(time) && time >= 0 && time > previous && time <= duration;
}

static bool finite_vec(af_vec3 v) {
    return isfinite(v.x) && isfinite(v.y) && isfinite(v.z);
}

bool af_timeline_valid(const af_timeline *timeline) {
    if (!timeline || !isfinite(timeline->duration) || timeline->duration < .01f ||
        timeline->duration > AF_TIMELINE_MAX_SECONDS || timeline->pose_count > AF_TIMELINE_MAX_KEYS ||
        timeline->light_count > AF_TIMELINE_MAX_KEYS || timeline->sound_count > AF_TIMELINE_MAX_KEYS)
        return false;
    float previous = -1;
    for (size_t i = 0; i < timeline->pose_count; ++i) {
        const af_timeline_pose *key = &timeline->poses[i];
        if (!key_time(key->time, previous, timeline->duration) || !finite_vec(key->transform.position) ||
            !finite_vec(key->transform.rotation) || !finite_vec(key->transform.scale))
            return false;
        previous = key->time;
    }
    previous = -1;
    for (size_t i = 0; i < timeline->light_count; ++i) {
        const af_timeline_light *key = &timeline->lights[i];
        if (!key_time(key->time, previous, timeline->duration) || !isfinite(key->intensity) ||
            key->intensity < 0 || key->intensity > 100)
            return false;
        previous = key->time;
    }
    previous = -1;
    for (size_t i = 0; i < timeline->sound_count; ++i) {
        const af_timeline_sound *key = &timeline->sounds[i];
        if (!key_time(key->time, previous, timeline->duration) || !isfinite(key->gain) ||
            key->gain < 0 || key->gain > 1 || !key->clip[0] || !memchr(key->clip, 0, sizeof(key->clip)))
            return false;
        previous = key->time;
    }
    return true;
}

static float interpolate(float a, float b, float t) {
    return a * (1.f - t) + b * t;
}

static af_vec3 interpolate_vec(af_vec3 a, af_vec3 b, float t) {
    return (af_vec3){interpolate(a.x, b.x, t), interpolate(a.y, b.y, t), interpolate(a.z, b.z, t)};
}

static float blend_at(float time, float start, float end) {
    return end == start ? 0 : fminf(1.f, fmaxf(0.f, (time - start) / (end - start)));
}

bool af_timeline_sample(const af_timeline *timeline, af_scene *scene, af_object_id target, float time) {
    if (!scene || !isfinite(time) || !af_timeline_valid(timeline))
        return false;
    if (timeline->pose_count) {
        size_t right = 0;
        while (right + 1 < timeline->pose_count && timeline->poses[right].time < time)
            ++right;
        const af_timeline_pose *a = &timeline->poses[right ? right - 1 : 0], *b = &timeline->poses[right];
        float t = blend_at(time, a->time, b->time);
        af_transform pose = {
            .position = interpolate_vec(a->transform.position, b->transform.position, t),
            .rotation = interpolate_vec(a->transform.rotation, b->transform.rotation, t),
            .scale = interpolate_vec(a->transform.scale, b->transform.scale, t),
        };
        if (!af_scene_set_object_transform(scene, target, pose))
            return false;
    }
    if (timeline->light_count) {
        size_t right = 0;
        while (right + 1 < timeline->light_count && timeline->lights[right].time < time)
            ++right;
        const af_timeline_light *a = &timeline->lights[right ? right - 1 : 0], *b = &timeline->lights[right];
        af_directional_light light = af_scene_get_light(scene);
        light.intensity = interpolate(a->intensity, b->intensity, blend_at(time, a->time, b->time));
        return af_scene_set_light(scene, light);
    }
    return true;
}
