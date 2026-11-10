/* SPDX-License-Identifier: MPL-2.0 */
#include "geometry/cluster.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool wall(af_mesh_file *out) {
    enum { WIDTH = 384, HEIGHT = 192, STRIDE = WIDTH + 1 };
    af_vertex *vertices = calloc(STRIDE * (HEIGHT + 1), sizeof(*vertices));
    uint32_t *indices = malloc(WIDTH * HEIGHT * 6 * sizeof(*indices));
    if (!vertices || !indices) {
        free(vertices);
        free(indices);
        return false;
    }
    for (unsigned y = 0; y <= HEIGHT; ++y)
        for (unsigned x = 0; x <= WIDTH; ++x) {
            float u = (float)x / (float)WIDTH, v = (float)y / (float)HEIGHT;
            float px = u * 32 - 16, py = v * 16 - 8;
            float grain = sinf(px * 8 + sinf(py * 6)) * cosf(py * 8 + px);
            float depth = .31f * sinf(px * 1.3f + sinf(py)) * cosf(py * 1.45f) +
                          .10f * sinf(px * 3.5f + py) * sinf(py * 3.1f) + .018f * grain;
            float shade = .52f + .16f * sinf(px * .72f + py) + .06f * grain;
            vertices[y * STRIDE + x] =
                (af_vertex){.position = {px, py, depth}, .color = {shade, shade * .87f, shade * .72f, 1}, .uv = {u, v}};
        }
    for (unsigned y = 0; y < HEIGHT; ++y)
        for (unsigned x = 0; x < WIDTH; ++x) {
            uint32_t a = y * STRIDE + x, b = a + 1, c = a + STRIDE, d = c + 1;
            uint32_t *quad = indices + 6 * (y * WIDTH + x);
            quad[0] = a;
            quad[1] = b;
            quad[2] = c;
            quad[3] = b;
            quad[4] = d;
            quad[5] = c;
        }
    out->data = (af_mesh_data){vertices, STRIDE * (HEIGHT + 1), indices, WIDTH * HEIGHT * 6};
    return true;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: afterlight_cluster_cook INPUT.glb|--wall OUTPUT.afgeom\n");
        return 1;
    }
    af_mesh_file source = {0};
    af_asset_error error = {0};
    bool loaded = strcmp(argv[1], "--wall") ? af_mesh_file_load(argv[1], &source, &error) : wall(&source);
    if (!loaded) {
        fprintf(stderr, "Cannot load source mesh. %s\n", error.message);
        return 1;
    }
    af_cluster_mesh mesh;
    bool ok = af_cluster_build(&source.data, &mesh);
    af_mesh_file_free(&source);
    if (ok)
        ok = af_cluster_save(argv[2], &mesh);
    if (ok)
        printf("%u triangles in %u clusters\n", mesh.mesh.index_count / 3, mesh.count);
    else
        fprintf(stderr, "Cannot cook mesh or create %s. Use a new output file.\n", argv[2]);
    af_cluster_free(&mesh);
    return ok ? 0 : 1;
}
