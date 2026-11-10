/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "blueprint_io.h"
#include "scene/scene_internal.h"
#include <float.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *pin_type_to_str(af_bp_pin_type type) {
    switch (type) {
    case AF_BP_PIN_EXEC:
        return "exec";
    case AF_BP_PIN_FLOAT:
        return "float";
    case AF_BP_PIN_BOOL:
        return "bool";
    case AF_BP_PIN_VEC3:
        return "vec3";
    case AF_BP_PIN_OBJECT:
        return "object";
    case AF_BP_PIN_STRING:
        return "string";
    default:
        return "unknown";
    }
}

static bool read_u32(const cJSON *item, uint32_t *out) {
    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble) || item->valuedouble < 0 ||
        item->valuedouble > UINT32_MAX || floor(item->valuedouble) != item->valuedouble)
        return false;
    *out = (uint32_t)item->valuedouble;
    return true;
}

static bool read_object(const cJSON *item, af_object_id *out) {
    if (cJSON_IsNumber(item) && item->valuedouble >= 0 && item->valuedouble <= 9007199254740991.0 &&
        floor(item->valuedouble) == item->valuedouble) {
        *out = (af_object_id)item->valuedouble;
        return true;
    }
    if (!cJSON_IsString(item) || !item->valuestring[0])
        return false;
    af_object_id id = 0;
    for (const char *c = item->valuestring; *c; ++c) {
        if (*c < '0' || *c > '9' || id > (UINT64_MAX - (unsigned)(*c - '0')) / 10)
            return false;
        id = id * 10 + (unsigned)(*c - '0');
    }
    *out = id;
    return true;
}

static cJSON *object_id_json(af_object_id id) {
    if (id <= UINT64_C(9007199254740991))
        return cJSON_CreateNumber((double)id);
    char text[32];
    snprintf(text, sizeof(text), "%" PRIu64, id);
    return cJSON_CreateString(text);
}

static bool read_float(const cJSON *item, float *out) {
    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble) || fabs(item->valuedouble) > FLT_MAX)
        return false;
    *out = (float)item->valuedouble;
    return true;
}

static bool read_string(const cJSON *item, char *out, size_t size) {
    if (!cJSON_IsString(item) || strlen(item->valuestring) >= size)
        return false;
    memcpy(out, item->valuestring, strlen(item->valuestring) + 1);
    return true;
}

static bool put(cJSON *parent, const char *name, cJSON *item) {
    if (item && (name ? cJSON_AddItemToObject(parent, name, item) : cJSON_AddItemToArray(parent, item)))
        return true;
    cJSON_Delete(item);
    return false;
}

static cJSON *value_json(af_bp_value v) {
    switch (v.type) {
    case AF_BP_PIN_FLOAT:
        return cJSON_CreateNumber(v.f);
    case AF_BP_PIN_BOOL:
        return cJSON_CreateBool(v.b);
    case AF_BP_PIN_VEC3: {
        double values[] = {v.v.x, v.v.y, v.v.z};
        return cJSON_CreateDoubleArray(values, 3);
    }
    case AF_BP_PIN_OBJECT:
        return object_id_json(v.obj);
    case AF_BP_PIN_STRING:
        return cJSON_CreateString(v.s);
    default:
        return NULL;
    }
}

static bool read_value(const cJSON *j, af_bp_pin_type type, af_bp_value *out) {
    *out = (af_bp_value){.type = type};
    switch (type) {
    case AF_BP_PIN_FLOAT:
        return read_float(j, &out->f);
    case AF_BP_PIN_BOOL:
        out->b = cJSON_IsTrue(j);
        return cJSON_IsBool(j);
    case AF_BP_PIN_VEC3:
        return cJSON_IsArray(j) && cJSON_GetArraySize(j) == 3 && read_float(cJSON_GetArrayItem(j, 0), &out->v.x) &&
               read_float(cJSON_GetArrayItem(j, 1), &out->v.y) && read_float(cJSON_GetArrayItem(j, 2), &out->v.z);
    case AF_BP_PIN_OBJECT:
        return read_object(j, &out->obj);
    case AF_BP_PIN_STRING:
        return read_string(j, out->s, sizeof(out->s));
    default:
        return false;
    }
}

