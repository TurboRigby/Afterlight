/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <afterlight/asset.h>
#include <afterlight/model.h>
#include "morph.h"
#include "stb_image.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#define af_getcwd _getcwd
#else
#include <unistd.h>
#define af_getcwd getcwd
#endif
#define MAX_VERTICES 1048576u
static bool failure(af_asset_error *e, const char *text) {
    if (e)
        snprintf(e->message, sizeof(e->message), "%s", text);
    return false;
}
static bool absolute(const char *p) {
    return p[0] == '/' || p[0] == '\\' || (p[0] && p[1] == ':');
}
bool af_asset_resolve_path(const char *scene_path, const char *ref, char *out, size_t capacity) {
    if (!ref || !ref[0] || !out || !capacity)
        return false;
    char path[AF_ASSET_PATH_CAPACITY], base[AF_ASSET_PATH_CAPACITY];
    if (absolute(ref))
        return snprintf(out, capacity, "%s", ref) >= 0 && strlen(ref) < capacity;
    if (scene_path && scene_path[0]) {
        if (absolute(scene_path)) {
            if (snprintf(base, sizeof(base), "%s", scene_path) >= (int)sizeof(base))
                return false;
        } else {
            char cwd[AF_ASSET_PATH_CAPACITY];
            if (!af_getcwd(cwd, sizeof(cwd)) ||
                snprintf(base, sizeof(base), "%s/%s", cwd, scene_path) >= (int)sizeof(base))
                return false;
        }
        char *last = NULL;
        for (char *p = base; *p; ++p)
            if (*p == '/' || *p == '\\')
                last = p;
        if (last)
            last[1] = 0;
        else
            base[0] = 0;
    } else {
        if (!af_getcwd(base, sizeof(base)))
            return false;
        size_t n = strlen(base);
        if (n + 2 > sizeof(base))
            return false;
        base[n] = '/';
        base[n + 1] = 0;
    }
    int n = snprintf(path, sizeof(path), "%s%s", base, ref);
    if (n < 0 || (size_t)n >= sizeof(path) || (size_t)n >= capacity)
        return false;
    memcpy(out, path, (size_t)n + 1);
    return true;
}
static unsigned char *read_file(const char *path, size_t *length, af_asset_error *error) {
    FILE *file = path ? fopen(path, "rb") : NULL;
    if (!file) {
        failure(error, "Cannot open asset file.");
        return NULL;
    }
    unsigned char *bytes = NULL;
    long size;
    if (fseek(file, 0, SEEK_END) || (size = ftell(file)) < 0 || (unsigned long)size > AF_ASSET_FILE_MAX_BYTES ||
        fseek(file, 0, SEEK_SET))
        failure(error, "Asset exceeds file limit or cannot be read.");
    else if (!(bytes = malloc((size_t)size + 1)))
        failure(error, "Not enough memory for asset.");
    else if (fread(bytes, 1, (size_t)size, file) != (size_t)size || ferror(file)) {
        free(bytes);
        bytes = NULL;
        failure(error, "Reading complete asset failed.");
    } else {
        bytes[size] = 0;
        *length = (size_t)size;
    }
    if (fclose(file)) {
        free(bytes);
        bytes = NULL;
        failure(error, "Closing asset failed.");
    }
    return bytes;
}
void af_image_file_free(af_image_file *image) {
    if (image) {
        stbi_image_free(image->rgba);
        *image = (af_image_file){0};
    }
}
bool af_image_file_load(const char *path, af_image_file *out, af_asset_error *error) {
    if (path && strstr(path, "#image="))
        return af_model_image_load(path, out, error);
    if (error)
        *error = (af_asset_error){0};
    if (!out)
        return failure(error, "An image output is required.");
    *out = (af_image_file){0};
    size_t length = 0;
    unsigned char *bytes = read_file(path, &length, error);
    if (!bytes)
        return false;
    int w = 0, h = 0, channels = 0;
    bool valid = length <= INT_MAX && !stbi_is_hdr_from_memory(bytes, (int)length) &&
                 stbi_info_from_memory(bytes, (int)length, &w, &h, &channels) && w > 0 && h > 0 && w <= 8192 &&
                 h <= 8192;
    unsigned char *rgba = valid ? stbi_load_from_memory(bytes, (int)length, &w, &h, &channels, 4) : NULL;
    free(bytes);
    if (!rgba)
        return failure(error, "Unsupported or invalid image (PNG/JPEG/BMP/TGA; maximum 8192 square).");
    *out = (af_image_file){rgba, (uint32_t)w, (uint32_t)h};
    return true;
}
static bool hdr_complete(const unsigned char *bytes, size_t length, unsigned width, unsigned height) {
    size_t at = 0;
    bool blank = false;
    while (at < length && at < 16384) {
        const unsigned char *newline = memchr(bytes + at, '\n', length - at);
        if (!newline)
            return false;
        size_t end = (size_t)(newline - bytes);
        size_t line = end - at;
        if (line && bytes[end - 1] == '\r')
            --line;
        at = end + 1;
        if (!line) {
            blank = true;
            break;
        }
    }
    if (!blank || at >= length)
        return false;
    const unsigned char *newline = memchr(bytes + at, '\n', length - at);
    if (!newline)
        return false;
    at = (size_t)(newline - bytes) + 1;
    if (width < 8 || width > 32767 || length - at < 4 || bytes[at] != 2 || bytes[at + 1] != 2 || bytes[at + 2] & 128)
        return length - at >= (size_t)width * height * 4;
    for (unsigned y = 0; y < height; ++y) {
        if (length - at < 4 || bytes[at] != 2 || bytes[at + 1] != 2 ||
            ((unsigned)bytes[at + 2] << 8 | bytes[at + 3]) != width)
            return false;
        at += 4;
        for (unsigned channel = 0; channel < 4; ++channel) {
            unsigned decoded = 0;
            while (decoded < width) {
                if (at >= length)
                    return false;
                unsigned code = bytes[at++], count = code > 128 ? code - 128 : code;
                if (!count || count > width - decoded)
                    return false;
                size_t needed = code > 128 ? 1 : count;
                if (length - at < needed)
                    return false;
                at += needed;
                decoded += count;
            }
        }
    }
    return true;
}
void af_hdr_file_free(af_hdr_file *image) {
    if (image) {
        stbi_image_free(image->rgba);
        *image = (af_hdr_file){0};
    }
}
bool af_hdr_file_load(const char *path, af_hdr_file *out, af_asset_error *error) {
    if (error)
        *error = (af_asset_error){0};
    if (!out)
        return failure(error, "An HDR output is required.");
    *out = (af_hdr_file){0};
    size_t length = 0;
    unsigned char *bytes = read_file(path, &length, error);
    if (!bytes)
        return false;
    int width = 0, height = 0, channels = 0;
    bool valid = length <= INT_MAX && stbi_is_hdr_from_memory(bytes, (int)length) &&
                 stbi_info_from_memory(bytes, (int)length, &width, &height, &channels) && width >= 2 && height >= 1 &&
                 width <= 4096 && height <= 2048 && (uint64_t)width * (uint64_t)height * 16 <= 128u * 1024u * 1024u;
    valid = valid && hdr_complete(bytes, length, (unsigned)width, (unsigned)height);
    float *rgba = valid ? stbi_loadf_from_memory(bytes, (int)length, &width, &height, &channels, 4) : NULL;
    free(bytes);
    if (!rgba)
        return failure(error, "Invalid Radiance HDR panorama (maximum 4096 x 2048 / 128 MiB).");
    for (size_t i = 0; i < (size_t)width * (size_t)height * 4; ++i)
        if (!isfinite(rgba[i]) || rgba[i] < 0 || rgba[i] > 65504) {
            stbi_image_free(rgba);
            return failure(error, "HDR radiance must be finite and within [0,65504].");
        }
    *out = (af_hdr_file){rgba, (uint32_t)width, (uint32_t)height};
    return true;
}
void af_mesh_morph_free(af_mesh_morph *morph) {
    if (morph) {
        free(morph->source_vertices);
        free(morph->targets);
        free(morph);
    }
}
void af_mesh_morph_blend(const af_mesh_morph *morph, const float *weights, bool relative, af_morph_delta *out) {
    memset(out, 0, morph->source_count * sizeof(*out));
    for (uint32_t i = 0; i < morph->target_count; ++i) {
        float weight = weights[i] - (relative ? morph->defaults[i] : 0.0f);
        if (weight == 0.0f || !(morph->used_targets & (UINT64_C(1) << i)))
            continue;
        const af_morph_delta *target = morph->targets + (size_t)i * morph->source_count;
        for (uint32_t j = 0; j < morph->source_count; ++j) {
            out[j].position = af_vec3_add(out[j].position, af_vec3_scale(target[j].position, weight));
            out[j].normal = af_vec3_add(out[j].normal, af_vec3_scale(target[j].normal, weight));
        }
    }
}
void af_mesh_file_free(af_mesh_file *mesh) {
    if (mesh) {
        free((void *)mesh->data.vertices);
        free((void *)mesh->data.indices);
        af_mesh_morph_free(mesh->morph);
        *mesh = (af_mesh_file){0};
    }
}
static bool grow(void **p, size_t *capacity, size_t count, size_t size) {
    if (count <= *capacity)
        return true;
    size_t n = *capacity ? *capacity * 2 : 256;
    if (n < count)
        n = count;
    if (n > MAX_VERTICES)
        n = MAX_VERTICES;
    if (n < count)
        return false;
    void *next = realloc(*p, n * size);
    if (!next)
        return false;
    *p = next;
    *capacity = n;
    return true;
}
static char *word(char **cursor) {
    char *p = *cursor;
    while (isspace((unsigned char)*p))
        ++p;
    if (!*p || *p == '#') {
        *cursor = p;
        return NULL;
    }
    char *start = p;
    while (*p && !isspace((unsigned char)*p) && *p != '#')
        ++p;
    if (*p == '#') {
        *p = 0;
        *cursor = p;
    } else if (*p) {
        *p++ = 0;
        *cursor = p;
    } else
        *cursor = p;
    return start;
}
static bool number(char **cursor, float *out) {
    char *token = word(cursor), *end = NULL;
    if (!token)
        return false;
    errno = 0;
    float value = strtof(token, &end);
    if (*end || errno || !isfinite(value))
        return false;
    *out = value;
    return true;
}
static bool obj_index(char **p, size_t count, int *index) {
    char *end;
    errno = 0;
    long n = strtol(*p, &end, 10);
    if (end == *p || errno || n == 0 || n > INT_MAX || n < -INT_MAX)
        return false;
    long resolved = n > 0 ? n - 1 : (long)count + n;
    if (resolved < 0 || (size_t)resolved >= count)
        return false;
    *index = (int)resolved;
    *p = end;
    return true;
}
static bool corner(char *text, af_vec3 *positions, size_t np, af_vec2 *uvs, size_t nu, af_vec3 *normals, size_t nn,
                   af_vertex *out) {
    int p, t = -1, n = -1;
    if (!obj_index(&text, np, &p))
        return false;
    if (*text == '/') {
        ++text;
        if (*text != '/') {
            if (!obj_index(&text, nu, &t))
                return false;
        }
        if (*text == '/') {
            ++text;
            if (!obj_index(&text, nn, &n))
                return false;
        }
    }
    if (*text)
        return false;
    *out = (af_vertex){.position = positions[p], .color = {1, 1, 1, 1}};
    if (t >= 0)
        out->uv = uvs[t];
    if (n >= 0)
        out->normal = af_vec3_normalize(normals[n]);
    return true;
}
bool af_mesh_file_load_obj(const char *path, af_mesh_file *out, af_asset_error *error) {
    if (error)
        *error = (af_asset_error){0};
    if (!out)
        return failure(error, "A mesh output is required.");
    *out = (af_mesh_file){0};
    size_t length = 0;
    unsigned char *bytes = read_file(path, &length, error);
    if (!bytes)
        return false;
    if (memchr(bytes, 0, length)) {
        free(bytes);
        return failure(error, "OBJ must be text without embedded NUL.");
    }
    af_vec3 *positions = NULL, *normals = NULL;
    af_vec2 *uvs = NULL;
    af_vertex *vertices = NULL;
    uint32_t *indices = NULL;
    size_t np = 0, nn = 0, nu = 0, nv = 0, cp = 0, cn = 0, cu = 0, cv = 0;
    bool success = false;
    unsigned line_number = 0;
    char *line = (char *)bytes;
    while (*line) {
        char *next = strchr(line, '\n');
        if (next)
            *next++ = 0;
        else
            next = line + strlen(line);
        ++line_number;
        char *cursor = line, *kind = word(&cursor);
        if (!kind) {
            line = next;
            continue;
        }
        if (!strcmp(kind, "v") || !strcmp(kind, "vn")) {
            af_vec3 v;
            if (!number(&cursor, &v.x) || !number(&cursor, &v.y) || !number(&cursor, &v.z))
                goto done;
            if (kind[1] == 'n') {
                if (!grow((void **)&normals, &cn, nn + 1, sizeof(*normals)))
                    goto done;
                normals[nn++] = v;
            } else {
                if (!grow((void **)&positions, &cp, np + 1, sizeof(*positions)))
                    goto done;
                positions[np++] = v;
            }
        } else if (!strcmp(kind, "vt")) {
            af_vec2 uv;
            if (!number(&cursor, &uv.x) || !number(&cursor, &uv.y) || !grow((void **)&uvs, &cu, nu + 1, sizeof(*uvs)))
                goto done;
            uv.y = 1 - uv.y;
            uvs[nu++] = uv;
        } else if (!strcmp(kind, "f")) {
            af_vertex face[1024];
            size_t count = 0;
            char *token;
            while ((token = word(&cursor))) {
                if (count >= 1024 || !corner(token, positions, np, uvs, nu, normals, nn, &face[count]))
                    goto done;
                ++count;
            }
            if (count < 3 || !grow((void **)&vertices, &cv, nv + (count - 2) * 3, sizeof(*vertices)))
                goto done;
            for (size_t i = 1; i + 1 < count; ++i) {
                af_vertex triangle[] = {face[0], face[i], face[i + 1]};
                af_vec3 normal =
                    af_vec3_normalize(af_vec3_cross(af_vec3_sub(triangle[1].position, triangle[0].position),
                                                    af_vec3_sub(triangle[2].position, triangle[0].position)));
                for (int j = 0; j < 3; ++j) {
                    if (af_vec3_length(triangle[j].normal) == 0)
                        triangle[j].normal = normal;
                    vertices[nv++] = triangle[j];
                }
            }
        }
        line = next;
    }
    if (!nv)
        goto done;
    indices = malloc(nv * sizeof(*indices));
    if (!indices)
        goto done;
    for (size_t i = 0; i < nv; ++i)
        indices[i] = (uint32_t)i;
    out->data = (af_mesh_data){vertices, (uint32_t)nv, indices, (uint32_t)nv};
    vertices = NULL;
    indices = NULL;
    success = true;
done:
    if (!success && error)
        snprintf(error->message, sizeof(error->message), "Invalid/oversized OBJ or allocation failure near line %u.",
                 line_number);
    free(bytes);
    free(positions);
    free(normals);
    free(uvs);
    free(vertices);
    free(indices);
    return success;
}
