/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <afterlight/project.h>
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#endif
/* Shared strict JSON lexical validation with scene files. */
bool af_json_valid_text(const char *text, size_t length);
static bool fail(af_asset_error *error, const char *message) {
    if (error)
        snprintf(error->message, sizeof(error->message), "%s", message);
    return false;
}
static bool fields(const cJSON *object) {
    static const char *const keys[] = {"format", "version", "name", "startup_scene", "game_module"};
    unsigned seen = 0;
    const cJSON *item;
    if (!cJSON_IsObject(object))
        return false;
    cJSON_ArrayForEach(item, object) {
        unsigned i;
        for (i = 0; i < 5; ++i)
            if (!strcmp(item->string, keys[i]))
                break;
        if (i == 5 || (seen & (1u << i)))
            return false;
        seen |= 1u << i;
    }
    return seen == 31;
}
static bool text_field(const cJSON *root, const char *key, char *out, size_t capacity) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!cJSON_IsString(item) || !item->valuestring[0] || strlen(item->valuestring) >= capacity)
        return false;
    snprintf(out, capacity, "%s", item->valuestring);
    return true;
}
bool af_project_load(const char *path, af_project *out, af_asset_error *error) {
    if (error)
        *error = (af_asset_error){0};
    if (!path || !out)
        return fail(error, "Project path and output required.");
    FILE *file = fopen(path, "rb");
    if (!file)
        return fail(error, "Cannot open project.");
    long length = 0;
    char *bytes = NULL;
    bool ok = false;
    if (!fseek(file, 0, SEEK_END) && (length = ftell(file)) > 0 && length < 65536 && !fseek(file, 0, SEEK_SET)) {
        bytes = malloc((size_t)length + 1);
        if (bytes) {
            ok = fread(bytes, 1, (size_t)length, file) == (size_t)length;
            bytes[length] = 0;
        }
    }
    if (fclose(file))
        ok = false;
    if (!ok || !af_json_valid_text(bytes, (size_t)length)) {
        free(bytes);
        return fail(error, "Invalid/oversized project JSON.");
    }
    cJSON *root = cJSON_ParseWithOpts(bytes, NULL, 1);
    free(bytes);
    af_project value = {0};
    char scene[AF_ASSET_PATH_CAPACITY], module[AF_ASSET_PATH_CAPACITY];
    const cJSON *format = cJSON_GetObjectItemCaseSensitive(root, "format"),
                *version = cJSON_GetObjectItemCaseSensitive(root, "version");
    ok = fields(root) && cJSON_IsString(format) && !strcmp(format->valuestring, "afterlight.project") &&
         cJSON_IsNumber(version) && version->valuedouble == AF_PROJECT_VERSION &&
         text_field(root, "name", value.name, sizeof(value.name)) &&
         text_field(root, "startup_scene", scene, sizeof(scene)) &&
         text_field(root, "game_module", module, sizeof(module)) &&
         af_asset_resolve_path(path, scene, value.startup_scene, sizeof(value.startup_scene)) &&
         af_asset_resolve_path(path, module, value.game_module, sizeof(value.game_module));
    cJSON_Delete(root);
    if (!ok)
        return fail(error, "Invalid project fields or unsupported version.");
    *out = value;
    return true;
}
bool af_project_save(const char *path, const af_project *project, af_asset_error *error) {
    if (error)
        *error = (af_asset_error){0};
    if (!path || !path[0] || !project || !memchr(project->name, 0, sizeof(project->name)) || !project->name[0] ||
        !memchr(project->startup_scene, 0, sizeof(project->startup_scene)) || !project->startup_scene[0] ||
        !memchr(project->game_module, 0, sizeof(project->game_module)) || !project->game_module[0])
        return fail(error, "Project needs a name, saved startup scene and C module.");
    char folder[AF_ASSET_PATH_CAPACITY];
    if (!af_asset_resolve_path(path, "placeholder", folder, sizeof(folder)))
        return fail(error, "Project path too long.");
    char *slash = strrchr(folder, '/');
    if (slash)
        slash[1] = 0;
    size_t n = strlen(folder);
    const char *scene = project->startup_scene, *module = project->game_module;
    if (!strncmp(scene, folder, n))
        scene += n;
    if (!strncmp(module, folder, n))
        module += n;
    cJSON *root = cJSON_CreateObject();
    if (!root)
        return fail(error, "Allocating project JSON.");
    bool ok = cJSON_AddStringToObject(root, "format", "afterlight.project") &&
              cJSON_AddNumberToObject(root, "version", AF_PROJECT_VERSION) &&
              cJSON_AddStringToObject(root, "name", project->name) &&
              cJSON_AddStringToObject(root, "startup_scene", scene) &&
              cJSON_AddStringToObject(root, "game_module", module);
    char *json = ok ? cJSON_Print(root) : NULL;
    cJSON_Delete(root);
    if (!json)
        return fail(error, "Encoding project failed.");
    if (!af_json_valid_text(json, strlen(json))) {
        cJSON_free(json);
        return fail(error, "Project strings must be valid UTF-8.");
    }
    char temporary[AF_ASSET_PATH_CAPACITY];
    static _Atomic unsigned sequence;
    int count = snprintf(temporary, sizeof(temporary), "%s.%llu-%u.tmp", path, (unsigned long long)time(NULL),
                         atomic_fetch_add(&sequence, 1));
    FILE *file = count > 0 && (size_t)count < sizeof(temporary) ? fopen(temporary, "wbx") : NULL;
    ok = file && fwrite(json, 1, strlen(json), file) == strlen(json) && fflush(file) == 0;
    if (file && fclose(file))
        ok = false;
    cJSON_free(json);
#ifdef _WIN32
    if (ok)
        ok = MoveFileExA(temporary, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    if (ok)
        ok = rename(temporary, path) == 0;
#endif
    if (!ok) {
        if (file)
            remove(temporary);
        return fail(error, "Writing/replacing project failed.");
    }
    return true;
}