static cJSON *timeline_json(const af_timeline *t) {
    if (!af_timeline_valid(t))
        return NULL;
    cJSON *root = cJSON_CreateObject();
    if (!root)
        return NULL;
    cJSON *poses = cJSON_AddArrayToObject(root, "poses");
    cJSON *lights = cJSON_AddArrayToObject(root, "lights");
    cJSON *sounds = cJSON_AddArrayToObject(root, "sounds");
    if (!poses || !lights || !sounds || !cJSON_AddNumberToObject(root, "duration", t->duration))
        goto fail;
    for (size_t i = 0; i < t->pose_count; ++i) {
        const af_timeline_pose *key = &t->poses[i];
        af_transform pose = key->transform;
        double values[] = {key->time,       pose.position.x, pose.position.y, pose.position.z, pose.rotation.x,
                           pose.rotation.y, pose.rotation.z, pose.scale.x,    pose.scale.y,    pose.scale.z};
        if (!put(poses, NULL, cJSON_CreateDoubleArray(values, 10)))
            goto fail;
    }
    for (size_t i = 0; i < t->light_count; ++i) {
        double values[] = {t->lights[i].time, t->lights[i].intensity};
        if (!put(lights, NULL, cJSON_CreateDoubleArray(values, 2)))
            goto fail;
    }
    for (size_t i = 0; i < t->sound_count; ++i) {
        const af_timeline_sound *key = &t->sounds[i];
        cJSON *j = cJSON_CreateObject();
        if (!put(sounds, NULL, j) || !cJSON_AddNumberToObject(j, "time", key->time) ||
            !cJSON_AddNumberToObject(j, "gain", key->gain) || !cJSON_AddStringToObject(j, "clip", key->clip))
            goto fail;
    }
    return root;
fail:
    cJSON_Delete(root);
    return NULL;
}

static bool read_numbers(const cJSON *entry, float *values, int count) {
    if (!cJSON_IsArray(entry) || cJSON_GetArraySize(entry) != count)
        return false;
    for (int i = 0; i < count; ++i)
        if (!read_float(cJSON_GetArrayItem(entry, i), &values[i]))
            return false;
    return true;
}

static bool read_timeline(const cJSON *root, af_timeline *t) {
    const cJSON *poses = cJSON_GetObjectItemCaseSensitive(root, "poses");
    const cJSON *lights = cJSON_GetObjectItemCaseSensitive(root, "lights");
    const cJSON *sounds = cJSON_GetObjectItemCaseSensitive(root, "sounds");
    if (!cJSON_IsObject(root) || !cJSON_IsArray(poses) || !cJSON_IsArray(lights) || !cJSON_IsArray(sounds) ||
        cJSON_GetArraySize(poses) > (int)AF_TIMELINE_MAX_KEYS ||
        cJSON_GetArraySize(lights) > (int)AF_TIMELINE_MAX_KEYS ||
        cJSON_GetArraySize(sounds) > (int)AF_TIMELINE_MAX_KEYS ||
        !read_float(cJSON_GetObjectItemCaseSensitive(root, "duration"), &t->duration))
        return false;
    const cJSON *entry;
    cJSON_ArrayForEach(entry, poses) {
        float v[10];
        if (!read_numbers(entry, v, 10))
            return false;
        t->poses[t->pose_count++] =
            (af_timeline_pose){v[0], {{v[1], v[2], v[3]}, {v[4], v[5], v[6]}, {v[7], v[8], v[9]}}};
    }
    cJSON_ArrayForEach(entry, lights) {
        float v[2];
        if (!read_numbers(entry, v, 2))
            return false;
        t->lights[t->light_count++] = (af_timeline_light){v[0], v[1]};
    }
    cJSON_ArrayForEach(entry, sounds) {
        af_timeline_sound *key = &t->sounds[t->sound_count++];
        if (!read_float(cJSON_GetObjectItemCaseSensitive(entry, "time"), &key->time) ||
            !read_float(cJSON_GetObjectItemCaseSensitive(entry, "gain"), &key->gain) ||
            !read_string(cJSON_GetObjectItemCaseSensitive(entry, "clip"), key->clip, sizeof(key->clip)))
            return false;
    }
    return af_timeline_valid(t);
}

