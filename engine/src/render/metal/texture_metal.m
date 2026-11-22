/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "metal_internal.h"
#include "assets/mipmap.h"

struct af_mtl_texture {
    af_texture_info info;
    id<MTLTexture> texture;
    id<MTLSamplerState> sampler;
};

void af_mtl_texture_destroy(af_mtl_texture *t) {
    if (!t)
        return;
    AF_MTL_RELEASE(t->sampler);
    AF_MTL_RELEASE(t->texture);
    free(t);
}

af_texture_info af_mtl_texture_info(const af_mtl_texture *t) {
    return t ? t->info : (af_texture_info){0};
}

id<MTLTexture> af_mtl_texture_handle(const af_mtl_texture *t) {
    return t ? t->texture : nil;
}

id<MTLSamplerState> af_mtl_texture_sampler(const af_mtl_texture *t) {
    return t ? t->sampler : nil;
}

static af_mtl_texture *create_texture(id<MTLDevice> device, const void *rgba, uint32_t width, uint32_t height,
                                      bool srgb, bool repeat, float anisotropy, uint32_t hdr_levels) {
    if (!device || !rgba || !width || !height || width > 8192 || height > 8192)
        return NULL;

    af_mtl_texture *t = calloc(1, sizeof(*t));
    if (!t)
        return NULL;

    af_mip_chain mips = {0};
    MTLPixelFormat format;
    size_t bpp;

    if (hdr_levels) {
        if (hdr_levels > AF_MAX_MIPS) {
            free(t);
            return NULL;
        }
        format = MTLPixelFormatRGBA16Float;
        bpp = 8;
        mips.count = hdr_levels;
        uint32_t w = width, h = height;
        for (uint32_t i = 0; i < hdr_levels; ++i) {
            mips.widths[i] = w;
            mips.heights[i] = h;
            mips.offsets[i] = mips.bytes;
            mips.bytes += (size_t)w * h * bpp;
            w = w > 1 ? w / 2 : 1;
            h = h > 1 ? h / 2 : 1;
        }
        mips.pixels = malloc(mips.bytes);
        if (!mips.pixels || !af_rc_rgba32f_to_rgba16f((const float *)rgba, (uint16_t *)mips.pixels, mips.bytes / 2)) {
            free(mips.pixels);
            free(t);
            return NULL;
        }
    } else {
        format = srgb ? MTLPixelFormatRGBA8Unorm_sRGB : MTLPixelFormatRGBA8Unorm;
        bpp = 4;
        if (!af_mip_chain_build((const unsigned char *)rgba, width, height, srgb, repeat, &mips)) {
            free(t);
            return NULL;
        }
    }

    @autoreleasepool {
        MTLTextureDescriptor *desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format
                                                                                        width:width
                                                                                       height:height
                                                                                    mipmapped:(mips.count > 1)];
        desc.mipmapLevelCount = mips.count;
        desc.usage = MTLTextureUsageShaderRead;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        desc.storageMode = [device hasUnifiedMemory] ? MTLStorageModeShared : MTLStorageModeManaged;
#pragma clang diagnostic pop

        t->texture = [device newTextureWithDescriptor:desc];
        if (!t->texture) {
            af_log("error", "Metal texture creation failed (%ux%u).", width, height);
            goto fail;
        }

        for (uint32_t level = 0; level < mips.count; ++level) {
            MTLRegion region = MTLRegionMake2D(0, 0, mips.widths[level], mips.heights[level]);
            [t->texture replaceRegion:region
                          mipmapLevel:level
                            withBytes:mips.pixels + mips.offsets[level]
                          bytesPerRow:(NSUInteger)mips.widths[level] * bpp];
        }

        MTLSamplerDescriptor *sdesc = [[MTLSamplerDescriptor alloc] init];
        sdesc.minFilter = MTLSamplerMinMagFilterLinear;
        sdesc.magFilter = MTLSamplerMinMagFilterLinear;
        sdesc.mipFilter = mips.count > 1 ? MTLSamplerMipFilterLinear : MTLSamplerMipFilterNotMipmapped;
        sdesc.lodMinClamp = 0.0f;
        sdesc.lodMaxClamp = (float)(mips.count - 1);
        NSUInteger max_aniso = (repeat && anisotropy > 1.0f) ? (NSUInteger)anisotropy : 1u;
        if (max_aniso > 16u)
            max_aniso = 16u;
        sdesc.maxAnisotropy = max_aniso;
        sdesc.sAddressMode = repeat ? MTLSamplerAddressModeRepeat : MTLSamplerAddressModeClampToEdge;
        sdesc.tAddressMode = (repeat && !hdr_levels) ? MTLSamplerAddressModeRepeat : MTLSamplerAddressModeClampToEdge;
        sdesc.rAddressMode = repeat ? MTLSamplerAddressModeRepeat : MTLSamplerAddressModeClampToEdge;
        t->sampler = [device newSamplerStateWithDescriptor:sdesc];
        [sdesc release];
        if (!t->sampler) {
            af_log("error", "Metal sampler creation failed.");
            goto fail;
        }

        uint64_t alloc_bytes = (uint64_t)[t->texture allocatedSize];
        if (alloc_bytes == 0)
            alloc_bytes = (uint64_t)mips.bytes;
        t->info = (af_texture_info){.width = width,
                                    .height = height,
                                    .mip_levels = mips.count,
                                    .allocation_bytes = alloc_bytes,
                                    .anisotropy = repeat ? (float)max_aniso : 1.0f,
                                    .srgb = srgb};
    }

    free(mips.pixels);
    return t;

fail:
    free(mips.pixels);
    af_mtl_texture_destroy(t);
    return NULL;
}

af_mtl_texture *af_mtl_texture_create(id<MTLDevice> device, const unsigned char *rgba, uint32_t width, uint32_t height,
                                      bool srgb, bool repeat, float anisotropy) {
    return create_texture(device, rgba, width, height, srgb, repeat, anisotropy, 0);
}

af_mtl_texture *af_mtl_texture_create_hdr(id<MTLDevice> device, const float *rgba, uint32_t width, uint32_t height,
                                          uint32_t levels, bool wrap_u) {
    return create_texture(device, rgba, width, height, false, wrap_u, 1.0f, levels);
}
