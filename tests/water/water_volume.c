/* SPDX-License-Identifier: MPL-2.0 */
#include "water/water_internal.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            fprintf(stderr, "Water volume %d: %s\n", __LINE__, #x);                                                    \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
typedef struct edge {
    af_vec3 a, b;
    int sign;
} edge;
static int point(af_vec3 a, af_vec3 b) {
    if (a.x != b.x)
        return a.x < b.x ? -1 : 1;
    if (a.y != b.y)
        return a.y < b.y ? -1 : 1;
    return a.z == b.z ? 0 : a.z < b.z ? -1 : 1;
}
static int compare(const void *a, const void *b) {
    const edge *x = a, *y = b;
    int order = point(x->a, y->a);
    return order ? order : point(x->b, y->b);
}
int main(void) {
    af_scene *s = af_scene_create();
    CHECK(s);
    af_object_id id = af_scene_add_object(s, "Volume", "__node");
    CHECK(id);
    for (unsigned mode = 0; mode < 2; ++mode)
        for (unsigned quality = 0; quality < 3; ++quality) {
            af_water w = af_water_default();
            w.mode = (af_water_mode)mode;
            w.quality = (af_water_quality)quality;
            w.wind = 0;
            if (mode) {
                w.path_count = 3;
                w.path[0] = (af_vec3){0, 0, -4};
                w.path[1] = (af_vec3){0, -.2f, 0};
                w.path[2] = (af_vec3){2, -.4f, 4};
            }
            CHECK(af_scene_set_water(s, id, w));
            af_vertex *v = NULL;
            uint32_t *ix = NULL;
            af_mesh_data mesh;
            CHECK(wa_mesh(NULL, s, id, 1, &v, &ix, &mesh));
            edge *edges = malloc(mesh.index_count * sizeof(*edges));
            CHECK(edges);
            for (uint32_t i = 0; i < mesh.index_count; i += 3) {
                af_vec3 a = v[ix[i]].position, b = v[ix[i + 1]].position, c = v[ix[i + 2]].position;
                af_vec3 normal = af_vec3_cross(af_vec3_sub(b, a), af_vec3_sub(c, a));
                if (!(af_vec3_length(normal) > 1e-8f && af_vec3_dot(normal, v[ix[i]].normal) > 0))
                    fprintf(stderr,
                            "mode %u quality %u triangle %u positions (%g,%g,%g) (%g,%g,%g) (%g,%g,%g) dot %g\n", mode,
                            quality, i / 3, a.x, a.y, a.z, b.x, b.y, b.z, c.x, c.y, c.z,
                            af_vec3_dot(normal, v[ix[i]].normal));
                CHECK(af_vec3_length(normal) > 1e-8f && af_vec3_dot(normal, v[ix[i]].normal) > 0);
                for (unsigned j = 0; j < 3; ++j) {
                    a = v[ix[i + j]].position;
                    b = v[ix[i + (j + 1) % 3]].position;
                    edges[i + j] = (edge){point(a, b) < 0 ? a : b, point(a, b) < 0 ? b : a, point(a, b) < 0 ? 1 : -1};
                }
            }
            qsort(edges, mesh.index_count, sizeof(*edges), compare);
            for (uint32_t i = 0; i < mesh.index_count; i += 2)
                CHECK(i + 1 < mesh.index_count && !compare(&edges[i], &edges[i + 1]) &&
                      edges[i].sign + edges[i + 1].sign == 0);
            CHECK(wa_contains(NULL, s, id, (af_vec3){0, -1, 0}));
            CHECK(!wa_contains(NULL, s, id, (af_vec3){0, -3, 0}));
            free(edges);
            free(v);
            free(ix);
        }
    af_scene_destroy(s);
    puts("Water volumes: watertight outward top/side/bottom boundaries at all qualities, sloped river and containment "
         "passed.");
    return 0;
}