static cJSON *encode(const af_blueprint *bp) {
    if (!bp)
        return NULL;
    cJSON *root = cJSON_CreateObject();
    if (!root)
        return NULL;
    cJSON *vars = cJSON_AddArrayToObject(root, "variables");
    cJSON *nodes = cJSON_AddArrayToObject(root, "nodes");
    cJSON *links = cJSON_AddArrayToObject(root, "links");
    if (!vars || !nodes || !links || !cJSON_AddStringToObject(root, "format", "afterlight.blueprint") ||
        !cJSON_AddNumberToObject(root, "version", 1) || !cJSON_AddStringToObject(root, "name", bp->name) ||
        !put(root, "bound_object", object_id_json(bp->bound_object)) ||
        !cJSON_AddBoolToObject(root, "enabled", bp->enabled) ||
        !cJSON_AddNumberToObject(root, "next_node_id", bp->next_node_id) ||
        !cJSON_AddNumberToObject(root, "next_link_id", bp->next_link_id))
        goto fail;
    if (bp->interact_range != 0 &&
        (!isfinite(bp->interact_range) || bp->interact_range < .25f || bp->interact_range > 30 || !bp->bound_object ||
         !cJSON_AddNumberToObject(root, "interact_range", bp->interact_range) ||
         !cJSON_AddStringToObject(root, "interact_prompt", bp->interact_prompt)))
        goto fail;
    for (size_t i = 0; i < bp->var_count; ++i) {
        const af_bp_variable *v = &bp->variables[i];
        cJSON *j = cJSON_CreateObject();
        if (!put(vars, NULL, j) || !cJSON_AddStringToObject(j, "name", v->name) ||
            !cJSON_AddStringToObject(j, "type", pin_type_to_str(v->type)) || !put(j, "val", value_json(v->val)))
            goto fail;
    }
    for (size_t i = 0; i < bp->node_count; ++i) {
        const af_bp_node *n = &bp->nodes[i];
        cJSON *j = cJSON_CreateObject();
        if (!put(nodes, NULL, j) || !cJSON_AddNumberToObject(j, "id", n->id) ||
            !cJSON_AddNumberToObject(j, "type", n->type) || !cJSON_AddStringToObject(j, "title", n->title) ||
            !cJSON_AddNumberToObject(j, "x", n->x) || !cJSON_AddNumberToObject(j, "y", n->y) ||
            (n->extra_str[0] && !cJSON_AddStringToObject(j, "extra", n->extra_str)) ||
            (n->type == AF_BP_NODE_SHOW_DIALOGUE && !cJSON_AddStringToObject(j, "text", n->dialogue_text)))
            goto fail;
        if (n->type == AF_BP_NODE_TIMELINE && !put(j, "timeline", timeline_json(n->timeline)))
            goto fail;
        cJSON *pins = cJSON_AddArrayToObject(j, "pins");
        if (!pins)
            goto fail;
        for (uint32_t p = 0; p < n->pin_count; ++p) {
            const af_bp_pin *pin = &n->pins[p];
            cJSON *pj = cJSON_CreateObject();
            if (!put(pins, NULL, pj) || !cJSON_AddNumberToObject(pj, "id", pin->id) ||
                !cJSON_AddStringToObject(pj, "name", pin->name) ||
                !cJSON_AddStringToObject(pj, "type", pin_type_to_str(pin->type)) ||
                !cJSON_AddStringToObject(pj, "dir", pin->dir == AF_BP_PIN_INPUT ? "in" : "out") ||
                (pin->dir == AF_BP_PIN_INPUT && pin->type != AF_BP_PIN_EXEC &&
                 !put(pj, "def", value_json(pin->default_val))))
                goto fail;
        }
    }
    for (size_t i = 0; i < bp->link_count; ++i) {
        const af_bp_link *l = &bp->links[i];
        cJSON *j = cJSON_CreateObject();
        if (!put(links, NULL, j) || !cJSON_AddNumberToObject(j, "id", l->id) ||
            !cJSON_AddNumberToObject(j, "from_node", l->from_node_id) ||
            !cJSON_AddNumberToObject(j, "from_pin", l->from_pin_id) ||
            !cJSON_AddNumberToObject(j, "to_node", l->to_node_id) ||
            !cJSON_AddNumberToObject(j, "to_pin", l->to_pin_id))
            goto fail;
    }
    return root;
fail:
    cJSON_Delete(root);
    return NULL;
}

static bool read_pins(af_bp_node *node, const cJSON *pins) {
    if (!cJSON_IsArray(pins) || cJSON_GetArraySize(pins) > (int)node->pin_count)
        return false;
    uint32_t seen = 0;
    const cJSON *entry;
    cJSON_ArrayForEach(entry, pins) {
        uint32_t id;
        if (!read_u32(cJSON_GetObjectItemCaseSensitive(entry, "id"), &id))
            return false;
        uint32_t p = 0;
        while (p < node->pin_count && node->pins[p].id != id)
            ++p;
        if (p == node->pin_count || (seen & (1u << p)))
            return false;
        seen |= 1u << p;
        af_bp_pin *pin = &node->pins[p];
        const cJSON *def = cJSON_GetObjectItemCaseSensitive(entry, "def");
        if (def && (pin->dir != AF_BP_PIN_INPUT || !read_value(def, pin->type, &pin->default_val)))
            return false;
        pin->runtime_val = pin->default_val;
    }
    return true;
}

