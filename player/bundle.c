/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE 1
#endif
#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE 1
#endif

#include "bundle.h"
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define af_mkdir(p) _mkdir(p)
#else
#include <dirent.h>
#include <unistd.h>
#define af_mkdir(p) mkdir(p, 0755)
#endif

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <mach-o/getsect.h>
#include <mach-o/ldsyms.h>
#endif

__attribute__((weak)) const unsigned char af_embedded_bundle[1] = {0};
__attribute__((weak)) const size_t af_embedded_bundle_size = 0;

static char s_bundle_temp_dir[1024] = {0};
static bool s_cleanup_registered = false;

static void rm_rf(const char *path) {
#ifdef _WIN32
    char pattern[1024];
    snprintf(pattern, sizeof(pattern), "%s\\*", path);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (strcmp(fd.cFileName, ".") && strcmp(fd.cFileName, "..")) {
                char item[1024];
                snprintf(item, sizeof(item), "%s\\%s", path, fd.cFileName);
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                    rm_rf(item);
                else
                    remove(item);
            }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    _rmdir(path);
#else
    DIR *d = opendir(path);
    if (!d)
        return;
    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
            continue;
        char item[1024];
        snprintf(item, sizeof(item), "%s/%s", path, entry->d_name);
        struct stat st;
        if (stat(item, &st) == 0) {
            if (S_ISDIR(st.st_mode))
                rm_rf(item);
            else
                remove(item);
        }
    }
    closedir(d);
    rmdir(path);
#endif
}

void af_bundle_cleanup(void) {
    if (s_bundle_temp_dir[0]) {
        rm_rf(s_bundle_temp_dir);
        s_bundle_temp_dir[0] = 0;
    }
}

static bool make_parent_dirs(char *path) {
    for (char *p = path + 1; *p; ++p) {
        if (*p == '/' || *p == '\\') {
            char old = *p;
            *p = 0;
            af_mkdir(path);
            *p = old;
        }
    }
    return true;
}

static bool extract_entries(const uint8_t *data, size_t size, uint64_t file_count, const char *target_dir) {
    size_t offset = 0;
    for (uint64_t i = 0; i < file_count; ++i) {
        if (offset + 2 > size)
            return false;
        uint16_t path_len = (uint16_t)((uint16_t)data[offset] | (uint16_t)((uint16_t)data[offset + 1] << 8));
        offset += 2;
        if (offset + path_len + 8 + 4 > size || path_len == 0 || path_len >= 1024)
            return false;

        char rel[1024];
        memcpy(rel, data + offset, path_len);
        rel[path_len] = 0;
        offset += path_len;

        /* Prevent directory traversal */
        if (strstr(rel, "..") || rel[0] == '/' || rel[0] == '\\')
            return false;

        uint64_t file_size = 0;
        for (size_t b = 0; b < 8; ++b)
            file_size |= ((uint64_t)data[offset + b]) << (b * 8);
        offset += 8;

        uint32_t file_mode = 0;
        for (size_t b = 0; b < 4; ++b)
            file_mode |= ((uint32_t)data[offset + b]) << (b * 8);
        offset += 4;

        if (offset + file_size > size)
            return false;

        char full_path[1024];
        snprintf(full_path, sizeof(full_path), "%s/%s", target_dir, rel);
        make_parent_dirs(full_path);

        FILE *out = fopen(full_path, "wb");
        if (!out)
            return false;
        if (file_size > 0 && fwrite(data + offset, 1, (size_t)file_size, out) != (size_t)file_size) {
            fclose(out);
            return false;
        }
        fclose(out);
#ifndef _WIN32
        chmod(full_path, file_mode ? (mode_t)file_mode : 0644);
#endif
        offset += (size_t)file_size;
    }
    return true;
}

static bool get_executable_path(char *out, size_t capacity) {
#if defined(__APPLE__)
    uint32_t size = (uint32_t)capacity;
    if (_NSGetExecutablePath(out, &size) == 0) {
        char real[PATH_MAX];
        if (realpath(out, real)) {
            snprintf(out, capacity, "%s", real);
            return true;
        }
        return true;
    }
#elif defined(_WIN32)
    if (GetModuleFileNameA(NULL, out, (DWORD)capacity))
        return true;
#else
    ssize_t len = readlink("/proc/self/exe", out, capacity - 1);
    if (len > 0) {
        out[len] = '\0';
        return true;
    }
#endif
    return false;
}

