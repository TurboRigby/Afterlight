/* SPDX-License-Identifier: MPL-2.0 */
#include "cluster_stream.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_PAGE_INDICES (AF_CLUSTER_REGION_LEAVES * AF_CLUSTER_TRIANGLES * 3)
#define MAX_STREAM_BYTES (256u * 1024u * 1024u)
#define MAX_STREAM_REGIONS 65536u

static const char magic[] = "AFPAGES1";
_Static_assert(sizeof(af_cluster_stream_region) == 48, "Packed page directory layout");

typedef struct af_page_request {
    uint64_t frame;
    float distance;
    uint32_t index;
} af_page_request;

struct af_cluster_stream {
    FILE *file;
    af_cluster_stream_region *regions;
    long *offsets;
    uint32_t count, pending;
    af_page_request requests[AF_CLUSTER_REQUEST_LIMIT];
    uint32_t queued, peak;
    SDL_Thread *thread;
    SDL_Mutex *mutex;
    SDL_Condition *condition;
    bool stopping, busy, ready;
    af_cluster_mesh result;
};

static bool little_endian(void) {
    uint32_t one = 1;
    return *(const unsigned char *)&one == 1;
}

static uint64_t mesh_bytes(uint32_t vertices, uint32_t indices, uint32_t clusters) {
    return (uint64_t)vertices * sizeof(af_vertex) + (uint64_t)indices * sizeof(uint32_t) +
           (uint64_t)clusters * sizeof(af_cluster);
}

static bool write_mesh(FILE *f, const af_cluster_mesh *mesh) {
    return fwrite(mesh->clusters, sizeof(af_cluster), mesh->count, f) == mesh->count &&
           fwrite(mesh->mesh.vertices, sizeof(af_vertex), mesh->mesh.vertex_count, f) == mesh->mesh.vertex_count &&
           fwrite(mesh->mesh.indices, sizeof(uint32_t), mesh->mesh.index_count, f) == mesh->mesh.index_count;
}

static bool read_mesh(FILE *f, af_cluster_mesh *mesh, uint32_t vertices, uint32_t indices, uint32_t clusters) {
    return af_cluster_allocate_mesh(mesh, vertices, indices, clusters) &&
           fread(mesh->clusters, sizeof(af_cluster), clusters, f) == clusters &&
           fread((void *)mesh->mesh.vertices, sizeof(af_vertex), vertices, f) == vertices &&
           fread((void *)mesh->mesh.indices, sizeof(uint32_t), indices, f) == indices && af_cluster_validate_mesh(mesh);
}

static void append_clusters(const af_cluster_mesh *source, uint32_t first, uint32_t count, uint32_t *map,
                            af_cluster_mesh *out) {
    af_vertex *vertices = (af_vertex *)out->mesh.vertices;
    uint32_t *indices = (uint32_t *)out->mesh.indices;
    for (uint32_t i = 0; i < count; ++i) {
        af_cluster c = source->clusters[first + i];
        out->clusters[out->count++] = (af_cluster){c.minimum, out->mesh.index_count, c.maximum, c.index_count};
        for (uint32_t j = 0; j < c.index_count; ++j) {
            uint32_t vertex = source->mesh.indices[c.first_index + j];
            if (!map[vertex]) {
                vertices[out->mesh.vertex_count++] = source->mesh.vertices[vertex];
                map[vertex] = out->mesh.vertex_count;
            }
            indices[out->mesh.index_count++] = map[vertex] - 1;
        }
    }
}