static af_blueprint *decode(const cJSON *root) {
    char name[64];
    uint32_t version, next_node, next_link;
    const cJSON *format = cJSON_GetObjectItemCaseSensitive(root, "format");
    const cJSON *enabled = cJSON_GetObjectItemCaseSensitive(root, "enabled");
    const cJSON *vars = cJSON_GetObjectItemCaseSensitive(root, "variables");
    const cJSON *nodes = cJSON_GetObjectItemCaseSensitive(root, "nodes");
    const cJSON *links = cJSON_GetObjectItemCaseSensitive(root, "links");
    if (!cJSON_IsString(format) || strcmp(format->valuestring, "afterlight.blueprint") ||
        !read_u32(cJSON_GetObjectItemCaseSensitive(root, "version"), &version) || version != 1 ||
        !read_string(cJSON_GetObjectItemCaseSensitive(root, "name"), name, sizeof(name)) || !name[0] ||
        !cJSON_IsBool(enabled) || !cJSON_IsArray(vars) || !cJSON_IsArray(nodes) || !cJSON_IsArray(links) ||
        cJSON_GetArraySize(vars) > AF_BP_MAX_VARIABLES || cJSON_GetArraySize(nodes) > AF_BP_MAX_NODES ||
        cJSON_GetArraySize(links) > AF_BP_MAX_LINKS ||
        !read_u32(cJSON_GetObjectItemCaseSensitive(root, "next_node_id"), &next_node) || !next_node ||
        !read_u32(cJSON_GetObjectItemCaseSensitive(root, "next_link_id"), &next_link) || !next_link)
        return NULL;
    af_blueprint *bp = af_blueprint_create(name);
    if (!bp)
        return NULL;
    bp->enabled = cJSON_IsTrue(enabled);
    if (!read_object(cJSON_GetObjectItemCaseSensitive(root, "bound_object"), &bp->bound_object))
        goto fail;
    const cJSON *range = cJSON_GetObjectItemCaseSensitive(root, "interact_range");
    if (range && (!read_float(range, &bp->interact_range) || bp->interact_range < .25f || bp->interact_range > 30 ||
                  !bp->bound_object ||
                  !read_string(cJSON_GetObjectItemCaseSensitive(root, "interact_prompt"), bp->interact_prompt,
                               sizeof(bp->interact_prompt))))
        goto fail;
    const cJSON *entry;
    cJSON_ArrayForEach(entry, vars) {
        char variable[AF_BP_NAME_CAPACITY];
        const cJSON *kind = cJSON_GetObjectItemCaseSensitive(entry, "type");
        if (!cJSON_IsString(kind) ||
            !read_string(cJSON_GetObjectItemCaseSensitive(entry, "name"), variable, sizeof(variable)))
            goto fail;
        af_bp_pin_type type = AF_BP_PIN_FLOAT;
        while (type <= AF_BP_PIN_STRING && strcmp(kind->valuestring, pin_type_to_str(type)))
            ++type;
        af_bp_value value;
        if (type > AF_BP_PIN_STRING || !read_value(cJSON_GetObjectItemCaseSensitive(entry, "val"), type, &value) ||
            !af_blueprint_add_variable(bp, variable, type, value))
            goto fail;
    }
    cJSON_ArrayForEach(entry, nodes) {
        uint32_t id, type;
        float x, y;
        if (!read_u32(cJSON_GetObjectItemCaseSensitive(entry, "id"), &id) || !id || id >= next_node ||
            af_blueprint_get_node(bp, id) || !read_u32(cJSON_GetObjectItemCaseSensitive(entry, "type"), &type) ||
            type >= AF_BP_NODE_TYPE_COUNT || !read_float(cJSON_GetObjectItemCaseSensitive(entry, "x"), &x) ||
            !read_float(cJSON_GetObjectItemCaseSensitive(entry, "y"), &y) ||
            !af_blueprint_add_node(bp, (af_bp_node_type)type, x, y))
            goto fail;
        af_bp_node *node = &bp->nodes[bp->node_count - 1];
        node->id = id;
        const cJSON *extra = cJSON_GetObjectItemCaseSensitive(entry, "extra");
        const cJSON *text = cJSON_GetObjectItemCaseSensitive(entry, "text");
        const cJSON *timeline = cJSON_GetObjectItemCaseSensitive(entry, "timeline");
        if ((extra && !read_string(extra, node->extra_str, sizeof(node->extra_str))) ||
            (text && (type != AF_BP_NODE_SHOW_DIALOGUE ||
                      !read_string(text, node->dialogue_text, sizeof(node->dialogue_text)))) ||
            (node->timeline ? !read_timeline(timeline, node->timeline) : timeline != NULL) ||
            !read_pins(node, cJSON_GetObjectItemCaseSensitive(entry, "pins")))
            goto fail;
    }
    cJSON_ArrayForEach(entry, links) {
        uint32_t id, from, output, to, input;
        if (!read_u32(cJSON_GetObjectItemCaseSensitive(entry, "id"), &id) || !id || id >= next_link ||
            !read_u32(cJSON_GetObjectItemCaseSensitive(entry, "from_node"), &from) ||
            !read_u32(cJSON_GetObjectItemCaseSensitive(entry, "from_pin"), &output) ||
            !read_u32(cJSON_GetObjectItemCaseSensitive(entry, "to_node"), &to) ||
            !read_u32(cJSON_GetObjectItemCaseSensitive(entry, "to_pin"), &input))
            goto fail;
        for (size_t i = 0; i < bp->link_count; ++i)
            if (bp->links[i].id == id)
                goto fail;
        size_t before = bp->link_count;
        if (!af_blueprint_add_link(bp, from, output, to, input) || bp->link_count != before + 1)
            goto fail;
        bp->links[before].id = id;
    }
    bp->next_node_id = next_node;
    bp->next_link_id = next_link;
    return bp;
fail:
    af_blueprint_destroy(bp);
    return NULL;
}

