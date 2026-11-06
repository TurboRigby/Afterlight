/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "ui_vulkan.h"
#include "texture_vulkan.h"
#include "log.h"
#include "ui_vert.h"
#include "ui_frag.h"
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define UI_FRAME_COUNT AF_UI_FRAMES_IN_FLIGHT

typedef struct ui_buffer {
    VkBuffer buffer;
    VkDeviceMemory memory;
    void *mapped;
    VkDeviceSize capacity;
    bool coherent;
} ui_buffer;
typedef struct ui_frame { ui_buffer vertices, indices;af_vk_texture *retired[128];size_t retired_count; } ui_frame;
struct af_vk_ui {
    VkPhysicalDevice physical;
    VkDevice device;
    af_vk_texture *texture;
    struct {uint32_t id;af_vk_texture *texture;unsigned char *pending;uint32_t width,height;bool remove;} images[128];
    size_t image_count;
    VkPipelineLayout layout;
    VkPipeline pipeline;
    ui_frame frames[UI_FRAME_COUNT];
};

static bool ok(VkResult result, const char *operation)
{
    if (result == VK_SUCCESS) return true;
    af_log("error", "UI %s failed (VkResult %d).", operation, (int)result);
    return false;
}
static uint32_t memory_type(af_vk_ui *ui, uint32_t bits, VkMemoryPropertyFlags required, VkMemoryPropertyFlags preferred)
{
    VkPhysicalDeviceMemoryProperties properties;
    vkGetPhysicalDeviceMemoryProperties(ui->physical, &properties);
    uint32_t fallback = UINT32_MAX;
    for (uint32_t i = 0; i < properties.memoryTypeCount; ++i) {
        VkMemoryPropertyFlags flags = properties.memoryTypes[i].propertyFlags;
        if (!(bits & (1u << i)) || (flags & required) != required) continue;
        fallback = i;
        if ((flags & preferred) == preferred) return i;
    }
    return fallback;
}
static void free_buffer(af_vk_ui *ui, ui_buffer *buffer)
{
    if (buffer->mapped) vkUnmapMemory(ui->device, buffer->memory);
    if (buffer->buffer) vkDestroyBuffer(ui->device, buffer->buffer, NULL);
    if (buffer->memory) vkFreeMemory(ui->device, buffer->memory, NULL);
    *buffer = (ui_buffer){0};
}
static bool reserve_buffer(af_vk_ui *ui, ui_buffer *buffer, VkDeviceSize size, VkBufferUsageFlags usage)
{
    if (buffer->capacity >= size) return true;
    free_buffer(ui, buffer); /* Owner frame fence is already complete. */
    VkDeviceSize capacity = size < 65536 ? 65536 : size * 2;
    VkBufferCreateInfo info = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = capacity, .usage = usage, .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
    if (!ok(vkCreateBuffer(ui->device, &info, NULL, &buffer->buffer), "vkCreateBuffer")) return false;
    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(ui->device, buffer->buffer, &requirements);
    uint32_t index = memory_type(ui, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (index == UINT32_MAX) return false;
    VkMemoryAllocateInfo allocation = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size, .memoryTypeIndex = index};
    if (!ok(vkAllocateMemory(ui->device, &allocation, NULL, &buffer->memory), "vkAllocateMemory") ||
        !ok(vkBindBufferMemory(ui->device, buffer->buffer, buffer->memory, 0), "vkBindBufferMemory") ||
        !ok(vkMapMemory(ui->device, buffer->memory, 0, VK_WHOLE_SIZE, 0, &buffer->mapped), "vkMapMemory")) return false;
    VkPhysicalDeviceMemoryProperties properties;
    vkGetPhysicalDeviceMemoryProperties(ui->physical, &properties);
    buffer->coherent = (properties.memoryTypes[index].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
    buffer->capacity = capacity;
    return true;
}
static bool flush_buffer(af_vk_ui *ui, ui_buffer *buffer)
{
    if (buffer->coherent) return true;
    VkMappedMemoryRange range = {.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
        .memory = buffer->memory, .size = VK_WHOLE_SIZE};
    return ok(vkFlushMappedMemoryRanges(ui->device, 1, &range), "vkFlushMappedMemoryRanges");
}

void af_vk_ui_release_pipeline(af_vk_ui *ui)
{
    if (ui && ui->pipeline) vkDestroyPipeline(ui->device, ui->pipeline, NULL);
    if (ui) ui->pipeline = VK_NULL_HANDLE;
}
void af_vk_ui_destroy(af_vk_ui *ui)
{
    if (!ui) return;
    af_vk_ui_release_pipeline(ui);
    for (unsigned i = 0; i < UI_FRAME_COUNT; ++i) {
        for(size_t j=0;j<ui->frames[i].retired_count;++j)af_vk_texture_destroy(ui->frames[i].retired[j]);
        free_buffer(ui, &ui->frames[i].vertices);
        free_buffer(ui, &ui->frames[i].indices);
    }
    if (ui->layout) vkDestroyPipelineLayout(ui->device, ui->layout, NULL);
    for(size_t i=0;i<ui->image_count;++i){free(ui->images[i].pending);af_vk_texture_destroy(ui->images[i].texture);}
    af_vk_texture_destroy(ui->texture);
    free(ui);
}

af_vk_ui *af_vk_ui_create(VkPhysicalDevice physical, VkDevice device, VkQueue queue, uint32_t family,
    const unsigned char *rgba, uint32_t width, uint32_t height)
{
    af_vk_ui *ui = calloc(1, sizeof(*ui));
    if (!ui) return NULL;
    ui->device = device; ui->physical = physical;
    ui->texture=af_vk_texture_create(physical,device,queue,family,rgba,width,height,false,false,1);
    if(!ui->texture)goto fail;
    VkDescriptorSetLayout descriptor_layout=af_vk_texture_layout(ui->texture);
    VkPushConstantRange range = {.stageFlags = VK_SHADER_STAGE_VERTEX_BIT, .size = 2*sizeof(float)};
    VkPipelineLayoutCreateInfo layout = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1, .pSetLayouts = &descriptor_layout, .pushConstantRangeCount = 1, .pPushConstantRanges = &range};
    if (!ok(vkCreatePipelineLayout(device, &layout, NULL, &ui->layout), "pipeline layout")) goto fail;
    return ui;
fail:
    af_vk_ui_destroy(ui);
    return NULL;
}

