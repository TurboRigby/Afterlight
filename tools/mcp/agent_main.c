/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "agent_internal.h"

#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

cJSON *af_agent_ok(cJSON *data) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", 1);
    if (data)
        cJSON_AddItemToObject(root, "data", data);
    else
        cJSON_AddObjectToObject(root, "data");
    return root;
}

cJSON *af_agent_err(const char *message) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", 0);
    cJSON_AddStringToObject(root, "error", message ? message : "unknown error");
    return root;
}

bool af_agent_has(const cJSON *obj, const char *key) {
    if (!obj || !key)
        return false;
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    return item != NULL && !cJSON_IsNull(item);
}

const char *af_agent_str(const cJSON *obj, const char *key, const char *fallback) {
    if (!obj || !key)
        return fallback;
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsString(item) && item->valuestring ? item->valuestring : fallback;
}

bool af_agent_bool(const cJSON *obj, const char *key, bool fallback) {
    if (!obj || !key)
        return fallback;
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsBool(item) ? cJSON_IsTrue(item) != 0 : fallback;
}

double af_agent_num(const cJSON *obj, const char *key, double fallback) {
    if (!obj || !key)
        return fallback;
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsNumber(item) ? item->valuedouble : fallback;
}

float af_agent_f32(const cJSON *obj, const char *key, float fallback) {
    return (float)af_agent_num(obj, key, (double)fallback);
}

uint32_t af_agent_u32(const cJSON *obj, const char *key, uint32_t fallback) {
    double v = af_agent_num(obj, key, -1.0);
    if (v < 0.0 || v > (double)UINT32_MAX)
        return fallback;
    return (uint32_t)v;
}

int af_agent_i32(const cJSON *obj, const char *key, int fallback) {
    if (!af_agent_has(obj, key))
        return fallback;
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsNumber(item) ? (int)llround(item->valuedouble) : fallback;
}

bool af_agent_vec2(const cJSON *obj, const char *key, af_vec2 *out) {
    if (!obj || !key || !out)
        return false;
    const cJSON *arr = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsArray(arr) || cJSON_GetArraySize(arr) < 2)
        return false;
    const cJSON *a = cJSON_GetArrayItem(arr, 0);
    const cJSON *b = cJSON_GetArrayItem(arr, 1);
    if (!cJSON_IsNumber(a) || !cJSON_IsNumber(b))
        return false;
    *out = (af_vec2){(float)a->valuedouble, (float)b->valuedouble};
    return true;
}

bool af_agent_vec3(const cJSON *obj, const char *key, af_vec3 *out) {
    if (!obj || !key || !out)
        return false;
    const cJSON *arr = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsArray(arr) || cJSON_GetArraySize(arr) < 3)
        return false;
    const cJSON *a = cJSON_GetArrayItem(arr, 0);
    const cJSON *b = cJSON_GetArrayItem(arr, 1);
    const cJSON *c = cJSON_GetArrayItem(arr, 2);
    if (!cJSON_IsNumber(a) || !cJSON_IsNumber(b) || !cJSON_IsNumber(c))
        return false;
    *out = (af_vec3){(float)a->valuedouble, (float)b->valuedouble, (float)c->valuedouble};
    return true;
}

bool af_agent_color(const cJSON *obj, const char *key, af_color *out) {
    if (!obj || !key || !out)
        return false;
    const cJSON *arr = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsArray(arr) || cJSON_GetArraySize(arr) < 3)
        return false;
    const cJSON *r = cJSON_GetArrayItem(arr, 0);
    const cJSON *g = cJSON_GetArrayItem(arr, 1);
    const cJSON *b = cJSON_GetArrayItem(arr, 2);
    if (!cJSON_IsNumber(r) || !cJSON_IsNumber(g) || !cJSON_IsNumber(b))
        return false;
    float a = 1.0f;
    if (cJSON_GetArraySize(arr) >= 4) {
        const cJSON *alpha = cJSON_GetArrayItem(arr, 3);
        if (cJSON_IsNumber(alpha))
            a = (float)alpha->valuedouble;
    }
    *out = (af_color){(float)r->valuedouble, (float)g->valuedouble, (float)b->valuedouble, a};
    return true;
}

af_object_id af_agent_id(const cJSON *obj, const char *key) {
    if (!obj || !key)
        return AF_OBJECT_NONE;
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsNumber(item) && item->valuedouble >= 0.0)
        return (af_object_id)llround(item->valuedouble);
    if (cJSON_IsString(item) && item->valuestring && item->valuestring[0]) {
        char *end = NULL;
        errno = 0;
        unsigned long long v = strtoull(item->valuestring, &end, 10);
        if (errno == 0 && end && *end == '\0')
            return (af_object_id)v;
    }
    return AF_OBJECT_NONE;
}