bool af_cluster_save_stream(const char *path, const af_cluster_mesh *source) {
    if (!little_endian() || !source->region_count || source->region_count > MAX_STREAM_REGIONS)
        return false;
    size_t map_bytes = (size_t)source->mesh.vertex_count * sizeof(uint32_t);
    uint32_t *map = calloc(source->mesh.vertex_count, sizeof(*map));
    af_cluster_stream_region *table = calloc(source->region_count, sizeof(*table));
    af_cluster_mesh coarse = {.mesh = {.vertices = malloc((size_t)source->mesh.vertex_count * sizeof(af_vertex)),
                                       .indices = malloc((size_t)source->mesh.index_count * sizeof(uint32_t))},
                              .clusters = malloc((size_t)source->count * sizeof(af_cluster))};
    af_cluster_mesh page = {.mesh = {.vertices = malloc(MAX_PAGE_INDICES * sizeof(af_vertex)),
                                     .indices = malloc(MAX_PAGE_INDICES * sizeof(uint32_t))},
                            .clusters = malloc(AF_CLUSTER_REGION_LEAVES * sizeof(af_cluster))};
    bool ok = map && table && coarse.mesh.vertices && coarse.mesh.indices && coarse.clusters && page.mesh.vertices &&
              page.mesh.indices && page.clusters;
    for (uint32_t i = 0; ok && i < source->region_count; ++i) {
        const af_cluster_region *r = &source->regions[i];
        uint32_t first = r->coarse_count ? r->coarse_first : r->fine_first;
        uint32_t count = r->coarse_count ? r->coarse_count : r->fine_count;
        table[i] = (af_cluster_stream_region){r->minimum, coarse.count, r->maximum, count, r->error, 0, 0, 0};
        append_clusters(source, first, count, map, &coarse);
    }
    uint32_t header[] = {source->region_count, coarse.mesh.vertex_count, coarse.mesh.index_count, coarse.count};
    uint64_t total = sizeof(magic) + sizeof(header) + (uint64_t)header[0] * sizeof(*table) +
                     mesh_bytes(header[1], header[2], header[3]);
    FILE *f = ok && total <= MAX_STREAM_BYTES ? fopen(path, "wbx") : NULL;
    ok = f && fwrite(magic, 1, sizeof(magic), f) == sizeof(magic) && fwrite(header, sizeof(*header), 4, f) == 4 &&
         fwrite(table, sizeof(*table), header[0], f) == header[0] && write_mesh(f, &coarse);
    if (ok)
        memset(map, 0, map_bytes);
    for (uint32_t i = 0; ok && i < source->region_count; ++i) {
        const af_cluster_region *r = &source->regions[i];
        if (!r->coarse_count)
            continue;
        page.mesh.vertex_count = page.mesh.index_count = page.count = 0;
        append_clusters(source, r->fine_first, r->fine_count, map, &page);
        table[i].page_vertices = page.mesh.vertex_count;
        table[i].page_indices = page.mesh.index_count;
        table[i].page_clusters = page.count;
        total += mesh_bytes(page.mesh.vertex_count, page.mesh.index_count, page.count);
        ok = total <= MAX_STREAM_BYTES && write_mesh(f, &page);
        for (uint32_t c = 0; c < r->fine_count; ++c) {
            af_cluster cluster = source->clusters[r->fine_first + c];
            for (uint32_t j = 0; j < cluster.index_count; ++j)
                map[source->mesh.indices[cluster.first_index + j]] = 0;
        }
    }
    ok = ok && !fseek(f, (long)(sizeof(magic) + sizeof(header)), SEEK_SET) &&
         fwrite(table, sizeof(*table), header[0], f) == header[0];
    if (f) {
        if (fclose(f))
            ok = false;
        if (!ok)
            remove(path);
    }
    free(map);
    free(table);
    af_cluster_free(&coarse);
    af_cluster_free(&page);
    return ok;
}

static bool contains(const af_cluster_stream_region *r, const af_cluster *c) {
    return c->minimum.x >= r->minimum.x && c->minimum.y >= r->minimum.y && c->minimum.z >= r->minimum.z &&
           c->maximum.x <= r->maximum.x && c->maximum.y <= r->maximum.y && c->maximum.z <= r->maximum.z;
}

