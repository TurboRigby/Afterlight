/* SPDX-License-Identifier: MPL-2.0 */
#include "cluster.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct triangle_order {
    uint32_t key, triangle;
} triangle_order;

static const char magic[8] = "AFGEOM1";

_Static_assert(sizeof(af_vertex) == 72, "Packed vertex layout");
_Static_assert(sizeof(af_cluster) == 32, "Packed cluster layout");

static bool little_endian(void) {
    uint32_t one = 1;
    return *(const unsigned char *)&one == 1;
}

static bool valid_sizes(uint32_t vertices, uint32_t indices, uint32_t clusters) {
    return vertices && indices && indices % 3 == 0 && clusters && clusters <= indices / 3 &&
           (uint64_t)vertices * sizeof(af_vertex) + (uint64_t)indices * sizeof(uint32_t) +
                   (uint64_t)clusters * sizeof(af_cluster) <=
               AF_ASSET_FILE_MAX_BYTES;
}

static bool valid_position(af_vec3 p) {
    return isfinite(p.x) && isfinite(p.y) && isfinite(p.z);
}

static af_vec3 minimum(af_vec3 a, af_vec3 b) {
    return (af_vec3){fminf(a.x, b.x), fminf(a.y, b.y), fminf(a.z, b.z)};
}

static af_vec3 maximum(af_vec3 a, af_vec3 b) {
    return (af_vec3){fmaxf(a.x, b.x), fmaxf(a.y, b.y), fmaxf(a.z, b.z)};
}

static uint32_t quantize(double v, double lo, double hi) {
    return hi > lo ? (uint32_t)(1023 * fmax(0, fmin(1, (v - lo) / (hi - lo)))) : 0;
}

static uint32_t morton(const af_vertex *a, const af_vertex *b, const af_vertex *c, af_vec3 lo, af_vec3 hi) {
    uint32_t x = quantize(((double)a->position.x + b->position.x + c->position.x) / 3, lo.x, hi.x);
    uint32_t y = quantize(((double)a->position.y + b->position.y + c->position.y) / 3, lo.y, hi.y);
    uint32_t z = quantize(((double)a->position.z + b->position.z + c->position.z) / 3, lo.z, hi.z);
    uint32_t result = 0;
    for (unsigned bit = 0; bit < 10; ++bit) {
        result |= ((x >> bit) & 1u) << (3 * bit);
        result |= ((y >> bit) & 1u) << (3 * bit + 1);
        result |= ((z >> bit) & 1u) << (3 * bit + 2);
    }
    return result;
}

static int compare_triangle(const void *lhs, const void *rhs) {
    const triangle_order *a = lhs, *b = rhs;
    if (a->key != b->key)
        return a->key < b->key ? -1 : 1;
    return (a->triangle > b->triangle) - (a->triangle < b->triangle);
}

static bool allocate_mesh(af_cluster_mesh *out, uint32_t vertices, uint32_t indices, uint32_t clusters) {
    out->mesh = (af_mesh_data){malloc((size_t)vertices * sizeof(af_vertex)), vertices,
                               malloc((size_t)indices * sizeof(uint32_t)), indices};
    out->clusters = malloc((size_t)clusters * sizeof(*out->clusters));
    out->count = clusters;
    return out->mesh.vertices && out->mesh.indices && out->clusters;
}

void af_cluster_free(af_cluster_mesh *m) {
    free((void *)m->mesh.vertices);
    free((void *)m->mesh.indices);
    free(m->clusters);
    *m = (af_cluster_mesh){0};
}

static bool valid_vertices(const af_mesh_data *mesh) {
    for (uint32_t i = 0; i < mesh->vertex_count; ++i) {
        const af_vertex *v = &mesh->vertices[i];
        if (!valid_position(v->position) || v->weights.x != 0 || v->weights.y != 0 || v->weights.z != 0 ||
            v->weights.w != 0)
            return false;
    }
    return true;
}

