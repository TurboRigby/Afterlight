/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef AF_METAL_BINDINGS_H
#define AF_METAL_BINDINGS_H

/* Fixed Metal resource binding indices shared by SPIR-V -> MSL translation
 * (both build-time built-in shaders and runtime custom material shaders)
 * and the Metal command encoders. */

/* Vertex buffer slot for [[stage_in]] vertex descriptors (0..30 allowed). */
#define AF_MTL_VERTEX_BUFFER_INDEX 30u

/* Buffer slots (scene, shadow, sky, custom material, post, ui). */
#define AF_MTL_BUF_PUSH 0u
#define AF_MTL_BUF_FRAME 1u
#define AF_MTL_BUF_SKIN 2u
#define AF_MTL_BUF_MOTION 3u
#define AF_MTL_BUF_ACCEL 4u
#define AF_MTL_BUF_REFLECTION 5u
#define AF_MTL_BUF_LIGHTS 6u
#define AF_MTL_BUF_LIGHT_TILES 7u

/* Texture & sampler slots (1:1 texture(N) and sampler(N)) for scene/shadow/sky shaders. */
#define AF_MTL_TEX_LOCAL_SHADOW 0u
#define AF_MTL_TEX_ENV 1u
#define AF_MTL_TEX_IRRADIANCE 2u
#define AF_MTL_TEX_BRDF 3u
#define AF_MTL_TEX_SKY 4u
#define AF_MTL_TEX_REFRACT 5u
#define AF_MTL_TEX_OPAQUE_DEPTH 6u
#define AF_MTL_TEX_BASE 7u
#define AF_MTL_TEX_SHADOW 8u
#define AF_MTL_TEX_NORMAL 9u
#define AF_MTL_TEX_MR 10u

/* Texture & sampler slots for post/gi_denoise/ui shaders. */
#define AF_MTL_POST_TEX_0 0u
#define AF_MTL_POST_TEX_1 1u
#define AF_MTL_POST_TEX_2 2u

/* Compute slots for cluster_icb.metal. */
#define AF_MTL_CLUSTER_BUF_DISPATCH 0u
#define AF_MTL_CLUSTER_BUF_CANDIDATES 1u
#define AF_MTL_CLUSTER_BUF_TRANSFORMS 2u
#define AF_MTL_CLUSTER_BUF_STATS 3u
#define AF_MTL_CLUSTER_BUF_ICB_ARGS 4u

#endif
