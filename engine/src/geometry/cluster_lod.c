/* SPDX-License-Identifier: MPL-2.0 */
#include "cluster.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define REGION_INDICES (AF_CLUSTER_REGION_LEAVES * AF_CLUSTER_TRIANGLES * 3)

typedef struct edge {
    uint32_t a, b;
} edge;
typedef struct collapse {
    edge vertices;
    float length;
} collapse;
typedef struct region_work {
    uint32_t indices[REGION_INDICES], global[REGION_INDICES], count, vertex_count;
    uint32_t head[REGION_INDICES], next[REGION_INDICES], marks[REGION_INDICES], remap[REGION_INDICES];
    bool locked[REGION_INDICES], touched[REGION_INDICES];
    uint32_t representative[REGION_INDICES];
    edge edges[REGION_INDICES];
    collapse candidates[REGION_INDICES];
} region_work;

static int edge_compare(const void *lhs, const void *rhs) {
    const edge *a = lhs, *b = rhs;
    if (a->a != b->a)
        return a->a < b->a ? -1 : 1;
    return (a->b > b->b) - (a->b < b->b);
}

static int collapse_compare(const void *lhs, const void *rhs) {
    const collapse *a = lhs, *b = rhs;
    if (a->length != b->length)
        return a->length < b->length ? -1 : 1;
    return edge_compare(&a->vertices, &b->vertices);
}

static void build_adjacency(region_work *w) {
    for (uint32_t i = 0; i < w->vertex_count; ++i)
        w->head[i] = UINT32_MAX;
    for (uint32_t i = 0; i < w->count; ++i) {
        uint32_t a = w->indices[i], b = w->indices[i % 3 == 2 ? i - 2 : i + 1];
        w->next[i] = w->head[a];
        w->head[a] = i;
        w->edges[i] = a < b ? (edge){a, b} : (edge){b, a};
    }
    qsort(w->edges, w->count, sizeof(*w->edges), edge_compare);
}

static uint32_t edge_run(const region_work *w, uint32_t first) {
    uint32_t end = first + 1;
    while (end < w->count && !edge_compare(&w->edges[first], &w->edges[end]))
        ++end;
    return end;
}

static af_vec3 position(const af_cluster_mesh *mesh, const region_work *w, uint32_t vertex) {
    return mesh->mesh.vertices[w->global[vertex]].position;
}

/* The link condition prevents joining unrelated fans. Check triangle orientation
   before replacing the vertex so the collapse does not fold a surviving face. */
static bool can_collapse(const af_cluster_mesh *mesh, region_work *w, edge e, uint32_t tag) {
    for (uint32_t i = w->head[e.b]; i != UINT32_MAX; i = w->next[i]) {
        uint32_t first = i - i % 3;
        for (uint32_t j = first; j < first + 3; ++j)
            w->marks[w->indices[j]] = tag;
    }
    uint32_t shared = 0;
    for (uint32_t i = w->head[e.a]; i != UINT32_MAX; i = w->next[i]) {
        uint32_t first = i - i % 3;
        uint32_t b = w->indices[first + (i % 3 + 1) % 3];
        uint32_t c = w->indices[first + (i % 3 + 2) % 3];
        uint32_t neighbors[] = {b, c};
        for (unsigned j = 0; j < 2; ++j) {
            uint32_t v = neighbors[j];
            if (v != e.b && w->marks[v] == tag) {
                ++shared;
                w->marks[v] = 0;
            }
        }
        if (b == e.b || c == e.b)
            continue;
        for (uint32_t j = w->head[e.b]; j != UINT32_MAX; j = w->next[j]) {
            uint32_t other = j - j % 3;
            uint32_t u = w->indices[other + (j % 3 + 1) % 3];
            uint32_t v = w->indices[other + (j % 3 + 2) % 3];
            if ((b == u && c == v) || (b == v && c == u))
                return false;
        }
        af_vec3 p = position(mesh, w, e.a), q = position(mesh, w, e.b);
        af_vec3 u = position(mesh, w, b), v = position(mesh, w, c);
        af_vec3 old = af_vec3_cross(af_vec3_sub(u, p), af_vec3_sub(v, p));
        af_vec3 next = af_vec3_cross(af_vec3_sub(u, q), af_vec3_sub(v, q));
        if (!(af_vec3_dot(old, next) > 0))
            return false;
    }
    return shared == 2;
}

static void protect_neighbors(region_work *w, uint32_t vertex) {
    for (uint32_t i = w->head[vertex]; i != UINT32_MAX; i = w->next[i]) {
        uint32_t first = i - i % 3;
        for (uint32_t j = first; j < first + 3; ++j)
            w->touched[w->indices[j]] = true;
    }
}