af_object_id af_agent_resolve_object(const af_scene *scene, const cJSON *args) {
    if (!scene || !args)
        return AF_OBJECT_NONE;
    af_object_id id = af_agent_id(args, "id");
    if (id == AF_OBJECT_NONE)
        id = af_agent_id(args, "object_id");
    if (id != AF_OBJECT_NONE)
        return af_scene_get_object(scene, id) ? id : AF_OBJECT_NONE;
    const char *name = af_agent_str(args, "object_name", NULL);
    if (!name)
        name = af_agent_str(args, "name", NULL);
    if (name && name[0]) {
        for (size_t i = 0; i < af_scene_object_count(scene); ++i) {
            const af_scene_object *o = af_scene_object_at(scene, i);
            if (!strcmp(o->name, name))
                return o->id;
        }
    }
    return AF_OBJECT_NONE;
}

void af_agent_copy_str(char *dst, size_t cap, const char *src) {
    if (!dst || cap == 0)
        return;
    snprintf(dst, cap, "%s", src ? src : "");
}

cJSON *af_agent_json_vec2(af_vec2 v) {
    cJSON *arr = cJSON_CreateArray();
    cJSON_AddItemToArray(arr, cJSON_CreateNumber(v.x));
    cJSON_AddItemToArray(arr, cJSON_CreateNumber(v.y));
    return arr;
}

cJSON *af_agent_json_vec3(af_vec3 v) {
    cJSON *arr = cJSON_CreateArray();
    cJSON_AddItemToArray(arr, cJSON_CreateNumber(v.x));
    cJSON_AddItemToArray(arr, cJSON_CreateNumber(v.y));
    cJSON_AddItemToArray(arr, cJSON_CreateNumber(v.z));
    return arr;
}

cJSON *af_agent_json_color(af_color c) {
    cJSON *arr = cJSON_CreateArray();
    cJSON_AddItemToArray(arr, cJSON_CreateNumber(c.r));
    cJSON_AddItemToArray(arr, cJSON_CreateNumber(c.g));
    cJSON_AddItemToArray(arr, cJSON_CreateNumber(c.b));
    cJSON_AddItemToArray(arr, cJSON_CreateNumber(c.a));
    return arr;
}

cJSON *af_agent_json_id(af_object_id id) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%" PRIu64, id);
    return cJSON_CreateString(buf);
}

static char *read_all_stream(FILE *f) {
    size_t cap = 4096, len = 0;
    char *buf = malloc(cap);
    if (!buf)
        return NULL;
    for (;;) {
        if (len + 4096 + 1 > cap) {
            cap *= 2;
            char *next = realloc(buf, cap);
            if (!next) {
                free(buf);
                return NULL;
            }
            buf = next;
        }
        size_t n = fread(buf + len, 1, 4096, f);
        len += n;
        if (n < 4096)
            break;
    }
    buf[len] = '\0';
    return buf;
}

int main(int argc, char **argv) {
    const char *req_path = NULL;
    const char *resp_path = NULL;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--request") && i + 1 < argc)
            req_path = argv[++i];
        else if (!strcmp(argv[i], "--response") && i + 1 < argc)
            resp_path = argv[++i];
        else if (!strcmp(argv[i], "--version")) {
            printf("%s\n", AF_VERSION_STRING);
            return 0;
        }
    }

    FILE *in = req_path ? fopen(req_path, "rb") : stdin;
    if (!in) {
        cJSON *err = af_agent_err("Failed to open request file.");
        char *text = cJSON_PrintUnformatted(err);
        puts(text ? text : "{\"ok\":false,\"error\":\"oom\"}");
        free(text);
        cJSON_Delete(err);
        return 1;
    }
    char *raw = read_all_stream(in);
    if (req_path)
        fclose(in);

    cJSON *req = raw ? cJSON_Parse(raw) : NULL;
    free(raw);

    cJSON *resp = NULL;
    if (!req || !cJSON_IsObject(req)) {
        resp = af_agent_err("Request must be a JSON object with 'command' and 'args'.");
    } else {
        const char *cmd = af_agent_str(req, "command", NULL);
        const cJSON *args = cJSON_GetObjectItemCaseSensitive(req, "args");
        if (!cmd || !cmd[0]) {
            resp = af_agent_err("Missing 'command' field.");
        } else if (!strcmp(cmd, "capture")) {
            resp = af_agent_cmd_capture(args);
        } else {
            resp = af_agent_dispatch_scene(cmd, args);
        }
    }

    cJSON_Delete(req);
    if (!resp)
        resp = af_agent_err("Internal error: empty response.");

    char *out = cJSON_PrintUnformatted(resp);
    const char *text = out ? out : "{\"ok\":false,\"error\":\"JSON serialization failed.\"}";
    if (resp_path) {
        FILE *rf = fopen(resp_path, "wb");
        if (rf) {
            fwrite(text, 1, strlen(text), rf);
            fputc('\n', rf);
            fclose(rf);
        } else {
            puts(text);
        }
    } else {
        puts(text);
    }
    free(out);
    bool ok = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(resp, "ok")) != 0;
    cJSON_Delete(resp);
    return ok ? 0 : 1;
}
