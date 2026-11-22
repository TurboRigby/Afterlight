/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "metal_internal.h"

#define AF_MTL_SHADOW_LAYER_MAX 24u

struct af_mtl_shadows {
    uint64_t allocation_bytes;
    uint32_t resolution, layers;
    id<MTLRenderPipelineState> pipeline;
    id<MTLDepthStencilState> depth_state;
    id<MTLSamplerState> sampler;
    id<MTLTexture> textures[AF_FRAMES_IN_FLIGHT];
};

void af_mtl_shadows_destroy(af_mtl_shadows *s) {
    if (!s)
        return;
    for (unsigned f = 0; f < AF_FRAMES_IN_FLIGHT; ++f)
        AF_MTL_RELEASE(s->textures[f]);
    AF_MTL_RELEASE(s->sampler);
    AF_MTL_RELEASE(s->depth_state);
    AF_MTL_RELEASE(s->pipeline);
    free(s);
}

uint64_t af_mtl_shadows_allocation_bytes(const af_mtl_shadows *s) {
    return s ? s->allocation_bytes : 0;
}

id<MTLTexture> af_mtl_shadows_texture(const af_mtl_shadows *s, uint32_t frame) {
    return s ? s->textures[frame % AF_FRAMES_IN_FLIGHT] : nil;
}

id<MTLSamplerState> af_mtl_shadows_sampler(const af_mtl_shadows *s) {
    return s ? s->sampler : nil;
}

af_mtl_shadows *af_mtl_shadows_create(id<MTLDevice> device, id<MTLFunction> vert_fn, id<MTLFunction> frag_fn,
                                      uint32_t resolution, uint32_t layers) {
    if (!device || !vert_fn || !frag_fn || !resolution || !layers || layers > AF_MTL_SHADOW_LAYER_MAX)
        return NULL;

    af_mtl_shadows *s = calloc(1, sizeof(*s));
    if (!s)
        return NULL;
    s->resolution = resolution;
    s->layers = layers;

    @autoreleasepool {
        MTLVertexDescriptor *vdesc = [MTLVertexDescriptor vertexDescriptor];
        vdesc.attributes[0].format = MTLVertexFormatFloat3;
        vdesc.attributes[0].offset = offsetof(af_vertex, position);
        vdesc.attributes[0].bufferIndex = AF_MTL_VERTEX_BUFFER_INDEX;

        vdesc.attributes[1].format = MTLVertexFormatFloat4;
        vdesc.attributes[1].offset = offsetof(af_vertex, color);
        vdesc.attributes[1].bufferIndex = AF_MTL_VERTEX_BUFFER_INDEX;

        vdesc.attributes[3].format = MTLVertexFormatFloat2;
        vdesc.attributes[3].offset = offsetof(af_vertex, uv);
        vdesc.attributes[3].bufferIndex = AF_MTL_VERTEX_BUFFER_INDEX;

        vdesc.attributes[4].format = MTLVertexFormatUShort4;
        vdesc.attributes[4].offset = offsetof(af_vertex, joints);
        vdesc.attributes[4].bufferIndex = AF_MTL_VERTEX_BUFFER_INDEX;

        vdesc.attributes[5].format = MTLVertexFormatFloat4;
        vdesc.attributes[5].offset = offsetof(af_vertex, weights);
        vdesc.attributes[5].bufferIndex = AF_MTL_VERTEX_BUFFER_INDEX;

        vdesc.layouts[AF_MTL_VERTEX_BUFFER_INDEX].stride = sizeof(af_vertex);
        vdesc.layouts[AF_MTL_VERTEX_BUFFER_INDEX].stepFunction = MTLVertexStepFunctionPerVertex;

        MTLRenderPipelineDescriptor *pdesc = [[MTLRenderPipelineDescriptor alloc] init];
        pdesc.label = @"Afterlight Shadow";
        pdesc.vertexFunction = vert_fn;
        pdesc.fragmentFunction = frag_fn;
        pdesc.vertexDescriptor = vdesc;
        pdesc.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;

        NSError *err = nil;
        s->pipeline = [device newRenderPipelineStateWithDescriptor:pdesc error:&err];
        [pdesc release];
        if (!s->pipeline) {
            af_log("error", "Metal shadow pipeline failed: %s", err ? err.localizedDescription.UTF8String : "unknown");
            goto fail;
        }

        MTLDepthStencilDescriptor *dsdesc = [[MTLDepthStencilDescriptor alloc] init];
        dsdesc.depthCompareFunction = MTLCompareFunctionLess;
        dsdesc.depthWriteEnabled = YES;
        s->depth_state = [device newDepthStencilStateWithDescriptor:dsdesc];
        [dsdesc release];
        if (!s->depth_state)
            goto fail;

        MTLSamplerDescriptor *sdesc = [[MTLSamplerDescriptor alloc] init];
        sdesc.minFilter = MTLSamplerMinMagFilterNearest;
        sdesc.magFilter = MTLSamplerMinMagFilterNearest;
        sdesc.mipFilter = MTLSamplerMipFilterNotMipmapped;
        sdesc.sAddressMode = MTLSamplerAddressModeClampToBorderColor;
        sdesc.tAddressMode = MTLSamplerAddressModeClampToBorderColor;
        sdesc.rAddressMode = MTLSamplerAddressModeClampToBorderColor;
        sdesc.borderColor = MTLSamplerBorderColorOpaqueWhite;
        s->sampler = [device newSamplerStateWithDescriptor:sdesc];
        [sdesc release];
        if (!s->sampler)
            goto fail;

        MTLTextureDescriptor *tdesc = [[MTLTextureDescriptor alloc] init];
        tdesc.textureType = MTLTextureType2DArray;
        tdesc.pixelFormat = MTLPixelFormatDepth32Float;
        tdesc.width = resolution;
        tdesc.height = resolution;
        tdesc.mipmapLevelCount = 1;
        tdesc.arrayLength = layers;
        tdesc.storageMode = MTLStorageModePrivate;
        tdesc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;

        for (unsigned f = 0; f < AF_FRAMES_IN_FLIGHT; ++f) {
            s->textures[f] = [device newTextureWithDescriptor:tdesc];
            if (!s->textures[f]) {
                [tdesc release];
                af_log("error", "Metal shadow depth array allocation failed (%ux%u x %u).", resolution, resolution,
                       layers);
                goto fail;
            }
            uint64_t bytes = (uint64_t)[s->textures[f] allocatedSize];
            s->allocation_bytes += bytes ? bytes : (uint64_t)resolution * resolution * layers * 4u;
        }
        [tdesc release];
    }

    return s;

fail:
    af_mtl_shadows_destroy(s);
    return NULL;
}