static uint32_t simplify(const af_cluster_mesh *mesh, region_work *w, float *error) {
    build_adjacency(w);
    for (uint32_t first = 0; first < w->count;) {
        uint32_t end = edge_run(w, first);
        if (end - first != 2)
            w->locked[w->edges[first].a] = w->locked[w->edges[first].b] = true;
        first = end;
    }
    for (uint32_t i = 0; i < w->vertex_count; ++i)
        w->representative[i] = i;
    uint32_t goal = w->count / 2;
    for (unsigned sweep = 0; sweep < 24 && w->count > goal; ++sweep) {
        if (sweep)
            build_adjacency(w);
        memset(w->marks, 0, w->vertex_count * sizeof(*w->marks));
        memset(w->touched, 0, w->vertex_count * sizeof(*w->touched));
        for (uint32_t i = 0; i < w->vertex_count; ++i)
            w->remap[i] = i;
        uint32_t candidates = 0;
        for (uint32_t first = 0; first < w->count;) {
            uint32_t end = edge_run(w, first);
            edge e = w->edges[first];
            if (end - first == 2 && e.a != e.b && !w->locked[e.a] && !w->locked[e.b]) {
                float length = af_vec3_length(af_vec3_sub(position(mesh, w, e.a), position(mesh, w, e.b)));
                if (isfinite(length) && can_collapse(mesh, w, e, first + 1))
                    w->candidates[candidates++] = (collapse){e, length};
            }
            first = end;
        }
        if (!candidates)
            break;
        qsort(w->candidates, candidates, sizeof(*w->candidates), collapse_compare);
        uint32_t remaining = w->count;
        for (uint32_t i = 0; i < candidates && remaining > goal; ++i) {
            collapse c = w->candidates[i];
            uint32_t a = c.vertices.a, b = c.vertices.b;
            if (w->touched[a] || w->touched[b])
                continue;
            w->remap[a] = b;
            protect_neighbors(w, a);
            protect_neighbors(w, b);
            remaining -= 6;
        }
        uint32_t out = 0;
        for (uint32_t i = 0; i < w->count; i += 3) {
            uint32_t a = w->remap[w->indices[i]], b = w->remap[w->indices[i + 1]], c = w->remap[w->indices[i + 2]];
            if (a != b && b != c && a != c) {
                w->indices[out++] = a;
                w->indices[out++] = b;
                w->indices[out++] = c;
            }
        }
        w->count = out;
        for (uint32_t i = 0; i < w->vertex_count; ++i)
            w->representative[i] = w->remap[w->representative[i]];
    }
    for (uint32_t i = 0; i < w->vertex_count; ++i)
        *error =
            fmaxf(*error, af_vec3_length(af_vec3_sub(position(mesh, w, i), position(mesh, w, w->representative[i]))));
    return w->count;
}

static int compare_vertex(const void *lhs, const void *rhs) {
    const af_vertex *const *a = lhs, *const *b = rhs;
    int result = memcmp(*a, *b, sizeof(**a));
    if (result)
        return result;
    return (*a > *b) - (*a < *b);
}

/* The flattened glTF loader can repeat complete vertices. Weld exact copies
   while preserving attribute seams and the original order of all triangles. */
static bool weld_vertices(af_cluster_mesh *mesh) {
    size_t count = mesh->mesh.vertex_count;
    const af_vertex **order = malloc(count * sizeof(*order));
    uint32_t *remap = malloc(count * sizeof(*remap));
    af_vertex *vertices = malloc(count * sizeof(*vertices));
    if (!order || !remap || !vertices) {
        free(order);
        free(remap);
        free(vertices);
        return false;
    }
    for (size_t i = 0; i < count; ++i)
        order[i] = &mesh->mesh.vertices[i];
    qsort(order, count, sizeof(*order), compare_vertex);
    uint32_t unique = 0;
    for (size_t i = 0; i < count; ++i) {
        if (!i || memcmp(order[i - 1], order[i], sizeof(*vertices)))
            vertices[unique++] = *order[i];
        remap[order[i] - mesh->mesh.vertices] = unique - 1;
    }
    uint32_t *indices = (uint32_t *)mesh->mesh.indices;
    for (uint32_t i = 0; i < mesh->mesh.index_count; ++i)
        indices[i] = remap[indices[i]];
    free(order);
    free(remap);
    free((void *)mesh->mesh.vertices);
    mesh->mesh.vertices = vertices;
    mesh->mesh.vertex_count = unique;
    return true;
}

