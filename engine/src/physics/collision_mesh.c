/* SPDX-License-Identifier: MPL-2.0 */
#include "collision_mesh_internal.h"
#include "scene/scene_internal.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct cm_node {
    af_vec3 low, high;
    uint32_t left, right, start, count;
} cm_node;
struct af_collision_mesh {
    unsigned refs;
    size_t count, node_count, bytes;
    af_triangle *triangles;
    cm_node *nodes;
};
static bool finite3(af_vec3 v) {
    return isfinite(v.x) && isfinite(v.y) && isfinite(v.z) && fabsf(v.x) <= 1000000 && fabsf(v.y) <= 1000000 &&
           fabsf(v.z) <= 1000000;
}
static af_vec3 minimum(af_vec3 a, af_vec3 b) {
    return (af_vec3){fminf(a.x, b.x), fminf(a.y, b.y), fminf(a.z, b.z)};
}
static af_vec3 maximum(af_vec3 a, af_vec3 b) {
    return (af_vec3){fmaxf(a.x, b.x), fmaxf(a.y, b.y), fmaxf(a.z, b.z)};
}
static float component(af_vec3 v, unsigned i) {
    return i == 0 ? v.x : i == 1 ? v.y : v.z;
}
static float center(af_triangle t, unsigned axis) {
    return (component(t.a, axis) + component(t.b, axis) + component(t.c, axis)) / 3;
}
static void median(af_triangle *t, size_t start, size_t end, size_t mid, unsigned axis) {
    while (end - start > 1) {
        float pivot = center(t[start + (end - start) / 2], axis);
        size_t i = start, j = end - 1;
        while (i <= j) {
            while (i < end && center(t[i], axis) < pivot)
                ++i;
            while (j > start && center(t[j], axis) > pivot)
                --j;
            if (i <= j) {
                af_triangle swap = t[i];
                t[i] = t[j];
                t[j] = swap;
                ++i;
                if (!j)
                    break;
                --j;
            }
        }
        if (mid <= j && j >= start)
            end = j + 1;
        else if (mid >= i)
            start = i;
        else
            break;
    }
}
static uint32_t build(af_collision_mesh *m, size_t start, size_t count) {
    uint32_t at = (uint32_t)m->node_count++;
    cm_node *node = &m->nodes[at];
    node->low = node->high = m->triangles[start].a;
    for (size_t i = start; i < start + count; ++i) {
        af_triangle t = m->triangles[i];
        node->low = minimum(node->low, minimum(t.a, minimum(t.b, t.c)));
        node->high = maximum(node->high, maximum(t.a, maximum(t.b, t.c)));
    }
    if (count <= 8) {
        node->start = (uint32_t)start;
        node->count = (uint32_t)count;
    } else {
        af_vec3 extent = af_vec3_sub(node->high, node->low);
        unsigned axis = extent.y > extent.x ? 1 : 0;
        if (extent.z > component(extent, axis))
            axis = 2;
        size_t half = count / 2;
        median(m->triangles, start, start + count, start + half, axis);
        node->left = build(m, start, half);
        node->right = build(m, start + half, count - half);
    }
    return at;
}
void cm_retain(af_collision_mesh *m) {
    if (m)
        ++m->refs;
}
void cm_release(af_collision_mesh *m) {
    if (m && !--m->refs) {
        free(m->triangles);
        free(m->nodes);
        free(m);
    }
}
static bool fail(af_asset_error *e, const char *message) {
    if (e)
        snprintf(e->message, sizeof(e->message), "%s", message);
    return false;
}
static af_collision_mesh *create(const af_mesh_data *data, af_asset_error *error) {
    if (!data || !data->vertices || !data->indices || !data->vertex_count || !data->index_count ||
        data->index_count % 3 || data->index_count / 3 > AF_COLLISION_MESH_MAX_TRIANGLES) {
        fail(error, "Collision mesh needs 1–262144 indexed triangles.");
        return NULL;
    }
    af_collision_mesh *m = calloc(1, sizeof(*m));
    if (!m)
        return NULL;
    m->refs = 1;
    size_t count = data->index_count / 3;
    m->triangles = calloc(count, sizeof(af_triangle));
    m->nodes = calloc(count * 2, sizeof(cm_node));
    if (!m->triangles || !m->nodes) {
        cm_release(m);
        return NULL;
    }
    for (size_t i = 0; i < count; ++i) {
        af_vec3 v[3];
        for (unsigned j = 0; j < 3; ++j) {
            uint32_t index = data->indices[i * 3 + j];
            if (index >= data->vertex_count || !finite3(data->vertices[index].position)) {
                cm_release(m);
                fail(error, "Collision mesh has invalid vertices/indices.");
                return NULL;
            }
            v[j] = data->vertices[index].position;
        }
        af_vec3 normal = af_vec3_cross(af_vec3_sub(v[1], v[0]), af_vec3_sub(v[2], v[0]));
        float area = af_vec3_length(normal);
        if (!isfinite(area)) {
            cm_release(m);
            fail(error, "Collision triangle exceeds numeric bounds.");
            return NULL;
        }
        if (area < 1e-8f)
            continue;
        m->triangles[m->count++] = (af_triangle){v[0], v[1], v[2]};
    }
    if (!m->count) {
        cm_release(m);
        fail(error, "Collision mesh contains only degenerate triangles.");
        return NULL;
    }
    build(m, 0, m->count);
    m->bytes = sizeof(*m) + count * (sizeof(af_triangle) + 2 * sizeof(cm_node));
    return m;
}
bool af_scene_set_collision_mesh(af_scene *s, const char *name, const af_mesh_data *data, af_asset_error *e) {
    if (e)
        *e = (af_asset_error){0};
    if (!s || !name || !name[0] || strlen(name) >= 64)
        return fail(e, "Collision mesh requires a name.");
    size_t at = 0;
    while (at < s->collision_mesh_count && strcmp(s->collision_meshes[at].name, name))
        ++at;
    if (at == AF_COLLISION_MESH_MAX_ASSETS)
        return fail(e, "Collision library exceeds 64 assets.");
    af_collision_mesh *m = create(data, e);
    if (!m)
        return false;
    size_t total = m->count;
    for (size_t i = 0; i < s->collision_mesh_count; ++i)
        if (i != at)
            total += s->collision_meshes[i].mesh->count;
    if (total > AF_COLLISION_MESH_MAX_TRIANGLES) {
        cm_release(m);
        return fail(e, "Scene collision library exceeds 262144 triangles.");
    }
    if (at < s->collision_mesh_count)
        cm_release(s->collision_meshes[at].mesh);
    else
        ++s->collision_mesh_count;
    s->collision_meshes[at] = (cm_slot){.mesh = m};
    snprintf(s->collision_meshes[at].name, 64, "%s", name);
    return true;
}
const af_collision_mesh *af_scene_collision_mesh(const af_scene *s, const char *name) {
    if (s && name)
        for (size_t i = 0; i < s->collision_mesh_count; ++i)
            if (!strcmp(s->collision_meshes[i].name, name))
                return s->collision_meshes[i].mesh;
    return NULL;
}
af_collision_mesh_stats af_scene_collision_mesh_stats(const af_scene *s) {
    af_collision_mesh_stats stats = {0};
    if (s)
        for (size_t i = 0; i < s->collision_mesh_count; ++i) {
            const af_collision_mesh *m = s->collision_meshes[i].mesh;
            ++stats.meshes;
            stats.triangles += m->count;
            stats.nodes += m->node_count;
            stats.bytes += m->bytes;
        }
    return stats;
}
bool af_scene_prepare_collision_meshes(af_scene *s, af_asset_error *error) {
    if (error)
        *error = (af_asset_error){0};
    if (!s)
        return false;
    bool pending = false;
    for (size_t i = 0; i < s->count; ++i) {
        const af_scene_object *o = &s->objects[i];
        if (o->collider.type != AF_COLLIDER_MESH || !o->collider.enabled)
            continue;
        if (o->body.enabled && o->body.type != AF_BODY_STATIC)
            return fail(error, "Triangle mesh colliders must be static.");
        const af_scene_asset *a = af_scene_find_asset(s, o->mesh);
        size_t at = 0;
        while (at < s->collision_mesh_count && strcmp(s->collision_meshes[at].name, o->mesh))
            ++at;
        if (at == s->collision_mesh_count || (a && strcmp(s->collision_meshes[at].path, a->path)))
            pending = true;
    }
    if (!pending)
        return true;
    af_scene *stage = af_scene_clone(s);
    if (!stage)
        return false;
    bool ok = true;
    for (size_t i = 0; i < stage->count && ok; ++i) {
        const af_scene_object *o = &stage->objects[i];
        if (o->collider.type != AF_COLLIDER_MESH || !o->collider.enabled)
            continue;
        if (o->body.enabled && o->body.type != AF_BODY_STATIC) {
            ok = fail(error, "Triangle mesh colliders must be static.");
            break;
        }
        const af_scene_asset *asset = af_scene_find_asset(stage, o->mesh);
        size_t at = 0;
        while (at < stage->collision_mesh_count && strcmp(stage->collision_meshes[at].name, o->mesh))
            ++at;
        if (at < stage->collision_mesh_count && (!asset || !strcmp(stage->collision_meshes[at].path, asset->path)))
            continue;
        if (!asset || asset->kind != AF_ASSET_MESH) {
            ok = fail(error, "Mesh collider needs an imported mesh asset or uploaded CPU collision mesh.");
            break;
        }
        af_mesh_file file = {0};
        if (!af_mesh_file_load(asset->path, &file, error)) {
            ok = false;
            break;
        }
        ok = af_scene_set_collision_mesh(stage, asset->name, &file.data, error);
        af_mesh_file_free(&file);
        if (ok) {
            size_t slot = 0;
            while (strcmp(stage->collision_meshes[slot].name, asset->name))
                ++slot;
            snprintf(stage->collision_meshes[slot].path, 4096, "%s", asset->path);
        }
    }
    if (ok) {
        for (size_t i = 0; i < s->collision_mesh_count; ++i)
            cm_release(s->collision_meshes[i].mesh);
        memcpy(s->collision_meshes, stage->collision_meshes, sizeof(s->collision_meshes));
        s->collision_mesh_count = stage->collision_mesh_count;
        stage->collision_mesh_count = 0;
    }
    af_scene_destroy(stage);
    return ok;
}
static bool intersects(af_vec3 a, af_vec3 b, af_vec3 c, af_vec3 d) {
    return a.x <= d.x && b.x >= c.x && a.y <= d.y && b.y >= c.y && a.z <= d.z && b.z >= c.z;
}
size_t af_collision_mesh_query(const af_collision_mesh *m, af_vec3 low, af_vec3 high, uint32_t *out, size_t capacity) {
    if (!m || !finite3(low) || !finite3(high) || low.x > high.x || low.y > high.y || low.z > high.z ||
        (!out && capacity))
        return 0;
    uint32_t stack[64];
    size_t pending = 1, count = 0;
    stack[0] = 0;
    while (pending) {
        const cm_node *n = &m->nodes[stack[--pending]];
        if (!intersects(low, high, n->low, n->high))
            continue;
        if (n->count) {
            for (uint32_t i = n->start; i < n->start + n->count; ++i) {
                af_triangle t = m->triangles[i];
                af_vec3 a = minimum(t.a, minimum(t.b, t.c)), b = maximum(t.a, maximum(t.b, t.c));
                if (!intersects(low, high, a, b))
                    continue;
                if (count < capacity)
                    out[count] = i;
                ++count;
            }
        } else {
            if (pending + 2 > 64)
                return 0;
            stack[pending++] = n->left;
            stack[pending++] = n->right;
        }
    }
    return count;
}
bool af_collision_mesh_triangle(const af_collision_mesh *m, uint32_t i, af_triangle *out) {
    if (!m || !out || i >= m->count)
        return false;
    *out = m->triangles[i];
    return true;
}
bool af_collision_mesh_bounds(const af_collision_mesh *m, af_vec3 *low, af_vec3 *high) {
    if (!m || !low || !high)
        return false;
    *low = m->nodes[0].low;
    *high = m->nodes[0].high;
    return true;
}
