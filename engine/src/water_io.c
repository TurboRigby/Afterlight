/* SPDX-License-Identifier: MPL-2.0 */
#include "water_internal.h"
#include "cJSON.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static const char *fields[] = {
    "owner",          "enabled",       "mode",       "quality",          "half_size", "depth",
    "density",        "drag",          "wave_speed", "damping",          "wind",      "absorption",
    "scattering",     "roughness",     "ior",        "flow_speed",       "path",      "particle_spacing",
    "initial_volume", "emission_rate", "viscosity",  "emission_velocity", "ripple_scale", "ripple_strength"};
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
    unsigned fields_count = cJSON_GetObjectItemCaseSensitive(root, "version")->valueint >= 22 ? 24u
                            : cJSON_GetObjectItemCaseSensitive(root, "version")->valueint >= 19   ? 22u
                            : cJSON_GetObjectItemCaseSensitive(root, "version")->valueint >= 18 ? 17u
                                                                                                : 15u;
    cJSON_ArrayForEach(j, array) {
        if (!cJSON_IsObject(j) || cJSON_GetArraySize(j) != (int)fields_count)
            return false;
        for (unsigned i = 0; i < fields_count; ++i) {
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
            !cJSON_IsBool(enabled) || !cJSON_IsString(mode) ||
            (strcmp(mode->valuestring, "pool") && strcmp(mode->valuestring, "river") &&
             strcmp(mode->valuestring, "particles")))
            return false;
        w.owner = strtoull(id->valuestring, &end, 10);
        char canonical[32];
        snprintf(canonical, sizeof(canonical), "%llu", (unsigned long long)w.owner);
        if (!end || *end || !w.owner || strcmp(canonical, id->valuestring) ||
            af_scene_get_water(s, w.owner))
            return false;
        w.mode = !strcmp(mode->valuestring, "particles") ? AF_WATER_PARTICLES
                 : !strcmp(mode->valuestring, "river")   ? AF_WATER_RIVER
                                                         : AF_WATER_POOL;
        if (fields_count >= 17) {
            const cJSON *path = cJSON_GetObjectItemCaseSensitive(j, "path");
            if (!number(cJSON_GetObjectItemCaseSensitive(j, "flow_speed"), &w.flow_speed) ||
                !cJSON_IsArray(path) || cJSON_GetArraySize(path) > (int)AF_WATER_MAX_PATH ||
                cJSON_GetArraySize(path) < 2)
                return false;
            w.path_count = (unsigned)cJSON_GetArraySize(path);
            for (unsigned i = 0; i < w.path_count; ++i) {
                float point[3];
                if (!vec(cJSON_GetArrayItem(path, (int)i), point, 3))
                    return false;
                w.path[i] = (af_vec3){point[0], point[1], point[2]};
            }
        } else if (w.mode != AF_WATER_POOL)
            return false;
        if (fields_count >= 22) {
            float *v[] = {&w.particle_spacing, &w.initial_volume, &w.emission_rate, &w.viscosity};
            for (unsigned k = 0; k < 4; ++k)
                if (!number(cJSON_GetObjectItemCaseSensitive(j, fields[17 + k]), v[k]))
                    return false;
            float xyz[3];
            if (!vec(cJSON_GetObjectItemCaseSensitive(j, "emission_velocity"), xyz, 3))
                return false;
            w.emission_velocity = (af_vec3){xyz[0], xyz[1], xyz[2]};
        } else if (w.mode == AF_WATER_PARTICLES)
            return false;
        if(fields_count>=24 && (!number(cJSON_GetObjectItemCaseSensitive(j,"ripple_scale"),&w.ripple_scale)||!number(cJSON_GetObjectItemCaseSensitive(j,"ripple_strength"),&w.ripple_strength)))return false;
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
            !cJSON_AddStringToObject(j, "mode",
                                     w.mode == AF_WATER_PARTICLES ? "particles"
                                     : w.mode == AF_WATER_RIVER   ? "river"
                                                                  : "pool") ||
            !cJSON_AddNumberToObject(j, "quality", w.quality) || !addvec(j, "half_size", pair, 2) ||
            !addvec(j, "absorption", a, 3) || !addvec(j, "scattering", b, 3))
            return false;
        if (!cJSON_AddNumberToObject(j, "flow_speed", w.flow_speed))
            return false;
        cJSON *path = cJSON_AddArrayToObject(j, "path");
        if (!path || w.path_count > AF_WATER_MAX_PATH || w.path_count < 2)
            return false;
        for (unsigned k = 0; k < w.path_count; ++k) {
            float point[] = {w.path[k].x, w.path[k].y, w.path[k].z};
            if (!cJSON_AddItemToArray(path, cJSON_CreateFloatArray(point, 3)))
                return false;
        }
        float particle_values[] = {w.particle_spacing, w.initial_volume, w.emission_rate, w.viscosity};
        for (unsigned k = 0; k < 4; ++k)
            if (!cJSON_AddNumberToObject(j, fields[17 + k], particle_values[k]))
                return false;
        float velocity[] = {w.emission_velocity.x, w.emission_velocity.y, w.emission_velocity.z};
        if (!addvec(j, "emission_velocity", velocity, 3))
            return false;
        if(!cJSON_AddNumberToObject(j,"ripple_scale",w.ripple_scale)||!cJSON_AddNumberToObject(j,"ripple_strength",w.ripple_strength))return false;
        float values[] = {w.depth,   w.density, w.drag,      w.wave_speed,
                          w.damping, w.wind,    w.roughness, w.ior};
        unsigned indexes[] = {5, 6, 7, 8, 9, 10, 13, 14};
        for (unsigned k = 0; k < 8; ++k)
            if (!cJSON_AddNumberToObject(j, fields[indexes[k]], values[k]))
                return false;
    }
    return true;
}
