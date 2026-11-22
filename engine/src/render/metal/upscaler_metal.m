/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "metal_internal.h"
#import <MetalFX/MetalFX.h>

static const char *const k_metalfx_quality_names[] = {
    "MetalFX Quality",
    "MetalFX Balanced",
    "MetalFX Performance",
    "MetalFX Native AA",
};
static const float k_metalfx_quality_scales[] = {
    0.6667f,
    0.5882f,
    0.5000f,
    1.0000f,
};

struct af_mtl_upscaler {
    id<MTLDevice> device;
    id<MTLFXTemporalScaler> temporal;
    id<MTLFXSpatialScaler> spatial;
    uint32_t render_w, render_h, out_w, out_h;
    bool supports_temporal, supports_spatial, use_temporal;
};

void af_mtl_upscaler_destroy(af_mtl_upscaler *u) {
    if (!u)
        return;
    AF_MTL_RELEASE(u->temporal);
    AF_MTL_RELEASE(u->spatial);
    free(u);
}

af_mtl_upscaler *af_mtl_upscaler_create(id<MTLDevice> device) {
    if (!device)
        return NULL;
    bool has_temporal = false;
    bool has_spatial = false;
    if (@available(macOS 13.0, *)) {
        has_temporal = [MTLFXTemporalScalerDescriptor supportsDevice:device];
        has_spatial = [MTLFXSpatialScalerDescriptor supportsDevice:device];
    }
    if (!has_temporal && !has_spatial)
        return NULL;
    af_mtl_upscaler *u = calloc(1, sizeof(*u));
    if (!u)
        return NULL;
    u->device = device;
    u->supports_temporal = has_temporal;
    u->supports_spatial = has_spatial;
    return u;
}

bool af_mtl_upscaler_configure(af_mtl_upscaler *u, uint32_t render_w, uint32_t render_h, uint32_t out_w, uint32_t out_h,
                               bool temporal) {
    if (!u || !render_w || !render_h || !out_w || !out_h)
        return false;
    AF_MTL_RELEASE(u->temporal);
    AF_MTL_RELEASE(u->spatial);
    u->render_w = render_w;
    u->render_h = render_h;
    u->out_w = out_w;
    u->out_h = out_h;
    u->use_temporal = temporal && u->supports_temporal;

    @autoreleasepool {
        if (@available(macOS 13.0, *)) {
            if (u->use_temporal) {
                MTLFXTemporalScalerDescriptor *desc = [[MTLFXTemporalScalerDescriptor alloc] init];
                desc.inputWidth = render_w;
                desc.inputHeight = render_h;
                desc.outputWidth = out_w;
                desc.outputHeight = out_h;
                desc.colorTextureFormat = MTLPixelFormatRGBA16Float;
                desc.depthTextureFormat = MTLPixelFormatDepth32Float;
                desc.motionTextureFormat = MTLPixelFormatRGBA16Float;
                desc.outputTextureFormat = MTLPixelFormatRGBA16Float;
                desc.autoExposureEnabled = NO;
                desc.inputContentPropertiesEnabled = NO;
                u->temporal = [desc newTemporalScalerWithDevice:u->device];
                [desc release];
                if (u->temporal) {
                    u->temporal.depthReversed = NO;
                    u->temporal.preExposure = 1.0f;
                    return true;
                }
                u->use_temporal = false;
            }
            if (u->supports_spatial) {
                MTLFXSpatialScalerDescriptor *desc = [[MTLFXSpatialScalerDescriptor alloc] init];
                desc.inputWidth = render_w;
                desc.inputHeight = render_h;
                desc.outputWidth = out_w;
                desc.outputHeight = out_h;
                desc.colorTextureFormat = MTLPixelFormatRGBA16Float;
                desc.outputTextureFormat = MTLPixelFormatRGBA16Float;
                desc.colorProcessingMode = MTLFXSpatialScalerColorProcessingModeHDR;
                u->spatial = [desc newSpatialScalerWithDevice:u->device];
                [desc release];
                return u->spatial != nil;
            }
        }
    }
    return false;
}

void af_mtl_upscaler_reset(af_mtl_upscaler *u) {
    (void)u;
}

bool af_mtl_upscaler_encode(af_mtl_upscaler *u, id<MTLCommandBuffer> cmd, id<MTLTexture> color, id<MTLTexture> depth,
                            id<MTLTexture> motion, id<MTLTexture> output, float jitter_px_x, float jitter_px_y,
                            bool reset_history) {
    if (!u || !cmd || !color || !output)
        return false;
    if (@available(macOS 13.0, *)) {
        if (u->use_temporal && u->temporal && depth && motion) {
            u->temporal.colorTexture = color;
            u->temporal.depthTexture = depth;
            u->temporal.motionTexture = motion;
            u->temporal.outputTexture = output;
            u->temporal.inputContentWidth = u->render_w;
            u->temporal.inputContentHeight = u->render_h;
            u->temporal.jitterOffsetX = jitter_px_x;
            u->temporal.jitterOffsetY = jitter_px_y;
            /* Afterlight's motion vectors are normalized UV deltas (current -> previous). */
            u->temporal.motionVectorScaleX = (float)u->render_w;
            u->temporal.motionVectorScaleY = (float)u->render_h;
            u->temporal.reset = reset_history ? YES : NO;
            [u->temporal encodeToCommandBuffer:cmd];
            return true;
        }
        if (u->spatial) {
            u->spatial.colorTexture = color;
            u->spatial.outputTexture = output;
            u->spatial.inputContentWidth = u->render_w;
            u->spatial.inputContentHeight = u->render_h;
            [u->spatial encodeToCommandBuffer:cmd];
            return true;
        }
    }
    return false;
}

uint32_t af_mtl_upscaler_quality_count(const af_mtl_upscaler *u) {
    return u ? (uint32_t)(sizeof(k_metalfx_quality_names) / sizeof(k_metalfx_quality_names[0])) : 0u;
}

const char *af_mtl_upscaler_quality_name(const af_mtl_upscaler *u, uint32_t index) {
    if (!u || index >= af_mtl_upscaler_quality_count(u))
        return NULL;
    return k_metalfx_quality_names[index];
}

float af_mtl_upscaler_quality_scale(const af_mtl_upscaler *u, uint32_t index) {
    if (!u || index >= af_mtl_upscaler_quality_count(u))
        return 1.0f;
    return k_metalfx_quality_scales[index];
}

bool af_mtl_upscaler_is_spatial(const af_mtl_upscaler *u) {
    return u && !u->use_temporal;
}
