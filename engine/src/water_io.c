/* SPDX-License-Identifier: MPL-2.0 */
#include "water_internal.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static const char *fields[] = {"owner", "enabled",    "mode",       "quality",    "half_size",
                               "depth", "density",    "drag",       "wave_speed", "damping",
                               "wind",  "absorption", "scattering", "roughness",  "ior"};
static bool number(const cJSON *j, float *v) {
    if (!cJSON_IsNumber(j) || !isfinite(j->valuedouble) || fabs(j->valuedouble) > 1e9)
        return false;
    *v = (float)j->valuedouble;
    return true;
}
static bool vec(const cJSON *j, float *v, unsigned n) {
    if (!cJSON_IsArray(j) || cJSON_GetArraySize(j) != (int)n)
        return false;
    for (unsigned i = 0; i < n; ++i)
        if (!number(cJSON_GetArrayItem(j, (int)i), &v[i]))
            return false;
    return true;
}
bool wa_decode(af_scene *s, const cJSON *root) {
    const cJSON *array = cJSON_GetObjectItemCaseSensitive(root, "water"), *j;
    if (!cJSON_IsArray(array) || cJSON_GetArraySize(array) > (int)AF_SCENE_MAX_WATERS)
        return false;
    cJSON_ArrayForEach(j, array) {
        if (!cJSON_IsObject(j) || cJSON_GetArraySize(j) != 15)
            return false;
        for (unsigned i = 0; i < 15; ++i) {
            unsigned found = 0;
            for (const cJSON *x = j->child; x; x = x->next)
                found += x->string && !strcmp(x->string, fields[i]);
            if (found != 1)
                return false;
        }
        af_water w = af_water_default();
        const cJSON *id = cJSON_GetObjectItemCaseSensitive(j, "owner"),
                    *enabled = cJSON_GetObjectItemCaseSensitive(j, "enabled"),
                    *mode = cJSON_GetObjectItemCaseSensitive(j, "mode");
        char *end = NULL;
        if (!cJSON_IsString(id) || !id->valuestring[0] || id->valuestring[0] == '-' ||
            !cJSON_IsBool(enabled) || !cJSON_IsString(mode) || strcmp(mode->valuestring, "pool"))
            return false;
        w.owner = strtoull(id->valuestring, &end, 10);
        char canonical[32];
        snprintf(canonical, sizeof(canonical), "%llu", (unsigned long long)w.owner);
        if (!end || *end || !w.owner || strcmp(canonical, id->valuestring) ||
            af_scene_get_water(s, w.owner))
            return false;
        w.enabled = cJSON_IsTrue(enabled) != 0;
        float quality;
        if (!number(cJSON_GetObjectItemCaseSensitive(j, "quality"), &quality) || quality < 0 ||
            quality > 2 || quality != floorf(quality))
            return false;
        w.quality = (af_water_quality)(int)quality;
        float pair[2], a[3], b[3];
        if (!vec(cJSON_GetObjectItemCaseSensitive(j, "half_size"), pair, 2) ||
            !vec(cJSON_GetObjectItemCaseSensitive(j, "absorption"), a, 3) ||
            !vec(cJSON_GetObjectItemCaseSensitive(j, "scattering"), b, 3))
            return false;
        w.half_size = (af_vec2){pair[0], pair[1]};
        w.absorption = (af_vec3){a[0], a[1], a[2]};
        w.scattering = (af_vec3){b[0], b[1], b[2]};
        float *values[] = {&w.depth,   &w.density, &w.drag,      &w.wave_speed,
                           &w.damping, &w.wind,    &w.roughness, &w.ior};
        unsigned indexes[] = {5, 6, 7, 8, 9, 10, 13, 14};
        for (unsigned i = 0; i < 8; ++i)
            if (!number(cJSON_GetObjectItemCaseSensitive(j, fields[indexes[i]]), values[i]))
                return false;
        if (!af_scene_set_water(s, w.owner, w))
            return false;
    }
    return true;
}
static bool addvec(cJSON *j, const char *name, const float *v, int n) {
    return cJSON_AddItemToObject(j, name, cJSON_CreateFloatArray(v, n));
}
bool wa_encode(const af_scene *s, cJSON *root) {
    cJSON *array = cJSON_AddArrayToObject(root, "water");
    if (!array)
        return false;
    for (size_t i = 0; i < af_scene_water_count(s); ++i) {
        af_water w = *af_scene_water_at(s, i);
        if (!wa_owner_valid(s, w.owner, w))
            return false;
        cJSON *j = cJSON_CreateObject();
        if (!j || !cJSON_AddItemToArray(array, j))
            return false;
        char id[32];
        snprintf(id, sizeof(id), "%llu", (unsigned long long)w.owner);
        float pair[] = {w.half_size.x, w.half_size.y},
              a[] = {w.absorption.x, w.absorption.y, w.absorption.z},
              b[] = {w.scattering.x, w.scattering.y, w.scattering.z};
        if (!cJSON_AddStringToObject(j, "owner", id) ||
            !cJSON_AddBoolToObject(j, "enabled", w.enabled) ||
            !cJSON_AddStringToObject(j, "mode", "pool") ||
            !cJSON_AddNumberToObject(j, "quality", w.quality) || !addvec(j, "half_size", pair, 2) ||
            !addvec(j, "absorption", a, 3) || !addvec(j, "scattering", b, 3))
            return false;
        float values[] = {w.depth,   w.density, w.drag,      w.wave_speed,
                          w.damping, w.wind,    w.roughness, w.ior};
        unsigned indexes[] = {5, 6, 7, 8, 9, 10, 13, 14};
        for (unsigned k = 0; k < 8; ++k)
            if (!cJSON_AddNumberToObject(j, fields[indexes[k]], values[k]))
                return false;
    }
    return true;
}
