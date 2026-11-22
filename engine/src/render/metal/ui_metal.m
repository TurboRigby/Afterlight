/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "metal_internal.h"

typedef struct mtl_ui_frame {
    id<MTLBuffer> vertices;
    uint64_t vertex_capacity;
    id<MTLBuffer> indices;
    uint64_t index_capacity;
    af_mtl_texture *retired[128];
    size_t retired_count;
} mtl_ui_frame;

struct af_mtl_ui {
    id<MTLDevice> device;
    af_mtl_texture *atlas;
    struct {
        uint32_t id;
        af_mtl_texture *texture;
        unsigned char *pending;
        uint32_t width, height;
        bool remove;
    } images[128];
    size_t image_count;
    id<MTLRenderPipelineState> pipeline;
    id<MTLDepthStencilState> depth_state;
    mtl_ui_frame frames[AF_FRAMES_IN_FLIGHT];
};

static bool reserve_ui_buffer(id<MTLDevice> device, id<MTLBuffer> *buf, uint64_t *capacity, uint64_t size) {
    if (*capacity >= size && *buf != nil)
        return true;
    AF_MTL_RELEASE(*buf);
    *capacity = 0;
    uint64_t cap = size < 65536u ? 65536u : size * 2u;
    *buf = [device newBufferWithLength:(NSUInteger)cap options:MTLResourceStorageModeShared];
    if (!*buf)
        return false;
    *capacity = cap;
    return true;
}

void af_mtl_ui_destroy(af_mtl_ui *ui) {
    if (!ui)
        return;
    for (unsigned f = 0; f < AF_FRAMES_IN_FLIGHT; ++f) {
        for (size_t j = 0; j < ui->frames[f].retired_count; ++j)
            af_mtl_texture_destroy(ui->frames[f].retired[j]);
        AF_MTL_RELEASE(ui->frames[f].vertices);
        AF_MTL_RELEASE(ui->frames[f].indices);
    }
    for (size_t i = 0; i < ui->image_count; ++i) {
        free(ui->images[i].pending);
        af_mtl_texture_destroy(ui->images[i].texture);
    }
    af_mtl_texture_destroy(ui->atlas);
    AF_MTL_RELEASE(ui->depth_state);
    AF_MTL_RELEASE(ui->pipeline);
    free(ui);
}

af_mtl_ui *af_mtl_ui_create(id<MTLDevice> device, id<MTLFunction> vert_fn, id<MTLFunction> frag_fn,
                            const unsigned char *rgba, uint32_t width, uint32_t height) {
    if (!device || !vert_fn || !frag_fn)
        return NULL;
    af_mtl_ui *ui = calloc(1, sizeof(*ui));
    if (!ui)
        return NULL;
    ui->device = device;
    ui->atlas = af_mtl_texture_create(device, rgba, width, height, false, false, 1.0f);
    if (!ui->atlas)
        goto fail;

    @autoreleasepool {
        MTLVertexDescriptor *vdesc = [MTLVertexDescriptor vertexDescriptor];
        vdesc.attributes[0].format = MTLVertexFormatFloat2;
        vdesc.attributes[0].offset = offsetof(af_overlay_vertex, position);
        vdesc.attributes[0].bufferIndex = AF_MTL_VERTEX_BUFFER_INDEX;

        vdesc.attributes[1].format = MTLVertexFormatFloat2;
        vdesc.attributes[1].offset = offsetof(af_overlay_vertex, uv);
        vdesc.attributes[1].bufferIndex = AF_MTL_VERTEX_BUFFER_INDEX;

        vdesc.attributes[2].format = MTLVertexFormatUChar4Normalized;
        vdesc.attributes[2].offset = offsetof(af_overlay_vertex, color);
        vdesc.attributes[2].bufferIndex = AF_MTL_VERTEX_BUFFER_INDEX;

        vdesc.layouts[AF_MTL_VERTEX_BUFFER_INDEX].stride = sizeof(af_overlay_vertex);
        vdesc.layouts[AF_MTL_VERTEX_BUFFER_INDEX].stepFunction = MTLVertexStepFunctionPerVertex;

        MTLRenderPipelineDescriptor *pdesc = [[MTLRenderPipelineDescriptor alloc] init];
        pdesc.label = @"Afterlight UI";
        pdesc.vertexFunction = vert_fn;
        pdesc.fragmentFunction = frag_fn;
        pdesc.vertexDescriptor = vdesc;
        pdesc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm_sRGB;
        pdesc.colorAttachments[0].blendingEnabled = YES;
        pdesc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
        pdesc.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        pdesc.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
        pdesc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
        pdesc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        pdesc.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;

        NSError *err = nil;
        ui->pipeline = [device newRenderPipelineStateWithDescriptor:pdesc error:&err];
        [pdesc release];
        if (!ui->pipeline) {
            af_log("error", "Metal UI pipeline failed: %s", err ? err.localizedDescription.UTF8String : "unknown");
            goto fail;
        }

        MTLDepthStencilDescriptor *dsdesc = [[MTLDepthStencilDescriptor alloc] init];
        dsdesc.depthCompareFunction = MTLCompareFunctionAlways;
        dsdesc.depthWriteEnabled = NO;
        ui->depth_state = [device newDepthStencilStateWithDescriptor:dsdesc];
        [dsdesc release];
        if (!ui->depth_state)
            goto fail;
    }

    return ui;

fail:
    af_mtl_ui_destroy(ui);
    return NULL;
}

