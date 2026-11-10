/* SPDX-License-Identifier: MPL-2.0 */
#include "render/ibl.h"
#include "render/local_shadow.h"
#include <math.h>
#include <stdio.h>
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            fprintf(stderr, "Lighting math line %d: %s\n", __LINE__, #c);                                              \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
int main(void) {
    af_ibl_images images;
    CHECK(!af_ibl_bake(3, &images));
    CHECK(af_ibl_bake(0, &images));
    size_t offset = 0;
    uint32_t w = AF_IBL_WIDTH, h = AF_IBL_HEIGHT;
    float sharp = 0, rough = 0;
    for (unsigned level = 0; level < AF_IBL_LEVELS; ++level) {
        float maximum = 0;
        for (size_t i = 0; i < (size_t)w * h * 4; ++i) {
            float v = images.specular[offset * 4 + i];
            CHECK(isfinite(v) && v >= 0 && v < 100);
            if (i % 4 != 3)
                maximum = fmaxf(maximum, v);
        }
        if (level == 0)
            sharp = maximum;
        if (level == AF_IBL_LEVELS - 1)
            rough = maximum;
        offset += (size_t)w * h;
        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
    }
    CHECK(sharp > 5 && rough < sharp * .5f);
    for (size_t i = 0; i < AF_IBL_DIFFUSE_WIDTH * AF_IBL_DIFFUSE_HEIGHT * 4; ++i)
        CHECK(isfinite(images.diffuse[i]) && images.diffuse[i] >= 0);
    for (size_t i = 0; i < AF_IBL_LUT_SIZE * AF_IBL_LUT_SIZE; ++i) {
        CHECK(images.brdf[i * 4] >= 0 && images.brdf[i * 4 + 1] >= 0);
        CHECK(images.brdf[i * 4] + images.brdf[i * 4 + 1] <= 1.05f);
    }
    CHECK(fabsf(af_vec3_length(af_ibl_direction(.17f, .83f)) - 1) < .0001f);
    af_ibl_free(&images);
    CHECK(!images.specular && !images.diffuse && !images.brdf);
    const af_vec3 directions[] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    af_local_light light = af_local_light_default(AF_LIGHT_POINT);
    light.position = (af_vec3){2, 3, 4};
    light.range = 20;
    for (unsigned face = 0; face < 6; ++face) {
        CHECK(af_local_shadow_face(directions[face]) == face);
        af_vec3 p = af_vec3_add(light.position, af_vec3_scale(directions[face], 3));
        af_vec4 clip = af_mat4_transform(af_local_shadow_matrix(light, face), (af_vec4){p.x, p.y, p.z, 1});
        CHECK(clip.w > 0 && fabsf(clip.x / clip.w) < .001f && fabsf(clip.y / clip.w) < .001f && clip.z / clip.w > 0 &&
              clip.z / clip.w < 1);
    }
    light.type = AF_LIGHT_SPOT;
    light.direction = (af_vec3){0, -1, 0};
    af_vec4 p = af_mat4_transform(af_local_shadow_matrix(light, 0), (af_vec4){2, 0, 4, 1});
    CHECK(p.w > 0 && fabsf(p.x / p.w) < .001f && fabsf(p.y / p.w) < .001f);
    puts("Lighting math: GGX filtering, finite/energy-bounded BRDF, six point faces and vertical spot projection "
         "passed.");
    return 0;
}