bool af_vk_ui_create_pipeline(af_vk_ui *ui, VkRenderPass pass)
{
    af_vk_ui_release_pipeline(ui);
    VkShaderModule vertex = VK_NULL_HANDLE, fragment = VK_NULL_HANDLE;
    VkShaderModuleCreateInfo shader = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = sizeof(af_ui_vert), .pCode = af_ui_vert};
    if (!ok(vkCreateShaderModule(ui->device, &shader, NULL, &vertex), "vertex module")) return false;
    shader.codeSize = sizeof(af_ui_frag); shader.pCode = af_ui_frag;
    bool success = false;
    if (!ok(vkCreateShaderModule(ui->device, &shader, NULL, &fragment), "fragment module")) goto done;
    VkPipelineShaderStageCreateInfo stages[] = {
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vertex, .pName = "main"},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fragment, .pName = "main"}
    };
    VkVertexInputBindingDescription binding = {0,sizeof(af_overlay_vertex),VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attributes[] = {
        {0,0,VK_FORMAT_R32G32_SFLOAT,offsetof(af_overlay_vertex,position)},
        {1,0,VK_FORMAT_R32G32_SFLOAT,offsetof(af_overlay_vertex,uv)},
        {2,0,VK_FORMAT_R8G8B8A8_UNORM,offsetof(af_overlay_vertex,color)}
    };
    VkPipelineVertexInputStateCreateInfo input = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1, .pVertexBindingDescriptions = &binding,
        .vertexAttributeDescriptionCount = 3, .pVertexAttributeDescriptions = attributes};
    VkPipelineInputAssemblyStateCreateInfo assembly = {.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    VkPipelineViewportStateCreateInfo viewport = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .scissorCount = 1};
    VkPipelineRasterizationStateCreateInfo raster = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE, .lineWidth = 1};
    VkPipelineMultisampleStateCreateInfo samples = {.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};
    VkPipelineDepthStencilStateCreateInfo depth = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    VkPipelineColorBlendAttachmentState color = {.blendEnable = VK_TRUE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA, .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD, .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT};
    VkPipelineColorBlendStateCreateInfo blending = {.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1, .pAttachments = &color};
    VkDynamicState states[] = {VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2, .pDynamicStates = states};
    VkGraphicsPipelineCreateInfo info = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2, .pStages = stages, .pVertexInputState = &input, .pInputAssemblyState = &assembly,
        .pViewportState = &viewport, .pRasterizationState = &raster, .pMultisampleState = &samples,
        .pDepthStencilState = &depth, .pColorBlendState = &blending, .pDynamicState = &dynamic,
        .layout = ui->layout, .renderPass = pass, .basePipelineIndex = -1};
    success = ok(vkCreateGraphicsPipelines(ui->device, VK_NULL_HANDLE, 1, &info, NULL, &ui->pipeline), "graphics pipeline");
