/* SPDX-License-Identifier: MPL-2.0 */
#include <afterlight/input.h>
#include "cJSON.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
bool af_json_valid_text(const char *, size_t);
static bool valid(af_action_binding b) {
    return b.name[0] && memchr(b.name, 0, sizeof(b.name)) && b.negative_key >= -1 && b.negative_key < AF_KEY_COUNT &&
           b.positive_key >= -1 && b.positive_key < AF_KEY_COUNT && b.pad_button >= -1 && b.pad_button < 32 &&
           b.pad_axis >= -1 && b.pad_axis < 8 && b.mouse_axis >= -1 && b.mouse_axis < 2 && isfinite(b.deadzone) &&
           b.deadzone >= 0 && b.deadzone < 1 && isfinite(b.scale) && fabsf(b.scale) <= 10;
}
bool af_input_bind(af_input_map *m, af_action_binding b) {
    if (!m || !valid(b) || m->count > AF_INPUT_MAX_ACTIONS)
        return false;
    size_t at = 0;
    while (at < m->count && strcmp(m->bindings[at].name, b.name))
        ++at;
    if (at == AF_INPUT_MAX_ACTIONS)
        return false;
    m->bindings[at] = b;
    if (at == m->count)
        ++m->count;
    return true;
}
af_input_map af_input_map_default(void) {
    af_input_map m = {0};
    af_action_binding b = {.negative_key = -1,
                           .positive_key = -1,
                           .pad_button = -1,
                           .pad_axis = -1,
                           .mouse_axis = -1,
                           .deadzone = .18f,
                           .scale = 1};
#define BIND(name_, neg, pos, button, axis, mouse, sign)                                                               \
    do {                                                                                                               \
        snprintf(b.name, sizeof(b.name), "%s", name_);                                                                 \
        b.negative_key = neg;                                                                                          \
        b.positive_key = pos;                                                                                          \
        b.pad_button = button;                                                                                         \
        b.pad_axis = axis;                                                                                             \
        b.mouse_axis = mouse;                                                                                          \
        b.scale = sign;                                                                                                \
        af_input_bind(&m, b);                                                                                          \
    } while (0)
    BIND("MoveX", AF_KEY_A, AF_KEY_D, -1, 0, -1, 1);
    BIND("MoveY", AF_KEY_S, AF_KEY_W, -1, 1, -1, -1);
    BIND("LookX", -1, -1, -1, 2, 0, 1);
    BIND("LookY", -1, -1, -1, 3, 1, 1);
    BIND("Jump", -1, AF_KEY_SPACE, 0, -1, -1, 1);
    BIND("Run", -1, AF_KEY_LEFT_SHIFT, 7, -1, -1, 1);
    BIND("Interact", -1, AF_KEY_E, 2, -1, -1, 1);
    BIND("Restart", -1, AF_KEY_R, 3, -1, -1, 1);
    BIND("Release", -1, AF_KEY_ESCAPE, 6, -1, -1, 1);
    BIND("NextScene", -1, AF_KEY_N, 14, -1, -1, 1);
    BIND("Spawn", -1, AF_KEY_Q, 12, -1, -1, 1);
    BIND("Grab", -1, AF_KEY_F, 1, -1, -1, 1);
    BIND("Throw", -1, AF_KEY_V, 10, -1, -1, 1);
    BIND("MotorToggle", -1, AF_KEY_Z, -1, -1, -1, 1);
    BIND("MotorReverse", -1, AF_KEY_X, -1, -1, -1, 1);
#undef BIND
    return m;
}
float af_input_value(const af_input_map *m, const af_input_sample *s, const char *name) {
    if (!m || !s || !name || m->count > AF_INPUT_MAX_ACTIONS)
        return 0;
    for (size_t i = 0; i < m->count; ++i) {
        af_action_binding b = m->bindings[i];
        if (!valid(b) || strcmp(name, b.name))
            continue;
        float key = (b.positive_key >= 0 && s->keys[b.positive_key] ? 1.f : 0) -
                    (b.negative_key >= 0 && s->keys[b.negative_key] ? 1.f : 0);
        float pad = b.pad_button >= 0 && s->buttons[b.pad_button] ? 1.f : 0;
        if (b.pad_axis >= 0) {
            float axis = s->axes[b.pad_axis];
            if (!isfinite(axis))
                axis = 0;
            axis = fmaxf(-1, fminf(1, axis));
            axis =
                fabsf(axis) > b.deadzone ? copysignf((fabsf(axis) - b.deadzone) / (1 - b.deadzone), axis) * b.scale : 0;
            if (fabsf(axis) > fabsf(pad))
                pad = axis;
        }
        float value = fabsf(key) >= fabsf(pad) ? key : pad;
        if (b.mouse_axis >= 0) {
            float mouse = b.mouse_axis == 0 ? s->mouse.x : s->mouse.y;
            if (isfinite(mouse) && mouse != 0)
                value = mouse * b.scale;
        }
        return value;
    }
    return 0;
}
static bool fail(af_asset_error *e) {
    if (e)
        snprintf(e->message, sizeof(e->message), "Invalid/unreadable input map (up to 32 unique actions).");
    return false;
}
bool af_input_map_save(const char *path, const af_input_map *m, af_asset_error *e) {
    if (e)
        *e = (af_asset_error){0};
    if (!path || !m || m->count > 32)
        return fail(e);
    cJSON *root = cJSON_CreateArray();
    if (!root)
        return fail(e);
    bool ok = true;
    for (size_t i = 0; i < m->count && ok; ++i) {
        af_action_binding b = m->bindings[i];
        if (!valid(b)) {
            ok = false;
            break;
        }
        cJSON *j = cJSON_CreateObject();
        if (!j || !cJSON_AddItemToArray(root, j)) {
            cJSON_Delete(j);
            ok = false;
            break;
        }
        ok = cJSON_AddStringToObject(j, "name", b.name) && cJSON_AddNumberToObject(j, "negative_key", b.negative_key) &&
             cJSON_AddNumberToObject(j, "positive_key", b.positive_key) &&
             cJSON_AddNumberToObject(j, "pad_button", b.pad_button) &&
             cJSON_AddNumberToObject(j, "pad_axis", b.pad_axis) &&
             cJSON_AddNumberToObject(j, "mouse_axis", b.mouse_axis) &&
             cJSON_AddNumberToObject(j, "deadzone", b.deadzone) && cJSON_AddNumberToObject(j, "scale", b.scale);
    }
    char *text = ok ? cJSON_Print(root) : NULL;
    cJSON_Delete(root);
    if (!text)
        return fail(e);
    char temporary[4096];
    if (snprintf(temporary, sizeof(temporary), "%s.tmp", path) >= (int)sizeof(temporary)) {
        free(text);
        return fail(e);
    }
    FILE *f = fopen(temporary, "wbx");
    if (!f) {
        free(text);
        return fail(e);
    }
    ok = fwrite(text, 1, strlen(text), f) == strlen(text);
    if (fclose(f))
        ok = false;
    free(text);
    if (ok)
        ok = rename(temporary, path) == 0;
    if (!ok)
        remove(temporary);
    return ok || fail(e);
}
bool af_input_map_load(const char *path, af_input_map *out, af_asset_error *e) {
    if (e)
        *e = (af_asset_error){0};
    if (!path || !out)
        return fail(e);
    FILE *f = fopen(path, "rb");
    if (!f)
        return fail(e);
    char text[32769];
    size_t n = fread(text, 1, sizeof(text), f);
    bool ok = !ferror(f) && n < sizeof(text);
    fclose(f);
    if (!ok)
        return fail(e);
    text[n] = 0;
    if (!af_json_valid_text(text, n))
        return fail(e);
    cJSON *root = cJSON_ParseWithLength(text, n + 1);
    af_input_map m = {0};
    ok = cJSON_IsArray(root) && cJSON_GetArraySize(root) <= 32;
    const char *fields[] = {"negative_key", "positive_key", "pad_button", "pad_axis",
                            "mouse_axis",   "deadzone",     "scale"};
    for (const cJSON *j = root ? root->child : NULL; j && ok; j = j->next) {
        af_action_binding b = {0};
        const cJSON *name = cJSON_GetObjectItemCaseSensitive(j, "name");
        if (!cJSON_IsString(name) || !name->valuestring[0] || strlen(name->valuestring) >= 32) {
            ok = false;
            break;
        }
        snprintf(b.name, sizeof(b.name), "%s", name->valuestring);
        double v[7];
        for (unsigned i = 0; i < 7; ++i) {
            const cJSON *value = cJSON_GetObjectItemCaseSensitive(j, fields[i]);
            if (!cJSON_IsNumber(value) || !isfinite(value->valuedouble) || fabs(value->valuedouble) > 1000 ||
                (i < 5 && floor(value->valuedouble) != value->valuedouble)) {
                ok = false;
                break;
            }
            v[i] = value->valuedouble;
        }
        if (!ok)
            break;
        b.negative_key = (int)v[0];
        b.positive_key = (int)v[1];
        b.pad_button = (int)v[2];
        b.pad_axis = (int)v[3];
        b.mouse_axis = (int)v[4];
        b.deadzone = (float)v[5];
        b.scale = (float)v[6];
        size_t before = m.count;
        ok = af_input_bind(&m, b) && m.count == before + 1;
    }
    cJSON_Delete(root);
    if (!ok)
        return fail(e);
    *out = m;
    return true;
}
