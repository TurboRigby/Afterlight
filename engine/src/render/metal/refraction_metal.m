/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "metal_internal.h"

typedef struct mtl_refract_frame {
    id<MTLTexture> color;
    id<MTLTexture> depth;
} mtl_refract_frame;

struct af_mtl_refraction {
    uint64_t allocation_bytes;
    uint32_t width, height, count;
    id<MTLSamplerState> color_sampler;
    id<MTLSamplerState> depth_sampler;
    mtl_refract_frame *frames;
};

void af_mtl_refraction_destroy(af_mtl_refraction *r) {
    if (!r)
        return;
    for (uint32_t i = 0; r->frames && i < r->count; ++i) {
        AF_MTL_RELEASE(r->frames[i].color);
        AF_MTL_RELEASE(r->frames[i].depth);
    }
    AF_MTL_RELEASE(r->color_sampler);
    AF_MTL_RELEASE(r->depth_sampler);
    free(r->frames);
    free(r);
}

uint64_t af_mtl_refraction_allocation_bytes(const af_mtl_refraction *r) {
    return r ? r->allocation_bytes : 0;
}

id<MTLTexture> af_mtl_refraction_color(const af_mtl_refraction *r, uint32_t frame) {
    return (r && r->frames) ? r->frames[frame % r->count].color : nil;
}

id<MTLTexture> af_mtl_refraction_depth(const af_mtl_refraction *r, uint32_t frame) {
    return (r && r->frames) ? r->frames[frame % r->count].depth : nil;
}

id<MTLSamplerState> af_mtl_refraction_color_sampler(const af_mtl_refraction *r) {
    return r ? r->color_sampler : nil;
}

id<MTLSamplerState> af_mtl_refraction_depth_sampler(const af_mtl_refraction *r) {
    return r ? r->depth_sampler : nil;
}

af_mtl_refraction *af_mtl_refraction_create(id<MTLDevice> device, uint32_t width, uint32_t height, uint32_t count) {
    if (!device || !width || !height || !count)
        return NULL;

    af_mtl_refraction *r = calloc(1, sizeof(*r));
    if (!r)
        return NULL;
    r->width = width;
    r->height = height;
    r->count = count;
    r->frames = calloc(count, sizeof(*r->frames));
    if (!r->frames)
        goto fail;

    @autoreleasepool {
        MTLSamplerDescriptor *sdesc = [[MTLSamplerDescriptor alloc] init];
        sdesc.minFilter = MTLSamplerMinMagFilterLinear;
        sdesc.magFilter = MTLSamplerMinMagFilterLinear;
        sdesc.mipFilter = MTLSamplerMipFilterNotMipmapped;
        sdesc.sAddressMode = MTLSamplerAddressModeClampToEdge;
        sdesc.tAddressMode = MTLSamplerAddressModeClampToEdge;
        sdesc.rAddressMode = MTLSamplerAddressModeClampToEdge;
        r->color_sampler = [device newSamplerStateWithDescriptor:sdesc];

        sdesc.minFilter = MTLSamplerMinMagFilterNearest;
        sdesc.magFilter = MTLSamplerMinMagFilterNearest;
        r->depth_sampler = [device newSamplerStateWithDescriptor:sdesc];
        [sdesc release];
        if (!r->color_sampler || !r->depth_sampler)
            goto fail;

        MTLTextureDescriptor *cdesc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float
                                                                                         width:width
                                                                                        height:height
                                                                                     mipmapped:NO];
        cdesc.storageMode = MTLStorageModePrivate;
        cdesc.usage = MTLTextureUsageShaderRead;

        MTLTextureDescriptor *ddesc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                                                                         width:width
                                                                                        height:height
                                                                                     mipmapped:NO];
        ddesc.storageMode = MTLStorageModePrivate;
        ddesc.usage = MTLTextureUsageShaderRead;

        for (uint32_t i = 0; i < count; ++i) {
            r->frames[i].color = [device newTextureWithDescriptor:cdesc];
            r->frames[i].depth = [device newTextureWithDescriptor:ddesc];
            if (!r->frames[i].color || !r->frames[i].depth)
                goto fail;
            uint64_t cb = (uint64_t)[r->frames[i].color allocatedSize];
            uint64_t db = (uint64_t)[r->frames[i].depth allocatedSize];
            r->allocation_bytes += (cb ? cb : (uint64_t)width * height * 8u) + (db ? db : (uint64_t)width * height * 4u);
        }
    }

    return r;

fail:
    af_mtl_refraction_destroy(r);
    return NULL;
}

void af_mtl_refraction_copy(af_mtl_refraction *r, id<MTLCommandBuffer> cmd, uint32_t frame, id<MTLTexture> color,
                            id<MTLTexture> depth) {
    if (!r || !cmd || !color || !depth)
        return;
    mtl_refract_frame *f = &r->frames[frame % r->count];
    id<MTLBlitCommandEncoder> blit = [cmd blitCommandEncoder];
    if (!blit)
        return;
    MTLSize size = MTLSizeMake(r->width, r->height, 1);
    MTLOrigin zero = MTLOriginMake(0, 0, 0);
    [blit copyFromTexture:color
              sourceSlice:0
              sourceLevel:0
             sourceOrigin:zero
               sourceSize:size
                toTexture:f->color
         destinationSlice:0
         destinationLevel:0
        destinationOrigin:zero];
    [blit copyFromTexture:depth
              sourceSlice:0
              sourceLevel:0
             sourceOrigin:zero
               sourceSize:size
                toTexture:f->depth
         destinationSlice:0
         destinationLevel:0
        destinationOrigin:zero];
    [blit endEncoding];
}