bool af_mtl_ui_upload_texture(af_mtl_ui *ui, uint32_t id, const unsigned char *rgba, uint32_t width, uint32_t height) {
    if (!ui || id < 2 || !rgba || !width || !height || width > 8192 || height > 8192)
        return false;
    size_t index = 0;
    while (index < ui->image_count && ui->images[index].id != id)
        ++index;
    if (index == 128)
        return false;
    size_t size = (size_t)width * height * 4, total = 0;
    for (size_t i = 0; i < ui->image_count; ++i)
        if (i != index && ui->images[i].pending)
            total += (size_t)ui->images[i].width * ui->images[i].height * 4;
    if (size > 64u * 1024u * 1024u || total > 64u * 1024u * 1024u - size)
        return false;
    unsigned char *copy = malloc(size);
    if (!copy)
        return false;
    memcpy(copy, rgba, size);
    if (index == ui->image_count) {
        memset(&ui->images[index], 0, sizeof(ui->images[index]));
        ++ui->image_count;
    }
    free(ui->images[index].pending);
    ui->images[index].id = id;
    ui->images[index].pending = copy;
    ui->images[index].width = width;
    ui->images[index].height = height;
    ui->images[index].remove = false;
    return true;
}

bool af_mtl_ui_remove_texture(af_mtl_ui *ui, uint32_t id) {
    if (!ui || id < 2)
        return false;
    for (size_t i = 0; i < ui->image_count; ++i)
        if (ui->images[i].id == id) {
            free(ui->images[i].pending);
            ui->images[i].pending = NULL;
            ui->images[i].remove = true;
            return true;
        }
    return false;
}

void af_mtl_ui_process_uploads(af_mtl_ui *ui, uint32_t frame_index) {
    if (!ui)
        return;
    mtl_ui_frame *frame = &ui->frames[frame_index % AF_FRAMES_IN_FLIGHT];
    for (size_t i = 0; i < frame->retired_count; ++i)
        af_mtl_texture_destroy(frame->retired[i]);
    frame->retired_count = 0;
    bool uploaded = false;
    for (size_t i = 0; i < ui->image_count;) {
        if (ui->images[i].remove) {
            if (ui->images[i].texture)
                frame->retired[frame->retired_count++] = ui->images[i].texture;
            ui->images[i] = ui->images[--ui->image_count];
            continue;
        }
        if (!uploaded && ui->images[i].pending) {
            af_mtl_texture *tex = af_mtl_texture_create(ui->device, ui->images[i].pending, ui->images[i].width,
                                                        ui->images[i].height, false, false, 1.0f);
            if (tex) {
                if (ui->images[i].texture)
                    frame->retired[frame->retired_count++] = ui->images[i].texture;
                ui->images[i].texture = tex;
            }
            free(ui->images[i].pending);
            ui->images[i].pending = NULL;
            uploaded = true;
        }
        ++i;
    }
}