char *af_blueprint_serialize_json(const af_blueprint *bp) {
    cJSON *root = encode(bp);
    char *text = root ? cJSON_Print(root) : NULL;
    cJSON_Delete(root);
    return text;
}

af_blueprint *af_blueprint_deserialize_json(const char *text) {
    cJSON *root = text ? cJSON_ParseWithOpts(text, NULL, true) : NULL;
    af_blueprint *bp = root ? decode(root) : NULL;
    cJSON_Delete(root);
    return bp;
}

bool af_scene_encode_blueprints(const af_scene *scene, cJSON *root) {
    cJSON *graphs = cJSON_AddArrayToObject(root, "blueprints");
    if (!graphs)
        return false;
    for (size_t i = 0; i < scene->blueprint_count; ++i)
        if (!put(graphs, NULL, encode(scene->blueprints[i])))
            return false;
    return true;
}

bool af_scene_decode_blueprints(af_scene *scene, const cJSON *root) {
    const cJSON *graphs = cJSON_GetObjectItemCaseSensitive(root, "blueprints");
    if (!cJSON_IsArray(graphs) || cJSON_GetArraySize(graphs) > AF_SCENE_MAX_BLUEPRINTS)
        return false;
    const cJSON *entry;
    cJSON_ArrayForEach(entry, graphs) {
        af_blueprint *bp = decode(entry);
        if (!bp || (bp->bound_object && !af_scene_get_object(scene, bp->bound_object)) ||
            !af_scene_add_blueprint(scene, bp->name)) {
            af_blueprint_destroy(bp);
            return false;
        }
        size_t index = scene->blueprint_count - 1;
        af_blueprint_destroy(scene->blueprints[index]);
        scene->blueprints[index] = bp;
    }
    return true;
}

bool af_blueprint_save_file(const af_blueprint *bp, const char *path) {
    if (!bp || !path)
        return false;
    char *text = af_blueprint_serialize_json(bp);
    if (!text)
        return false;
    FILE *f = fopen(path, "wb");
    if (!f) {
        free(text);
        return false;
    }
    size_t len = strlen(text);
    bool ok = fwrite(text, 1, len, f) == len;
    fclose(f);
    free(text);
    return ok;
}

af_blueprint *af_blueprint_load_file(const char *path) {
    if (!path)
        return NULL;
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0 || size > 16 * 1024 * 1024) {
        fclose(f);
        return NULL;
    }
    char *buf = (char *)malloc((size_t)size + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t read_bytes = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[read_bytes] = '\0';

    af_blueprint *bp = af_blueprint_deserialize_json(buf);
    free(buf);
    return bp;
}
