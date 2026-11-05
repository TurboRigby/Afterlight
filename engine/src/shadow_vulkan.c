/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "shadow_vulkan.h"
#include <afterlight/mesh.h>

#include "shadow_vert.h"
#include "log.h"
#include <stdlib.h>

typedef struct shadow_frame {
    VkImage image;
    VkDeviceMemory memory;
    VkImageView array_view,layer_views[AF_SHADOW_LAYER_COUNT];
    VkFramebuffer framebuffers[AF_SHADOW_LAYER_COUNT];
    VkDescriptorSet descriptor;
} shadow_frame;
struct af_vk_shadows {
    VkDevice device;
    uint32_t resolution,layers;
    VkFormat format;
    VkRenderPass pass;
    VkPipelineLayout pipeline_layout;
    VkPipeline pipeline;
    VkSampler sampler;
    VkDescriptorSetLayout descriptor_layout;
    VkDescriptorPool pool;
    shadow_frame frames[AF_SHADOW_FRAME_COUNT];
};
static bool ok(VkResult result,const char *operation){if(result==VK_SUCCESS)return true;af_log("error","Shadow %s failed (%d).",operation,(int)result);return false;}
void af_vk_shadows_destroy(af_vk_shadows *s)
{
    if(!s)return;
    if(s->pipeline)vkDestroyPipeline(s->device,s->pipeline,NULL);
    if(s->pipeline_layout)vkDestroyPipelineLayout(s->device,s->pipeline_layout,NULL);
    if(s->pool)vkDestroyDescriptorPool(s->device,s->pool,NULL);
    if(s->descriptor_layout)vkDestroyDescriptorSetLayout(s->device,s->descriptor_layout,NULL);
    if(s->sampler)vkDestroySampler(s->device,s->sampler,NULL);
    for(unsigned f=0;f<AF_SHADOW_FRAME_COUNT;++f){shadow_frame *frame=&s->frames[f];
        for(unsigned l=0;l<AF_SHADOW_LAYER_COUNT;++l){
            if(frame->framebuffers[l])vkDestroyFramebuffer(s->device,frame->framebuffers[l],NULL);
            if(frame->layer_views[l])vkDestroyImageView(s->device,frame->layer_views[l],NULL);}
        if(frame->array_view)vkDestroyImageView(s->device,frame->array_view,NULL);
        if(frame->image)vkDestroyImage(s->device,frame->image,NULL);
        if(frame->memory)vkFreeMemory(s->device,frame->memory,NULL);
    }
    if(s->pass)vkDestroyRenderPass(s->device,s->pass,NULL);free(s);
}
static bool pipeline(af_vk_shadows *s)
{
    VkShaderModule shader;
    VkShaderModuleCreateInfo code={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=sizeof(af_shadow_vert),.pCode=af_shadow_vert};
    if(!ok(vkCreateShaderModule(s->device,&code,NULL,&shader),"shader"))return false;
    VkPushConstantRange push={VK_SHADER_STAGE_VERTEX_BIT,0,64};
    VkPipelineLayoutCreateInfo layout={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.pushConstantRangeCount=1,.pPushConstantRanges=&push};
    bool success=false;
    if(!ok(vkCreatePipelineLayout(s->device,&layout,NULL,&s->pipeline_layout),"pipeline layout"))goto done;
    VkPipelineShaderStageCreateInfo stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_VERTEX_BIT,.module=shader,.pName="main"};
    VkVertexInputBindingDescription binding={0,sizeof(af_vertex),VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attribute={0,0,VK_FORMAT_R32G32B32_SFLOAT,0};
    VkPipelineVertexInputStateCreateInfo input={.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,.vertexBindingDescriptionCount=1,.pVertexBindingDescriptions=&binding,.vertexAttributeDescriptionCount=1,.pVertexAttributeDescriptions=&attribute};
    VkPipelineInputAssemblyStateCreateInfo assembly={.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    VkPipelineViewportStateCreateInfo viewport={.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,.viewportCount=1,.scissorCount=1};
    VkPipelineRasterizationStateCreateInfo raster={.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,.polygonMode=VK_POLYGON_MODE_FILL,.cullMode=VK_CULL_MODE_NONE,.lineWidth=1,.depthBiasEnable=VK_TRUE};
    VkPipelineMultisampleStateCreateInfo samples={.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT};
    VkPipelineDepthStencilStateCreateInfo depth={.sType=VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,.depthTestEnable=VK_TRUE,.depthWriteEnable=VK_TRUE,.depthCompareOp=VK_COMPARE_OP_LESS};
    VkPipelineColorBlendStateCreateInfo blend={.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    VkDynamicState states[]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR,VK_DYNAMIC_STATE_DEPTH_BIAS};
    VkPipelineDynamicStateCreateInfo dynamic={.sType=VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,.dynamicStateCount=3,.pDynamicStates=states};
    VkGraphicsPipelineCreateInfo info={.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,.stageCount=1,.pStages=&stage,.pVertexInputState=&input,.pInputAssemblyState=&assembly,
        .pViewportState=&viewport,.pRasterizationState=&raster,.pMultisampleState=&samples,.pDepthStencilState=&depth,.pColorBlendState=&blend,.pDynamicState=&dynamic,
        .layout=s->pipeline_layout,.renderPass=s->pass,.basePipelineIndex=-1};
    success=ok(vkCreateGraphicsPipelines(s->device,VK_NULL_HANDLE,1,&info,NULL,&s->pipeline),"pipeline");
done:
    vkDestroyShaderModule(s->device,shader,NULL);return success;
}
af_vk_shadows *af_vk_shadows_create(VkPhysicalDevice physical,VkDevice device,uint32_t resolution,uint32_t layers)
{
    if(!resolution||!layers||layers>AF_SHADOW_LAYER_COUNT)return NULL;
    af_vk_shadows *s=calloc(1,sizeof(*s));if(!s)return NULL;s->device=device;s->resolution=resolution;s->layers=layers;
    VkPhysicalDeviceProperties limits;vkGetPhysicalDeviceProperties(physical,&limits);
    if(resolution>limits.limits.maxImageDimension2D||layers>limits.limits.maxImageArrayLayers)goto fail;
    VkFormat formats[]={VK_FORMAT_D32_SFLOAT,VK_FORMAT_D16_UNORM};
    for(unsigned i=0;i<2;++i){VkFormatProperties p;vkGetPhysicalDeviceFormatProperties(physical,formats[i],&p);
        VkFormatFeatureFlags flags=VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
        if((p.optimalTilingFeatures&flags)==flags){s->format=formats[i];break;}}
    if(s->format==VK_FORMAT_UNDEFINED){af_log("error","No sampled shadow depth format.");goto fail;}
    VkAttachmentDescription attachment={.format=s->format,.samples=VK_SAMPLE_COUNT_1_BIT,.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR,.storeOp=VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE,.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE,.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED,.finalLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
    VkAttachmentReference reference={0,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass={.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS,.pDepthStencilAttachment=&reference};
    VkSubpassDependency dependencies[]={
        {.srcSubpass=VK_SUBPASS_EXTERNAL,.dstSubpass=0,.srcStageMask=VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,.dstStageMask=VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            .srcAccessMask=VK_ACCESS_SHADER_READ_BIT,.dstAccessMask=VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT},
        {.srcSubpass=0,.dstSubpass=VK_SUBPASS_EXTERNAL,.srcStageMask=VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,.dstStageMask=VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            .srcAccessMask=VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,.dstAccessMask=VK_ACCESS_SHADER_READ_BIT}};
    VkRenderPassCreateInfo pass={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,.attachmentCount=1,.pAttachments=&attachment,.subpassCount=1,.pSubpasses=&subpass,.dependencyCount=2,.pDependencies=dependencies};
    if(!ok(vkCreateRenderPass(device,&pass,NULL,&s->pass),"render pass")||!pipeline(s))goto fail;
    VkSamplerCreateInfo sampler={.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,.magFilter=VK_FILTER_NEAREST,.minFilter=VK_FILTER_NEAREST,.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,.addressModeV=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,.borderColor=VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE};
    if(!ok(vkCreateSampler(device,&sampler,NULL,&s->sampler),"sampler"))goto fail;
    VkDescriptorSetLayoutBinding binding={.binding=0,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT};
    VkDescriptorSetLayoutCreateInfo descriptor_layout={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=1,.pBindings=&binding};
    if(!ok(vkCreateDescriptorSetLayout(device,&descriptor_layout,NULL,&s->descriptor_layout),"descriptor layout"))goto fail;
    VkDescriptorPoolSize size={VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,AF_SHADOW_FRAME_COUNT};
    VkDescriptorPoolCreateInfo pool={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=AF_SHADOW_FRAME_COUNT,.poolSizeCount=1,.pPoolSizes=&size};
    if(!ok(vkCreateDescriptorPool(device,&pool,NULL,&s->pool),"descriptor pool"))goto fail;
    VkPhysicalDeviceMemoryProperties memory;vkGetPhysicalDeviceMemoryProperties(physical,&memory);
    for(unsigned f=0;f<AF_SHADOW_FRAME_COUNT;++f){shadow_frame *frame=&s->frames[f];
        VkImageCreateInfo image={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,.format=s->format,.extent={resolution,resolution,1},.mipLevels=1,.arrayLayers=layers,
            .samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,.usage=VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,.sharingMode=VK_SHARING_MODE_EXCLUSIVE};
        if(!ok(vkCreateImage(device,&image,NULL,&frame->image),"image"))goto fail;
        VkMemoryRequirements requirements;vkGetImageMemoryRequirements(device,frame->image,&requirements);uint32_t type=UINT32_MAX;
        for(unsigned i=0;i<memory.memoryTypeCount;++i)if((requirements.memoryTypeBits&(1u<<i))&&(memory.memoryTypes[i].propertyFlags&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)){type=i;break;}
        if(type==UINT32_MAX)goto fail;
        VkMemoryAllocateInfo allocate={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=requirements.size,.memoryTypeIndex=type};
        if(!ok(vkAllocateMemory(device,&allocate,NULL,&frame->memory),"memory")||!ok(vkBindImageMemory(device,frame->image,frame->memory,0),"bind memory"))goto fail;
        VkImageViewCreateInfo view={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=frame->image,.viewType=VK_IMAGE_VIEW_TYPE_2D_ARRAY,.format=s->format,.subresourceRange={VK_IMAGE_ASPECT_DEPTH_BIT,0,1,0,layers}};
        if(!ok(vkCreateImageView(device,&view,NULL,&frame->array_view),"array view"))goto fail;
        view.viewType=VK_IMAGE_VIEW_TYPE_2D;view.subresourceRange.layerCount=1;
        for(unsigned l=0;l<layers;++l){view.subresourceRange.baseArrayLayer=l;
            if(!ok(vkCreateImageView(device,&view,NULL,&frame->layer_views[l]),"layer view"))goto fail;
            VkFramebufferCreateInfo framebuffer={.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,.renderPass=s->pass,.attachmentCount=1,.pAttachments=&frame->layer_views[l],.width=resolution,.height=resolution,.layers=1};
            if(!ok(vkCreateFramebuffer(device,&framebuffer,NULL,&frame->framebuffers[l]),"framebuffer"))goto fail;
        }
        VkDescriptorSetAllocateInfo descriptor={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=s->pool,.descriptorSetCount=1,.pSetLayouts=&s->descriptor_layout};
        if(!ok(vkAllocateDescriptorSets(device,&descriptor,&frame->descriptor),"descriptor"))goto fail;
        VkDescriptorImageInfo image_info={s->sampler,frame->array_view,VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet write={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=frame->descriptor,.dstBinding=0,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.pImageInfo=&image_info};
        vkUpdateDescriptorSets(device,1,&write,0,NULL);
    }
    return s;
fail:
    af_vk_shadows_destroy(s);return NULL;
}
VkDescriptorSetLayout af_vk_shadows_layout(const af_vk_shadows *s){return s->descriptor_layout;}
VkDescriptorSet af_vk_shadows_descriptor(const af_vk_shadows *s,uint32_t frame){return s->frames[frame].descriptor;}
void af_vk_shadows_begin(af_vk_shadows *s,VkCommandBuffer command,uint32_t frame,uint32_t layer,af_shadow_settings settings)
{
    VkClearValue clear={.depthStencil={1,0}};
    VkRenderPassBeginInfo begin={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,.renderPass=s->pass,.framebuffer=s->frames[frame].framebuffers[layer],.renderArea={.extent={s->resolution,s->resolution}},.clearValueCount=1,.pClearValues=&clear};
    vkCmdBeginRenderPass(command,&begin,VK_SUBPASS_CONTENTS_INLINE);
    VkViewport viewport={.width=(float)s->resolution,.height=(float)s->resolution,.maxDepth=1};VkRect2D scissor={.extent={s->resolution,s->resolution}};
    vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,s->pipeline);vkCmdSetViewport(command,0,1,&viewport);vkCmdSetScissor(command,0,1,&scissor);
    vkCmdSetDepthBias(command,settings.constant_bias,0,settings.slope_bias);
}
void af_vk_shadows_push(af_vk_shadows *s,VkCommandBuffer command,const float *mvp){vkCmdPushConstants(command,s->pipeline_layout,VK_SHADER_STAGE_VERTEX_BIT,0,64,mvp);}

VkDescriptorImageInfo af_vk_shadows_image(const af_vk_shadows *s,uint32_t frame){return (VkDescriptorImageInfo){s->sampler,s->frames[frame].array_view,VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};}
