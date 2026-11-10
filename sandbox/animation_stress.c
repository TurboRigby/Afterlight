/* SPDX-License-Identifier: MPL-2.0 */
#include "animation/animation_runtime.h"
#include <afterlight/model.h>
#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
static int compare(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}
int main(int argc, char **argv) {
    if (argc != 2) {
        fputs("Usage: afterlight_animation_stress LOCOMOTION_GLTF\n", stderr);
        return 2;
    }
    puts("actors,objects,samples,mean_ms,p95_ms,max_ms");
    unsigned counts[] = {1, 16, 64};
    for (unsigned run = 0; run < 3; ++run) {
        af_scene *scene = af_scene_create();
        af_animation_runtime *runtime = af_animation_runtime_create(NULL);
        af_object_id roots[64];
        af_asset_error error;
        double values[120], sum = 0;
        if (!scene || !runtime)
            return 1;
        for (unsigned i = 0; i < counts[run]; ++i) {
            if (!af_scene_import_model(scene, "crowd", argv[1], &roots[i], &error))
                return 1;
            af_transform t = af_transform_identity();
            t.position = (af_vec3){(float)(i % 8) * 3, 0, (float)(i / 8) * 3};
            if (!af_scene_set_object_transform(scene, roots[i], t))
                return 1;
        }
        const char *asset = af_scene_get_object(scene, roots[0])->animation.asset;
        if (!af_animation_runtime_load(runtime, asset, argv[1], &error))
            return 1;
        af_animation_runtime_scene(runtime, scene);
        for (unsigned i = 0; i < counts[run]; ++i)
            if (!af_animation_runtime_root_motion(runtime, roots[i], 10, false))
                return 1;
        for (unsigned tick = 0; tick < 150; ++tick) {
            Uint64 start = SDL_GetPerformanceCounter();
            for (unsigned i = 0; i < counts[run]; ++i)
                if (!af_animation_runtime_locomotion(
                        runtime, roots[i], (af_locomotion_input){.speed = 2 + (float)(i % 4), .grounded = true},
                        1.f / 60, NULL))
                    return 1;
            if (!af_animation_runtime_update(runtime, NULL, 1, 1.f / 60, true))
                return 1;
            double elapsed =
                1000.0 * (double)(SDL_GetPerformanceCounter() - start) / (double)SDL_GetPerformanceFrequency();
            if (tick >= 30) {
                values[tick - 30] = elapsed;
                sum += elapsed;
            }
        }
        for (unsigned i = 0; i < counts[run]; ++i) {
            af_animation_bone bones[9];
            if (af_animation_runtime_bones(runtime, roots[i], bones, 9) != 9 || !isfinite(bones[0].position.y))
                return 1;
        }
        qsort(values, 120, sizeof(*values), compare);
        printf("%u,%zu,120,%.4f,%.4f,%.4f\n", counts[run], af_scene_object_count(scene), sum / 120, values[113],
               values[119]);
        af_animation_runtime_destroy(runtime);
        af_scene_destroy(scene);
    }
    return 0;
}