static void include_point(af_vec3 p, af_vec3 *lo, af_vec3 *hi) {
    *lo = (af_vec3){fminf(lo->x, p.x), fminf(lo->y, p.y), fminf(lo->z, p.z)};
    *hi = (af_vec3){fmaxf(hi->x, p.x), fmaxf(hi->y, p.y), fmaxf(hi->z, p.z)};
}

bool af_cluster_build_lods(af_cluster_mesh *m) {
    if (!m->count || m->regions || !weld_vertices(m))
        return false;
    uint32_t region_count = (m->count + AF_CLUSTER_REGION_LEAVES - 1) / AF_CLUSTER_REGION_LEAVES;
    af_cluster_region *regions = calloc(region_count, sizeof(*regions));
    uint32_t fine_indices = m->mesh.index_count;
    uint32_t *indices = malloc((size_t)fine_indices * 2 * sizeof(*indices));
    af_cluster *clusters = malloc((size_t)m->count * 2 * sizeof(*clusters));
    uint32_t *local = calloc(m->mesh.vertex_count, sizeof(*local));
    region_work *w = malloc(sizeof(*w));
    if (!regions || !indices || !clusters || !local || !w) {
        free(regions);
        free(indices);
        free(clusters);
        free(local);
        free(w);
        return false;
    }
    memcpy(indices, m->mesh.indices, (size_t)fine_indices * sizeof(*indices));
    memcpy(clusters, m->clusters, (size_t)m->count * sizeof(*clusters));
    uint32_t cluster_count = m->count, index_count = fine_indices;
    for (uint32_t i = 0; i < region_count; ++i) {
        af_cluster_region *r = &regions[i];
        r->fine_first = i * AF_CLUSTER_REGION_LEAVES;
        r->fine_count = m->count - r->fine_first;
        if (r->fine_count > AF_CLUSTER_REGION_LEAVES)
            r->fine_count = AF_CLUSTER_REGION_LEAVES;
        uint32_t first = clusters[r->fine_first].first_index;
        af_cluster last = clusters[r->fine_first + r->fine_count - 1];
        uint32_t size = last.first_index + last.index_count - first;
        r->minimum = (af_vec3){INFINITY, INFINITY, INFINITY};
        r->maximum = (af_vec3){-INFINITY, -INFINITY, -INFINITY};
        w->count = size;
        w->vertex_count = 0;
        bool nondegenerate = true;
        for (uint32_t j = 0; j < size; ++j) {
            uint32_t vertex = indices[first + j];
            if (!local[vertex]) {
                w->global[w->vertex_count++] = vertex;
                local[vertex] = w->vertex_count;
                include_point(m->mesh.vertices[vertex].position, &r->minimum, &r->maximum);
            }
            w->indices[j] = local[vertex] - 1;
            if (j % 3 == 2 && (w->indices[j] == w->indices[j - 1] || w->indices[j] == w->indices[j - 2] ||
                               w->indices[j - 1] == w->indices[j - 2]))
                nondegenerate = false;
        }
        memset(w->locked, 0, w->vertex_count * sizeof(*w->locked));
        uint32_t reduced = nondegenerate ? simplify(m, w, &r->error) : size;
        for (uint32_t j = 0; j < w->vertex_count; ++j)
            local[w->global[j]] = 0;
        if (!reduced || reduced == size || !isfinite(r->error)) {
            r->error = 0;
            continue;
        }
        r->coarse_first = cluster_count;
        for (uint32_t offset = 0; offset < reduced;) {
            uint32_t remaining = reduced - offset;
            uint32_t length = remaining < AF_CLUSTER_TRIANGLES * 3 ? remaining : AF_CLUSTER_TRIANGLES * 3;
            af_cluster c = {{INFINITY, INFINITY, INFINITY}, index_count, {-INFINITY, -INFINITY, -INFINITY}, length};
            for (uint32_t end = offset + length; offset < end; ++offset) {
                uint32_t vertex = w->global[w->indices[offset]];
                indices[index_count++] = vertex;
                include_point(m->mesh.vertices[vertex].position, &c.minimum, &c.maximum);
            }
            clusters[cluster_count++] = c;
            ++r->coarse_count;
        }
    }
    free(local);
    free(w);
    free(m->clusters);
    free((void *)m->mesh.indices);
    m->clusters = clusters;
    m->count = cluster_count;
    m->mesh.indices = indices;
    m->mesh.index_count = index_count;
    m->regions = regions;
    m->region_count = region_count;
    return true;
}