static bool valid_region(const af_cluster_stream_region *r) {
    return isfinite(r->minimum.x) && isfinite(r->minimum.y) && isfinite(r->minimum.z) && isfinite(r->maximum.x) &&
           isfinite(r->maximum.y) && isfinite(r->maximum.z) && r->minimum.x <= r->maximum.x &&
           r->minimum.y <= r->maximum.y && r->minimum.z <= r->maximum.z && isfinite(r->error) && r->error >= 0 &&
           r->coarse_count && r->coarse_count <= AF_CLUSTER_REGION_LEAVES &&
           ((!r->page_vertices && !r->page_indices && !r->page_clusters) ||
            (r->page_vertices && r->page_vertices <= MAX_PAGE_INDICES && r->page_indices &&
             r->page_indices <= MAX_PAGE_INDICES && r->page_indices % 3 == 0 && r->page_clusters &&
             r->page_clusters <= AF_CLUSTER_REGION_LEAVES));
}

static bool load_directory(af_cluster_stream *s, af_cluster_mesh *coarse) {
    FILE *f = s->file;
    if (fseek(f, 0, SEEK_END))
        return false;
    long size = ftell(f);
    if (size <= 0 || size > MAX_STREAM_BYTES || fseek(f, 0, SEEK_SET))
        return false;
    char tag[sizeof(magic)];
    uint32_t header[4];
    if (fread(tag, 1, sizeof(tag), f) != sizeof(tag) || memcmp(tag, magic, sizeof(tag)) ||
        fread(header, sizeof(*header), 4, f) != 4 || !header[0] || header[0] > MAX_STREAM_REGIONS)
        return false;
    s->count = header[0];
    s->regions = malloc((size_t)s->count * sizeof(*s->regions));
    s->offsets = malloc((size_t)s->count * sizeof(*s->offsets));
    if (!s->regions || !s->offsets || fread(s->regions, sizeof(*s->regions), s->count, f) != s->count ||
        !read_mesh(f, coarse, header[1], header[2], header[3]))
        return false;
    long start = ftell(f);
    if (start < 0)
        return false;
    uint64_t offset = (uint64_t)start;
    uint32_t next = 0;
    for (uint32_t i = 0; i < s->count; ++i) {
        const af_cluster_stream_region *r = &s->regions[i];
        if (!valid_region(r) || r->coarse_first != next || r->coarse_count > coarse->count - next)
            return false;
        for (uint32_t j = 0; j < r->coarse_count; ++j)
            if (!contains(r, &coarse->clusters[next++]))
                return false;
        s->offsets[i] = (long)offset;
        offset += mesh_bytes(r->page_vertices, r->page_indices, r->page_clusters);
        if (offset > (uint64_t)size)
            return false;
    }
    return offset == (uint64_t)size && next == coarse->count;
}

static bool higher_priority(af_page_request a, af_page_request b) {
    if (a.frame != b.frame)
        return a.frame > b.frame;
    if (a.distance != b.distance)
        return a.distance < b.distance;
    return a.index < b.index;
}

static int read_pages(void *context) {
    af_cluster_stream *s = context;
    SDL_LockMutex(s->mutex);
    while (!s->stopping) {
        if (!s->queued || s->ready) {
            SDL_WaitCondition(s->condition, s->mutex);
            continue;
        }
        uint32_t best = 0;
        for (uint32_t i = 1; i < s->queued; ++i)
            if (higher_priority(s->requests[i], s->requests[best]))
                best = i;
        uint32_t index = s->requests[best].index;
        s->requests[best] = s->requests[--s->queued];
        s->pending = index;
        s->busy = true;
        SDL_UnlockMutex(s->mutex);
        af_cluster_mesh page = {0};
        const af_cluster_stream_region *r = &s->regions[index];
        bool ok = !fseek(s->file, s->offsets[index], SEEK_SET) &&
                  read_mesh(s->file, &page, r->page_vertices, r->page_indices, r->page_clusters);
        for (uint32_t i = 0; ok && i < page.count; ++i)
            ok = contains(r, &page.clusters[i]);
        if (!ok)
            af_cluster_free(&page);
        SDL_LockMutex(s->mutex);
        s->result = page;
        s->busy = false;
        s->ready = true;
    }
    SDL_UnlockMutex(s->mutex);
    return 0;
}