id<MTLRenderCommandEncoder> af_mtl_shadows_begin(af_mtl_shadows *s, id<MTLCommandBuffer> cmd, uint32_t frame,
                                                 uint32_t layer, af_shadow_settings settings,
                                                 id<MTLBuffer> skin_buffer) {
    MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
    rp.depthAttachment.texture = s->textures[frame % AF_FRAMES_IN_FLIGHT];
    rp.depthAttachment.slice = layer;
    rp.depthAttachment.level = 0;
    rp.depthAttachment.loadAction = MTLLoadActionClear;
    rp.depthAttachment.storeAction = MTLStoreActionStore;
    rp.depthAttachment.clearDepth = 1.0;

    id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:rp];
    if (!enc)
        return nil;

    MTLViewport vp = {0.0, 0.0, (double)s->resolution, (double)s->resolution, 0.0, 1.0};
    MTLScissorRect sc = {0, 0, s->resolution, s->resolution};
    [enc setViewport:vp];
    [enc setScissorRect:sc];
    [enc setRenderPipelineState:s->pipeline];
    [enc setDepthStencilState:s->depth_state];
    [enc setCullMode:MTLCullModeNone];
    [enc setFrontFacingWinding:MTLWindingCounterClockwise];
    [enc setDepthBias:settings.constant_bias slopeScale:settings.slope_bias clamp:0.0f];
    if (skin_buffer)
        [enc setVertexBuffer:skin_buffer offset:0 atIndex:AF_MTL_BUF_SKIN];
    return enc;
}

void af_mtl_shadows_push(id<MTLRenderCommandEncoder> enc, const float *mvp, uint32_t skin_base, uint32_t material_flags,
                         float alpha, float cutoff, const af_mtl_texture *texture) {
    struct {
        float mvp[16];
        uint32_t skin[4];
    } push = {.skin = {skin_base, material_flags, 0, 0}};
    memcpy(&push.skin[2], &alpha, 4);
    push.skin[3] = (uint32_t)(cutoff * 65535.0f + 0.5f);
    memcpy(push.mvp, mvp, 64);
    [enc setVertexBytes:&push length:sizeof(push) atIndex:AF_MTL_BUF_PUSH];
    [enc setFragmentBytes:&push length:sizeof(push) atIndex:AF_MTL_BUF_PUSH];
    if (texture) {
        [enc setFragmentTexture:af_mtl_texture_handle(texture) atIndex:AF_MTL_TEX_BASE];
        [enc setFragmentSamplerState:af_mtl_texture_sampler(texture) atIndex:AF_MTL_TEX_BASE];
    }
}