bool af_bundle_extract(char *out_dir, size_t out_dir_capacity) {
    const uint8_t *bundle_bytes = NULL;
    size_t bundle_size = 0;
    uint8_t *allocated = NULL;

#if defined(__APPLE__)
    unsigned long sec_size = 0;
    const uint8_t *sec_data = getsectiondata(&_mh_execute_header, "__BUNDLE", "__bundle", &sec_size);
    if (!sec_data || sec_size < 16)
        sec_data = getsectiondata(&_mh_execute_header, "__DATA", "__bundle", &sec_size);
    if (sec_data && sec_size >= 16) {
        bundle_bytes = sec_data;
        bundle_size = sec_size;
    }
#endif

    if (!bundle_bytes && af_embedded_bundle_size >= 16) {
        bundle_bytes = af_embedded_bundle;
        bundle_size = af_embedded_bundle_size;
    }

    if (!bundle_bytes) {
        /* Check for trailing appended bundle */
        char exec_path[1024];
        if (get_executable_path(exec_path, sizeof(exec_path))) {
            FILE *f = fopen(exec_path, "rb");
            if (f) {
                if (fseek(f, -32, SEEK_END) == 0) {
                    uint8_t footer[32];
                    if (fread(footer, 1, 32, f) == 32) {
                        uint64_t magic1 = 0, magic2 = 0, offset = 0;
                        for (size_t i = 0; i < 8; ++i) {
                            magic1 |= ((uint64_t)footer[i]) << (i * 8);
                            magic2 |= ((uint64_t)footer[8 + i]) << (i * 8);
                            offset |= ((uint64_t)footer[16 + i]) << (i * 8);
                        }
                        if (magic1 == AF_BUNDLE_FOOTER_MAGIC1 && magic2 == AF_BUNDLE_FOOTER_MAGIC2 &&
                            offset < (uint64_t)ftell(f)) {
                            long cur_end = ftell(f);
                            if (fseek(f, (long)offset, SEEK_SET) == 0) {
                                size_t payload_len = (size_t)(cur_end - 32 - (long)offset);
                                allocated = malloc(payload_len);
                                if (allocated && fread(allocated, 1, payload_len, f) == payload_len) {
                                    bundle_bytes = allocated;
                                    bundle_size = payload_len;
                                }
                            }
                        }
                    }
                }
                fclose(f);
            }
        }
    }

    if (!bundle_bytes || bundle_size < 16) {
        free(allocated);
        return false;
    }

    /* Parse header */
    uint64_t header_magic = 0, file_count = 0;
    for (size_t i = 0; i < 8; ++i) {
        header_magic |= ((uint64_t)bundle_bytes[i]) << (i * 8);
        file_count |= ((uint64_t)bundle_bytes[8 + i]) << (i * 8);
    }
    if (header_magic != AF_BUNDLE_HEADER_MAGIC || file_count == 0) {
        free(allocated);
        return false;
    }

    /* Create temporary directory */
    char tmpl[1024];
#ifdef _WIN32
    char tmp_base[MAX_PATH];
    GetTempPathA(sizeof(tmp_base), tmp_base);
    snprintf(tmpl, sizeof(tmpl), "%safterlight_game_%u", tmp_base, (unsigned)GetCurrentProcessId());
    _mkdir(tmpl);
#else
    const char *tmp_base = getenv("TMPDIR");
    if (!tmp_base || !tmp_base[0])
        tmp_base = "/tmp";
    snprintf(tmpl, sizeof(tmpl), "%s/afterlight_standalone_XXXXXX", tmp_base);
    if (!mkdtemp(tmpl)) {
        free(allocated);
        return false;
    }
#endif

    bool ok = extract_entries(bundle_bytes + 16, bundle_size - 16, file_count, tmpl);
    free(allocated);

    if (!ok) {
        rm_rf(tmpl);
        return false;
    }

    snprintf(s_bundle_temp_dir, sizeof(s_bundle_temp_dir), "%s", tmpl);
    if (!s_cleanup_registered) {
        atexit(af_bundle_cleanup);
        s_cleanup_registered = true;
    }
    snprintf(out_dir, out_dir_capacity, "%s", tmpl);
    return true;
}
