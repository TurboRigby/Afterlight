/* SPDX-License-Identifier: MPL-2.0 */
#ifndef AF_IBL_H
#define AF_IBL_H
#include <afterlight/math.h>
#include <stdbool.h>
#include <stdint.h>
#define AF_IBL_WIDTH 128u
#define AF_IBL_HEIGHT 64u
#define AF_IBL_LEVELS 6u
#define AF_IBL_DIFFUSE_WIDTH 32u
#define AF_IBL_DIFFUSE_HEIGHT 16u
#define AF_IBL_LUT_SIZE 32u
typedef struct af_ibl_images {float *specular,*diffuse,*brdf;} af_ibl_images;
/* Original HDR studio/sky radiance, cosine convolution, GGX prefilter and
   split-sum BRDF integration. CPU-only and independent of Vulkan. */
af_vec3 af_ibl_radiance(af_vec3 direction,uint32_t profile);
af_vec3 af_ibl_direction(float u,float v);
bool af_ibl_bake(uint32_t profile,af_ibl_images *images);
void af_ibl_free(af_ibl_images *images);
#endif
