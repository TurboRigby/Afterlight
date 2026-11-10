/* SPDX-License-Identifier: MPL-2.0 */
#include "scene_internal.h"
#include "story_internal.h"
#include <math.h>
#include <string.h>

static bool identifier(const char *name, size_t capacity) {
    if (!name || !name[0])
        return false;
    for (size_t i = 0; i < capacity; ++i) {
        char ch = name[i];
        if (!ch)
            return true;
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_' ||
              ch == '-'))
            return false;
    }
    return false;
}

static size_t flag_index(const af_story_flags *flags, const char *name) {
    size_t i = 0;
    while (i < flags->count && strcmp(flags->names[i], name))
        ++i;
    return i;
}

bool af_scene_story_flag(const af_scene *s, const char *name) {
    return s && name && flag_index(&s->story_flags, name) < s->story_flags.count;
}

bool af_scene_set_story_flag(af_scene *s, const char *name, bool value) {
    if (!s || !identifier(name, AF_STORY_FLAG_CAPACITY))
        return false;
    af_story_flags *f = &s->story_flags;
    size_t i = flag_index(f, name);
    if (!value && i < f->count) {
        if (i < --f->count)
            memmove(f->names[i], f->names[i + 1], (f->count - i) * sizeof(f->names[0]));
        memset(f->names[f->count], 0, sizeof(f->names[0]));
    } else if (value && i == f->count) {
        if (i == AF_STORY_MAX_FLAGS)
            return false;
        memcpy(f->names[f->count++], name, strlen(name) + 1);
    }
    return true;
}

const af_story_flags *af_scene_story_flags(const af_scene *s) {
    return s ? &s->story_flags : NULL;
}

bool af_scene_restore_story_flags(af_scene *s, const af_story_flags *source) {
    if (!s || !source || source->count > AF_STORY_MAX_FLAGS)
        return false;
    af_story_flags clean = {0};
    for (size_t i = 0; i < source->count; ++i) {
        if (!identifier(source->names[i], AF_STORY_FLAG_CAPACITY) ||
            flag_index(&clean, source->names[i]) != clean.count)
            return false;
        memcpy(clean.names[clean.count++], source->names[i], strlen(source->names[i]) + 1);
    }
    s->story_flags = clean;
    return true;
}

static bool interactable(const af_scene *s, const af_blueprint *bp) {
    if (!bp->enabled || !bp->bound_object || !bp->interact_prompt[0] || !isfinite(bp->interact_range) ||
        bp->interact_range < .25f || bp->interact_range > 30.f || !af_scene_object_visible(s, bp->bound_object))
        return false;
    for (size_t i = 0; i < bp->node_count; ++i)
        if (bp->nodes[i].type == AF_BP_NODE_EVENT_INTERACT)
            return true;
    return false;
}

af_interaction_focus af_scene_interaction_focus(const af_scene *s, const af_camera *camera, af_object_id viewer) {
    af_interaction_focus best = {0};
    if (!s || !af_camera_is_valid(camera) || s->story_dialogue.text[0])
        return best;
    af_vec3 forward = af_camera_forward(camera);
    float best_score = INFINITY;
    for (size_t i = 0; i < s->blueprint_count; ++i) {
        const af_blueprint *bp = s->blueprints[i];
        af_mat4 world;
        if (!interactable(s, bp) || bp->bound_object == viewer || !af_scene_world_matrix(s, bp->bound_object, &world))
            continue;
        af_vec3 point = {world.elements[12], world.elements[13], world.elements[14]};
        af_vec3 delta = af_vec3_sub(point, camera->position);
        float distance = af_vec3_length(delta), along = af_vec3_dot(forward, delta);
        if (distance < .001f || distance > bp->interact_range || along < distance * .92f)
            continue;
        float score = distance + 12.f * (1.f - along / distance);
        if (score >= best_score)
            continue;
        af_collision_hit hit;
        af_query_filter filter = {.ignore = viewer, .ignore_also = bp->bound_object, .mask = UINT32_MAX};
        if (af_scene_raycast(s, camera->position, delta, distance - .001f, filter, &hit))
            continue;
        best = (af_interaction_focus){bp->bound_object, bp->interact_prompt};
        best_score = score;
    }
    return best;
}

bool af_scene_interact(af_scene *s, void *engine, af_object_id object) {
    if (!s || !object || s->story_dialogue.text[0])
        return false;
    bool used = false;
    for (size_t i = 0; i < s->blueprint_count; ++i) {
        af_blueprint *bp = s->blueprints[i];
        if (bp->bound_object != object || !interactable(s, bp))
            continue;
        for (size_t j = 0; j < bp->node_count && !s->story_dialogue.text[0]; ++j)
            if (bp->nodes[j].type == AF_BP_NODE_EVENT_INTERACT)
                used |= af_blueprint_dispatch_story(bp, s, engine, bp->nodes[j].id, "Exec");
        if (s->story_dialogue.text[0])
            break;
    }
    return used;
}

bool af_story_open_dialogue(af_scene *s, const af_blueprint *bp, uint32_t node, const af_story_dialogue *d) {
    if (!s || !d || s->story_dialogue.text[0] || !d->text[0] || (!!d->choices[0][0] != !!d->choices[1][0]))
        return false;
    for (size_t i = 0; i < s->blueprint_count; ++i)
        if (s->blueprints[i] == bp) {
            s->story_dialogue = *d;
            s->story_graph = i;
            s->story_node = node;
            return true;
        }
    return false;
}

const af_story_dialogue *af_scene_story_dialogue(const af_scene *s) {
    return s && s->story_dialogue.text[0] ? &s->story_dialogue : NULL;
}

bool af_scene_answer_dialogue(af_scene *s, void *engine, unsigned choice) {
    const af_story_dialogue *d = af_scene_story_dialogue(s);
    if (!d || choice > (d->choices[0][0] ? 1u : 0u))
        return false;
    const char *output = d->choices[0][0] ? (choice ? "Second" : "First") : "Continue";
    s->story_dialogue = (af_story_dialogue){0};
    return af_blueprint_dispatch_story(af_scene_get_blueprint(s, s->story_graph), s, engine, s->story_node, output);
}

bool af_story_request_destination(af_scene *s, const char *name) {
    if (!s || s->story_destination[0] || !identifier(name, sizeof(s->story_destination)))
        return false;
    memcpy(s->story_destination, name, strlen(name) + 1);
    return true;
}

bool af_scene_take_story_destination(af_scene *s, char out[AF_STORY_LABEL_CAPACITY]) {
    if (!s || !out || !s->story_destination[0])
        return false;
    memcpy(out, s->story_destination, sizeof(s->story_destination));
    s->story_destination[0] = 0;
    return true;
}