bool af_cluster_build(const af_mesh_data *source, af_cluster_mesh *out) {
    *out = (af_cluster_mesh){0};
    if (!source || !source->vertices || !source->indices)
        return false;
    uint32_t triangles = source->index_count / 3;
    uint32_t count = (triangles + AF_CLUSTER_TRIANGLES - 1) / AF_CLUSTER_TRIANGLES;
    if (!valid_sizes(source->vertex_count, source->index_count, count) || !valid_vertices(source))
        return false;
    for (uint32_t i = 0; i < source->index_count; ++i)
        if (source->indices[i] >= source->vertex_count)
            return false;
    af_vec3 lo = source->vertices[0].position, hi = lo;
    for (uint32_t i = 1; i < source->vertex_count; ++i) {
        lo = minimum(lo, source->vertices[i].position);
        hi = maximum(hi, source->vertices[i].position);
    }
    triangle_order *order = malloc((size_t)triangles * sizeof(*order));
    if (!order || !allocate_mesh(out, source->vertex_count, source->index_count, count)) {
        free(order);
        af_cluster_free(out);
        return false;
    }
    for (uint32_t i = 0; i < triangles; ++i) {
        const uint32_t *tri = source->indices + 3 * i;
        order[i] = (triangle_order){
            morton(&source->vertices[tri[0]], &source->vertices[tri[1]], &source->vertices[tri[2]], lo, hi), i};
    }
    qsort(order, triangles, sizeof(*order), compare_triangle);
    memcpy((void *)out->mesh.vertices, source->vertices, (size_t)source->vertex_count * sizeof(af_vertex));
    uint32_t *indices = (uint32_t *)out->mesh.indices;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t first = i * AF_CLUSTER_TRIANGLES;
        uint32_t size = triangles - first;
        if (size > AF_CLUSTER_TRIANGLES)
            size = AF_CLUSTER_TRIANGLES;
        af_vec3 lower = {INFINITY, INFINITY, INFINITY}, upper = {-INFINITY, -INFINITY, -INFINITY};
        for (uint32_t t = first; t < first + size; ++t) {
            const uint32_t *tri = source->indices + 3 * order[t].triangle;
            for (unsigned j = 0; j < 3; ++j) {
                af_vec3 p = source->vertices[tri[j]].position;
                lower = minimum(lower, p);
                upper = maximum(upper, p);
                indices[3 * t + j] = tri[j];
            }
        }
        out->clusters[i] = (af_cluster){lower, first * 3, upper, size * 3};
    }
    free(order);
    return true;
}

bool af_cluster_save(const char *path, const af_cluster_mesh *m) {
    if (!little_endian())
        return false;
    FILE *f = fopen(path, "wbx");
    if (!f)
        return false;
    uint32_t sizes[] = {m->mesh.vertex_count, m->mesh.index_count, m->count};
    bool ok = fwrite(magic, 1, sizeof(magic), f) == sizeof(magic) && fwrite(sizes, sizeof(*sizes), 3, f) == 3 &&
              fwrite(m->clusters, sizeof(*m->clusters), m->count, f) == m->count &&
              fwrite(m->mesh.vertices, sizeof(af_vertex), sizes[0], f) == sizes[0] &&
              fwrite(m->mesh.indices, sizeof(uint32_t), sizes[1], f) == sizes[1];
    if (fclose(f))
        ok = false;
    if (!ok)
        remove(path);
    return ok;
}

static bool validate_clusters(af_cluster_mesh *m) {
    if (!valid_vertices(&m->mesh))
        return false;
    uint32_t end = 0;
    for (uint32_t i = 0; i < m->count; ++i) {
        af_cluster *c = &m->clusters[i];
        if (c->first_index != end || !c->index_count || c->index_count % 3 ||
            c->index_count > AF_CLUSTER_TRIANGLES * 3 || c->index_count > m->mesh.index_count - end)
            return false;
        c->minimum = (af_vec3){INFINITY, INFINITY, INFINITY};
        c->maximum = (af_vec3){-INFINITY, -INFINITY, -INFINITY};
        for (uint32_t j = end; j < end + c->index_count; ++j) {
            uint32_t vertex = m->mesh.indices[j];
            if (vertex >= m->mesh.vertex_count)
                return false;
            c->minimum = minimum(c->minimum, m->mesh.vertices[vertex].position);
            c->maximum = maximum(c->maximum, m->mesh.vertices[vertex].position);
        }
        end += c->index_count;
    }
    return end == m->mesh.index_count;
}

bool af_cluster_load(const char *path, af_cluster_mesh *out) {
    *out = (af_cluster_mesh){0};
    FILE *f = path && little_endian() ? fopen(path, "rb") : NULL;
    if (!f)
        return false;
    char tag[sizeof(magic)];
    uint32_t sizes[3];
    bool ok = fread(tag, 1, sizeof(tag), f) == sizeof(tag) && !memcmp(tag, magic, sizeof(tag)) &&
              fread(sizes, sizeof(*sizes), 3, f) == 3 && valid_sizes(sizes[0], sizes[1], sizes[2]) &&
              allocate_mesh(out, sizes[0], sizes[1], sizes[2]) &&
              fread(out->clusters, sizeof(af_cluster), sizes[2], f) == sizes[2] &&
              fread((void *)out->mesh.vertices, sizeof(af_vertex), sizes[0], f) == sizes[0] &&
              fread((void *)out->mesh.indices, sizeof(uint32_t), sizes[1], f) == sizes[1] && fgetc(f) == EOF &&
              !ferror(f) && validate_clusters(out);
    fclose(f);
    if (!ok)
        af_cluster_free(out);
    return ok;
}