af_cluster_stream *af_cluster_open_stream(const char *path, af_cluster_mesh *coarse) {
    *coarse = (af_cluster_mesh){0};
    af_cluster_stream *s = path && little_endian() ? calloc(1, sizeof(*s)) : NULL;
    if (!s)
        return NULL;
    s->file = fopen(path, "rb");
    if (s->file && load_directory(s, coarse)) {
        s->mutex = SDL_CreateMutex();
        s->condition = SDL_CreateCondition();
        if (s->mutex && s->condition)
            s->thread = SDL_CreateThread(read_pages, "Afterlight geometry pages", s);
        if (s->thread)
            return s;
    }
    af_cluster_free(coarse);
    af_cluster_close_stream(s);
    return NULL;
}

void af_cluster_close_stream(af_cluster_stream *s) {
    if (!s)
        return;
    if (s->thread) {
        SDL_LockMutex(s->mutex);
        s->stopping = true;
        SDL_SignalCondition(s->condition);
        SDL_UnlockMutex(s->mutex);
        SDL_WaitThread(s->thread, NULL);
    }
    if (s->file)
        fclose(s->file);
    SDL_DestroyCondition(s->condition);
    SDL_DestroyMutex(s->mutex);
    af_cluster_free(&s->result);
    free(s->regions);
    free(s->offsets);
    free(s);
}

uint32_t af_cluster_stream_region_count(const af_cluster_stream *s) {
    return s->count;
}

const af_cluster_stream_region *af_cluster_stream_region_at(const af_cluster_stream *s, uint32_t i) {
    return &s->regions[i];
}

void af_cluster_prune_requests(af_cluster_stream *s, uint64_t frame) {
    SDL_LockMutex(s->mutex);
    uint32_t remaining = 0;
    for (uint32_t i = 0; i < s->queued; ++i)
        if (frame - s->requests[i].frame <= 2)
            s->requests[remaining++] = s->requests[i];
    s->queued = remaining;
    SDL_UnlockMutex(s->mutex);
}

void af_cluster_request_page(af_cluster_stream *s, uint32_t index, uint64_t frame, float distance) {
    if (index >= s->count || !s->regions[index].page_clusters)
        return;
    af_page_request request = {frame, isfinite(distance) ? distance : INFINITY, index};
    SDL_LockMutex(s->mutex);
    if ((s->busy || s->ready) && s->pending == index)
        goto done;
    uint32_t worst = 0;
    for (uint32_t i = 0; i < s->queued; ++i) {
        if (s->requests[i].index == index) {
            if (higher_priority(request, s->requests[i]))
                s->requests[i] = request;
            goto done;
        }
        if (higher_priority(s->requests[worst], s->requests[i]))
            worst = i;
    }
    if (s->queued < AF_CLUSTER_REQUEST_LIMIT) {
        s->requests[s->queued++] = request;
        if (s->peak < s->queued)
            s->peak = s->queued;
    } else if (higher_priority(request, s->requests[worst]))
        s->requests[worst] = request;
    SDL_SignalCondition(s->condition);
done:
    SDL_UnlockMutex(s->mutex);
}

uint32_t af_cluster_request_depth(af_cluster_stream *s) {
    SDL_LockMutex(s->mutex);
    uint32_t queued = s->queued;
    SDL_UnlockMutex(s->mutex);
    return queued;
}

uint32_t af_cluster_request_peak(af_cluster_stream *s) {
    SDL_LockMutex(s->mutex);
    uint32_t peak = s->peak;
    SDL_UnlockMutex(s->mutex);
    return peak;
}

bool af_cluster_take_page(af_cluster_stream *s, uint32_t *index, af_cluster_mesh *page) {
    SDL_LockMutex(s->mutex);
    bool ready = s->ready;
    if (ready) {
        *index = s->pending;
        *page = s->result;
        s->result = (af_cluster_mesh){0};
        s->ready = false;
        SDL_SignalCondition(s->condition);
    }
    SDL_UnlockMutex(s->mutex);
    return ready;
}
