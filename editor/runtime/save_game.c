/* SPDX-License-Identifier: MPL-2.0 */
#include "runtime.h"
#include <afterlight/story.h>
#include <SDL3/SDL_timer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif
#define HEADER 224u
#define MAX_STATE (1024u * 1024u)
#define MAX_STORY_BYTES (4u + AF_STORY_MAX_FLAGS * AF_STORY_FLAG_CAPACITY)
static void put32(unsigned char *p, uint32_t v) {
    for (unsigned i = 0; i < 4; ++i)
        p[i] = (unsigned char)(v >> (8 * i));
}
static uint32_t get32(const unsigned char *p) {
    uint32_t v = 0;
    for (unsigned i = 0; i < 4; ++i)
        v |= (uint32_t)p[i] << (8 * i);
    return v;
}
static uint32_t checksum(const unsigned char *p, size_t n) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; ++i) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}
static bool fail(ed_runtime *r, const char *why) {
    if (r)
        snprintf(r->status, sizeof(r->status), "Save game: %s", why);
    return false;
}
static bool scene_name(const char *p) {
    if (!p || !p[0])
        return false;
    for (; *p; ++p)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '-' ||
              *p == '_'))
            return false;
    return true;
}
bool ed_runtime_save_game(ed_runtime *r, const char *path) {
    if (!r || !r->playing || !path || !path[0] || !scene_name(r->current_scene) || !r->api->save_state ||
        strlen(r->api->name) >= 128)
        return fail(r, "this module does not support progress saves.");
    uint32_t authored_hash, authored_bytes;
    if (!r->authored_bytes || !ed_runtime_scene_fingerprint(r, r->current_scene, &authored_hash, &authored_bytes) ||
        authored_hash != r->authored_hash || authored_bytes != r->authored_bytes)
        return fail(r, "authored scene changed or is unavailable; restart before "
                       "saving progress.");
    size_t module_size = r->api->save_state(r->state, NULL, 0);
    if (!module_size || module_size > MAX_STATE)
        return fail(r, "module state exceeds the save limit.");
    const af_story_flags *flags = af_scene_story_flags(r->scene);
    size_t story_size = flags->count ? 4 + flags->count * AF_STORY_FLAG_CAPACITY : 0;
    size_t size = story_size + module_size;
    unsigned char *data = calloc(1, HEADER + size);
    if (!data)
        return fail(r, "cannot allocate state.");
    memcpy(data, story_size ? "AFSAVE3" : "AFSAVE2", 7);
    put32(data + 8, AF_GAME_ABI_VERSION);
    put32(data + 12, r->api->state_version);
    put32(data + 16, (uint32_t)size);
    put32(data + 24, authored_hash);
    put32(data + 28, authored_bytes);
    snprintf((char *)data + 32, 64, "%s", r->current_scene);
    snprintf((char *)data + 96, 128, "%s", r->api->name);
    if (story_size) {
        put32(data + HEADER, (uint32_t)flags->count);
        memcpy(data + HEADER + 4, flags->names, story_size - 4);
    }
    bool ok = r->api->save_state(r->state, data + HEADER + story_size, module_size) == module_size;
    put32(data + 20, checksum(data, HEADER + size));
    char temporary[AF_ASSET_PATH_CAPACITY] = {0};
    int n = snprintf(temporary, sizeof(temporary), "%s.%llu.tmp", path, (unsigned long long)SDL_GetTicksNS());
    FILE *file = ok && n > 0 && (size_t)n < sizeof(temporary) ? fopen(temporary, "wbx") : NULL;
    ok = file && fwrite(data, 1, HEADER + size, file) == HEADER + size && fflush(file) == 0;
    if (file && fclose(file))
        ok = false;
    free(data);
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
        return fail(r, "write failed; previous save retained.");
    }
    snprintf(r->status, sizeof(r->status), "Progress saved. World simulation restarts when resumed.");
    return true;
}
bool ed_runtime_load_game(ed_runtime *r, const char *path) {
    if (!r || !r->playing || !path)
        return fail(r, "a running project and path are required.");
    FILE *f = fopen(path, "rb");
    if (!f)
        return fail(r, "no saved progress found.");
    unsigned char header[HEADER];
    bool ok = fread(header, 1, HEADER, f) == HEADER;
    bool has_story = ok && !memcmp(header, "AFSAVE3\0", 8);
    ok = ok && (has_story || !memcmp(header, "AFSAVE2\0", 8)) && get32(header + 8) == AF_GAME_ABI_VERSION &&
         get32(header + 16) > 0 && get32(header + 16) <= (has_story ? MAX_STATE + MAX_STORY_BYTES : MAX_STATE) &&
         memchr(header + 32, 0, 64) && memchr(header + 96, 0, 128);
    if (ok)
        ok = scene_name((const char *)header + 32) && !strcmp((const char *)header + 96, r->api->name);
    size_t size = ok ? get32(header + 16) : 0;
    unsigned char *data = ok ? malloc(HEADER + size) : NULL;
    if (!data)
        ok = false;
    if (ok) {
        memcpy(data, header, HEADER);
        uint32_t expected = get32(header + 20);
        put32(data + 20, 0);
        ok = fread(data + HEADER, 1, size, f) == size && fgetc(f) == EOF && !ferror(f) &&
             expected == checksum(data, HEADER + size);
    }
    if (fclose(f))
        ok = false;
    if (!ok) {
        free(data);
        return fail(r, "invalid/incompatible progress; current game retained.");
    }
    af_story_flags flags = {0};
    size_t story_size = 0;
    if (has_story) {
        if (size >= 4)
            flags.count = get32(data + HEADER);
        story_size = flags.count <= AF_STORY_MAX_FLAGS ? 4 + flags.count * AF_STORY_FLAG_CAPACITY : size;
        if (!flags.count || story_size >= size || size - story_size > MAX_STATE) {
            free(data);
            return fail(r, "invalid story progress; current game retained.");
        }
        memcpy(flags.names, data + HEADER + 4, story_size - 4);
    }
    uint32_t authored_hash, authored_bytes;
    if (!ed_runtime_scene_fingerprint(r, (const char *)header + 32, &authored_hash, &authored_bytes) ||
        authored_hash != get32(header + 24) || authored_bytes != get32(header + 28)) {
        free(data);
        return fail(r, "authored scene revision differs; current game retained.");
    }
    char scene_path[AF_ASSET_PATH_CAPACITY];
    int n = snprintf(scene_path, sizeof(scene_path), "%s%s.afterlight.json", r->scene_directory, (char *)header + 32);
    af_scene *source = n > 0 && (size_t)n < sizeof(scene_path) ? af_scene_load(scene_path, NULL) : NULL;
    if (!source) {
        free(data);
        return fail(r, "saved scene is unavailable.");
    }
    if (!af_scene_restore_story_flags(source, &flags)) {
        af_scene_destroy(source);
        free(data);
        return fail(r, "invalid story flags; current game retained.");
    }
    af_engine *engine = r->host.engine;
    af_camera old_camera = af_engine_get_camera(engine);
    af_gameplay_status old_status = af_engine_gameplay_status(engine);
    bool captured = af_engine_mouse_captured(engine);
    ed_runtime next = {0};
    snprintf(next.scene_directory, sizeof(next.scene_directory), "%s", r->scene_directory);
    snprintf(next.project_directory, sizeof(next.project_directory), "%s", r->project_directory);
    snprintf(next.current_scene, sizeof(next.current_scene), "%s", (char *)header + 32);
    next.authored_hash = authored_hash;
    next.authored_bytes = authored_bytes;
    af_asset_error error = {0};
    ok = (!engine || af_engine_load_scene_assets(engine, source, &error)) &&
         (r->module_path[0] ? ed_runtime_start_file(&next, source, engine, r->module_path)
                            : ed_runtime_start(&next, source, engine, r->api));
    af_scene_destroy(source);
    if (ok)
        ok = next.api->load_state &&
             next.api->load_state(next.state, &next.host, get32(header + 12), data + HEADER + story_size,
                                  size - story_size);
    free(data);
    if (!ok) {
        ed_runtime_stop(&next);
        if (engine)
            (void)af_engine_load_scene_assets(engine, r->scene, &error);
        af_engine_set_scene(engine, r->scene);
        af_engine_set_camera(engine, &old_camera);
        af_engine_set_gameplay_status(engine, old_status);
        af_engine_set_mouse_capture(engine, captured);
        return fail(r, "restore rejected; current game retained.");
    }
    af_camera camera = af_engine_get_camera(engine);
    af_gameplay_status status = af_engine_gameplay_status(engine);
    next.paused = r->paused;
    next.auto_reload = r->auto_reload;
    next.transition_count = r->transition_count + 1;
    ed_runtime_stop(r);
    *r = next;
    r->host.runtime_context = r;
    r->host.project_directory = r->project_directory;
    af_engine_set_scene(engine, r->scene);
    af_engine_set_camera(engine, &camera);
    af_engine_set_gameplay_status(engine, status);
    af_engine_set_mouse_capture(engine, captured);
    af_engine_reset_interpolation(engine);
    snprintf(r->status, sizeof(r->status), "Saved progress restored; world simulation restarted.");
    return true;
}