done:
    if (fragment) vkDestroyShaderModule(ui->device, fragment, NULL);
    vkDestroyShaderModule(ui->device, vertex, NULL);
    return success;
}

bool af_vk_ui_prepare(af_vk_ui *ui, uint32_t frame_index, const af_overlay_frame *frame)
{
    if (!frame->vertex_count || !frame->index_count) return true;
    ui_frame *buffers = &ui->frames[frame_index % UI_FRAME_COUNT];
    VkDeviceSize vertices = (VkDeviceSize)frame->vertex_count * sizeof(*frame->vertices);
    VkDeviceSize indices = (VkDeviceSize)frame->index_count * sizeof(*frame->indices);
    if (!reserve_buffer(ui, &buffers->vertices, vertices, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) ||
        !reserve_buffer(ui, &buffers->indices, indices, VK_BUFFER_USAGE_INDEX_BUFFER_BIT)) return false;
    memcpy(buffers->vertices.mapped, frame->vertices, (size_t)vertices);
    memcpy(buffers->indices.mapped, frame->indices, (size_t)indices);
    return flush_buffer(ui, &buffers->vertices) && flush_buffer(ui, &buffers->indices);
}
void af_vk_ui_draw(af_vk_ui *ui, VkCommandBuffer command, uint32_t frame_index,
    const af_overlay_frame *frame, VkExtent2D extent)
{
    if (!frame->index_count || !ui->pipeline) return;
    const ui_frame *buffers = &ui->frames[frame_index % UI_FRAME_COUNT];
    VkViewport viewport = {.width = (float)extent.width, .height = (float)extent.height, .maxDepth = 1};
    vkCmdSetViewport(command, 0, 1, &viewport);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, ui->pipeline);
    VkDescriptorSet descriptor=af_vk_texture_descriptor(ui->texture);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, ui->layout, 0, 1, &descriptor, 0, NULL);
    float screen[] = {frame->width,frame->height};
    vkCmdPushConstants(command, ui->layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(screen), screen);
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(command, 0, 1, &buffers->vertices.buffer, &offset);
    vkCmdBindIndexBuffer(command, buffers->indices.buffer, 0, VK_INDEX_TYPE_UINT32);
    float sx = (float)extent.width / frame->width, sy = (float)extent.height / frame->height;
    for (uint32_t i = 0; i < frame->command_count; ++i) {
        const af_overlay_command *draw = &frame->commands[i];
        float x0 = fmaxf(0, fminf((float)extent.width, draw->clip.x * sx));
        float y0 = fmaxf(0, fminf((float)extent.height, draw->clip.y * sy));
        float x1 = fmaxf(x0, fminf((float)extent.width, (draw->clip.x + draw->clip.width) * sx));
        float y1 = fmaxf(y0, fminf((float)extent.height, (draw->clip.y + draw->clip.height) * sy));
        VkRect2D scissor = {.offset = {(int32_t)x0,(int32_t)y0}, .extent = {(uint32_t)(x1-x0),(uint32_t)(y1-y0)}};
        if (!scissor.extent.width || !scissor.extent.height || !draw->index_count) continue;
        af_vk_texture *texture=ui->texture;
        if(draw->texture>=2)for(size_t t=0;t<ui->image_count;++t)if(ui->images[t].id==draw->texture&&ui->images[t].texture){texture=ui->images[t].texture;break;}
        VkDescriptorSet next=af_vk_texture_descriptor(texture);
        if(next!=descriptor){vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_GRAPHICS,ui->layout,0,1,&next,0,NULL);descriptor=next;}
        vkCmdSetScissor(command, 0, 1, &scissor);
        vkCmdDrawIndexed(command, draw->index_count, 1, draw->first_index, 0, 0);
    }
}