bool af_mtl_ui_prepare(af_mtl_ui *ui, uint32_t frame_index, const af_overlay_frame *frame) {
    if (!ui || !frame->vertex_count || !frame->index_count)
        return true;
    mtl_ui_frame *bufs = &ui->frames[frame_index % AF_FRAMES_IN_FLIGHT];
    uint64_t vbytes = (uint64_t)frame->vertex_count * sizeof(*frame->vertices);
    uint64_t ibytes = (uint64_t)frame->index_count * sizeof(*frame->indices);
    if (!reserve_ui_buffer(ui->device, &bufs->vertices, &bufs->vertex_capacity, vbytes) ||
        !reserve_ui_buffer(ui->device, &bufs->indices, &bufs->index_capacity, ibytes))
        return false;
    memcpy([bufs->vertices contents], frame->vertices, (size_t)vbytes);
    memcpy([bufs->indices contents], frame->indices, (size_t)ibytes);
    return true;
}

void af_mtl_ui_draw(af_mtl_ui *ui, id<MTLRenderCommandEncoder> enc, uint32_t frame_index, const af_overlay_frame *frame,
                    uint32_t extent_w, uint32_t extent_h) {
    if (!ui || !enc || !frame || !frame->index_count || !ui->pipeline)
        return;
    const mtl_ui_frame *bufs = &ui->frames[frame_index % AF_FRAMES_IN_FLIGHT];
    if (!bufs->vertices || !bufs->indices)
        return;

    MTLViewport vp = {0.0, 0.0, (double)extent_w, (double)extent_h, 0.0, 1.0};
    [enc setViewport:vp];
    [enc setRenderPipelineState:ui->pipeline];
    [enc setDepthStencilState:ui->depth_state];
    [enc setCullMode:MTLCullModeNone];
    float screen[2] = {frame->width, frame->height};
    [enc setVertexBytes:screen length:sizeof(screen) atIndex:AF_MTL_BUF_PUSH];
    [enc setVertexBuffer:bufs->vertices offset:0 atIndex:AF_MTL_VERTEX_BUFFER_INDEX];

    const af_mtl_texture *bound = ui->atlas;
    [enc setFragmentTexture:af_mtl_texture_handle(bound) atIndex:AF_MTL_POST_TEX_0];
    [enc setFragmentSamplerState:af_mtl_texture_sampler(bound) atIndex:AF_MTL_POST_TEX_0];

    float sx = (float)extent_w / frame->width;
    float sy = (float)extent_h / frame->height;
    for (uint32_t i = 0; i < frame->command_count; ++i) {
        const af_overlay_command *draw = &frame->commands[i];
        float x0 = fmaxf(0.0f, fminf((float)extent_w, draw->clip.x * sx));
        float y0 = fmaxf(0.0f, fminf((float)extent_h, draw->clip.y * sy));
        float x1 = fmaxf(x0, fminf((float)extent_w, (draw->clip.x + draw->clip.width) * sx));
        float y1 = fmaxf(y0, fminf((float)extent_h, (draw->clip.y + draw->clip.height) * sy));
        uint32_t sw = (uint32_t)(x1 - x0), sh = (uint32_t)(y1 - y0);
        if (!sw || !sh || !draw->index_count)
            continue;
        const af_mtl_texture *tex = ui->atlas;
        if (draw->texture >= 2) {
            for (size_t t = 0; t < ui->image_count; ++t)
                if (ui->images[t].id == draw->texture && ui->images[t].texture) {
                    tex = ui->images[t].texture;
                    break;
                }
        }
        if (tex != bound) {
            [enc setFragmentTexture:af_mtl_texture_handle(tex) atIndex:AF_MTL_POST_TEX_0];
            [enc setFragmentSamplerState:af_mtl_texture_sampler(tex) atIndex:AF_MTL_POST_TEX_0];
            bound = tex;
        }
        MTLScissorRect sc = {(NSUInteger)x0, (NSUInteger)y0, sw, sh};
        [enc setScissorRect:sc];
        [enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                        indexCount:draw->index_count
                         indexType:MTLIndexTypeUInt32
                       indexBuffer:bufs->indices
                 indexBufferOffset:(NSUInteger)draw->first_index * sizeof(uint32_t)];
    }
}
