/* SPDX-License-Identifier: MPL-2.0 */
#include "water/water_internal.h"
#include <afterlight/physics.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
static double now(void) {
    return 1000.0 * (double)clock() / CLOCKS_PER_SEC;
}
int main(int argc, char **argv) {
    if (argc < 2 || argc > 3)
        return 2;
    unsigned ticks = argc == 3 ? (unsigned)strtoul(argv[2], NULL, 10) : 180;
    if (ticks < 30 || ticks > 3600)
        return 2;
    const char *names[] = {"water-pool",       "water-lake", "water-stream",
                           "water-river-bend", "water-pour", "water-pour-lab"};
    puts("scene,quality,ticks,physics_mean_ms,physics_max_ms,mesh_mean_ms,mesh_max_ms,vertices,"
         "particles,cells");
    for (unsigned scene = 0; scene < 6; ++scene)
        for (unsigned quality = 0; quality < 3; ++quality) {
            char path[4096];
            snprintf(path, sizeof(path), "%s/%s.afterlight.json", argv[1], names[scene]);
            af_scene_error e;
            af_scene *s = af_scene_load(path, &e);
            af_physics *p = af_physics_create();
            if (!s || !p) {
                fprintf(stderr, "%s\n", s ? "Allocation failed" : e.message);
                return 1;
            }
            af_water w = *af_scene_water_at(s, 0);
            w.quality = (af_water_quality)quality;
            if (!af_scene_set_water(s, w.owner, w))
                return 1;
            double total = 0, maximum = 0, mesh_total = 0, mesh_max = 0;
            uint32_t vertices = 0;
            for (unsigned i = 0; i < ticks + 30; ++i) {
                double start = now();
                if (!af_physics_step(p, s, 1.f / 60))
                    return 1;
                double physics = now() - start;
                af_vertex *v = NULL;
                uint32_t *ix = NULL;
                af_mesh_data mesh;
                start = now();
                if (!wa_mesh(ph_water_runtime(p), s, w.owner, 1, &v, &ix, &mesh))
                    return 1;
                double geometry = now() - start;
                vertices = mesh.vertex_count;
                free(v);
                free(ix);
                if (i >= 30) {
                    total += physics;
                    maximum = fmax(maximum, physics);
                    mesh_total += geometry;
                    mesh_max = fmax(mesh_max, geometry);
                }
            }
            af_water_stats stats = af_physics_water_stats(p);
            printf("%s,%u,%u,%.5f,%.5f,%.5f,%.5f,%u,%zu,%zu\n", names[scene], quality, ticks, total / ticks, maximum,
                   mesh_total / ticks, mesh_max, vertices, stats.particles, stats.cells);
            fflush(stdout);
            af_physics_destroy(p);
            af_scene_destroy(s);
        }
    return 0;
}