bool af_vk_ui_upload_texture(af_vk_ui *ui,VkQueue queue,uint32_t family,uint32_t id,const unsigned char *rgba,uint32_t width,uint32_t height){
    (void)queue;(void)family;if(!ui||id<2||!rgba||!width||!height||width>8192||height>8192)return false;
    size_t index=0;while(index<ui->image_count&&ui->images[index].id!=id)++index;if(index==128)return false;
    size_t size=(size_t)width*height*4,total=0;for(size_t i=0;i<ui->image_count;++i)if(i!=index&&ui->images[i].pending)total+=(size_t)ui->images[i].width*ui->images[i].height*4;
    if(size>64u*1024u*1024u||total>64u*1024u*1024u-size)return false;
    unsigned char *copy=malloc(size);if(!copy)return false;memcpy(copy,rgba,size);
    if(index==ui->image_count){memset(&ui->images[index],0,sizeof(ui->images[index]));++ui->image_count;}
    free(ui->images[index].pending);ui->images[index].id=id;ui->images[index].pending=copy;ui->images[index].width=width;ui->images[index].height=height;ui->images[index].remove=false;return true;
}
bool af_vk_ui_remove_texture(af_vk_ui *ui,uint32_t id){
    if(!ui||id<2)return false;for(size_t i=0;i<ui->image_count;++i)if(ui->images[i].id==id){free(ui->images[i].pending);ui->images[i].pending=NULL;ui->images[i].remove=true;return true;}return false;
}
void af_vk_ui_record_uploads(af_vk_ui *ui,VkCommandBuffer command,uint32_t frame_index){
    ui_frame *frame=&ui->frames[frame_index%UI_FRAME_COUNT];
    for(size_t i=0;i<frame->retired_count;++i)af_vk_texture_destroy(frame->retired[i]);frame->retired_count=0;
    bool uploaded=false;
    for(size_t i=0;i<ui->image_count;){
        if(ui->images[i].remove){if(ui->images[i].texture)frame->retired[frame->retired_count++]=ui->images[i].texture;ui->images[i]=ui->images[--ui->image_count];continue;}
        if(!uploaded&&ui->images[i].pending){
            af_vk_texture *texture=af_vk_texture_create_recorded(ui->physical,ui->device,command,ui->images[i].pending,ui->images[i].width,ui->images[i].height);
            if(texture){if(ui->images[i].texture)frame->retired[frame->retired_count++]=ui->images[i].texture;ui->images[i].texture=texture;}
            free(ui->images[i].pending);ui->images[i].pending=NULL;uploaded=true;
        }++i;
    }
}
