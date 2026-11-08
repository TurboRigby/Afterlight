/* SPDX-License-Identifier: MPL-2.0 */
#include "post_vulkan.h"
#include "../../third_party/smaa/AreaTex.h"
#include "../../third_party/smaa/SearchTex.h"
#include "fullscreen_vert.h"
#include "gi_denoise_frag.h"
#include "log.h"
#include "post_frag.h"
#include "texture_vulkan.h"
#include <math.h>
#include <stdlib.h>

typedef struct post_image {
    VkImage image;
    VkDeviceMemory memory;
    VkImageView view;
    VkFramebuffer framebuffer;
} post_image;
enum { UPSCALED_EXTRACT_SET = 14, UPSCALED_COMPOSITE_SET };
typedef struct post_frame {
    post_image hdr, motion, blur[2], upscaled, gi;
    VkFramebuffer scene, output;
    VkFramebuffer gi_framebuffer[2];
    VkDescriptorSet sets[UPSCALED_COMPOSITE_SET + 1];
    VkDescriptorSet gi_sets[2];
} post_frame;
struct af_vk_post {
    VkPhysicalDevice physical;
    VkDevice device;
    VkExtent2D extent, half, render_extent;
    uint32_t count;
    uint64_t allocation_bytes;
    unsigned allocation_attempts;
    VkRenderPass blur_pass, output_pass, gi_pass;
    VkPipeline blur_pipeline, output_pipeline, gi_pipeline;
    VkPipelineLayout pipeline_layout, gi_pipeline_layout;
    VkDescriptorSetLayout layout, gi_layout;
    VkDescriptorPool pool;
    VkSampler sampler;
    post_frame *frames;
    post_image display, history[2], gi_history[2];
    bool initialized, history_valid;
    bool gi, gi_initialized, gi_history_valid;
    unsigned history_index, gi_history_index;
    float adapted_exposure;
    af_vk_texture *area_lut, *search_lut;
};
static bool ok(VkResult r) {
    if (r == VK_SUCCESS)
        return true;
    af_log("error", "HDR/post resource failed (%d).", (int)r);
    return false;
}
static bool framebuffer(af_vk_post *p, VkRenderPass pass, const VkImageView *views, uint32_t count, VkExtent2D extent,
                        VkFramebuffer *out) {
    VkFramebufferCreateInfo info = {.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
                                    .renderPass = pass,
                                    .attachmentCount = count,
                                    .pAttachments = views,
                                    .width = extent.width,
                                    .height = extent.height,
                                    .layers = 1};
    return ok(vkCreateFramebuffer(p->device, &info, NULL, out));
}
static bool allocate_image(af_vk_post *p, post_image *out, VkExtent2D extent, VkImageUsageFlags usage) {
    const char *fault = getenv("AFTERLIGHT_TEST_POST_FAIL_AT");
    if (fault && strtoul(fault, NULL, 10) == ++p->allocation_attempts) {
        af_log("info", "Injected post allocation failure for cleanup regression.");
        return false;
    }
    VkImageCreateInfo info = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                              .imageType = VK_IMAGE_TYPE_2D,
                              .format = VK_FORMAT_R16G16B16A16_SFLOAT,
                              .extent = {extent.width, extent.height, 1},
                              .mipLevels = 1,
                              .arrayLayers = 1,
                              .samples = VK_SAMPLE_COUNT_1_BIT,
                              .tiling = VK_IMAGE_TILING_OPTIMAL,
                              .usage = usage,
                              .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
    if (!ok(vkCreateImage(p->device, &info, NULL, &out->image)))
        return false;
    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(p->device, out->image, &requirements);
    VkPhysicalDeviceMemoryProperties properties;
    vkGetPhysicalDeviceMemoryProperties(p->physical, &properties);
    uint32_t type = UINT32_MAX;
    for (uint32_t i = 0; i < properties.memoryTypeCount; ++i)
        if ((requirements.memoryTypeBits & (1u << i)) &&
            (properties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            type = i;
            break;
        }
    if (type == UINT32_MAX)
        return false;
    p->allocation_bytes += requirements.size;
    VkMemoryAllocateInfo allocation = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = requirements.size, .memoryTypeIndex = type};
    if (!ok(vkAllocateMemory(p->device, &allocation, NULL, &out->memory)) ||
        !ok(vkBindImageMemory(p->device, out->image, out->memory, 0)))
        return false;
    VkImageViewCreateInfo view = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                                  .image = out->image,
                                  .viewType = VK_IMAGE_VIEW_TYPE_2D,
                                  .format = info.format,
                                  .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    return ok(vkCreateImageView(p->device, &view, NULL, &out->view));
}
static bool image(af_vk_post *p, post_image *out, VkExtent2D extent) {
    return allocate_image(p, out, extent,
                          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                              VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
}

static bool pass(af_vk_post *p, VkFormat format, bool output, VkRenderPass *out) {
    VkAttachmentDescription attachment = {.format = format,
                                          .samples = VK_SAMPLE_COUNT_1_BIT,
                                          .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                                          .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                                          .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                                          .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                                          .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                                          .finalLayout = output ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
                                                                : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkAttachmentReference reference = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass = {.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    .colorAttachmentCount = 1,
                                    .pColorAttachments = &reference};
    VkSubpassDependency dependencies[] = {
        {.srcSubpass = VK_SUBPASS_EXTERNAL,
         .dstSubpass = 0,
         .srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
         .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
         .srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
         .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT},
        {.srcSubpass = 0,
         .dstSubpass = VK_SUBPASS_EXTERNAL,
         .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
         .dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
         .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
         .dstAccessMask = VK_ACCESS_SHADER_READ_BIT}};
    VkRenderPassCreateInfo info = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
                                   .attachmentCount = 1,
                                   .pAttachments = &attachment,
                                   .subpassCount = 1,
                                   .pSubpasses = &subpass,
                                   .dependencyCount = output ? 1u : 2u,
                                   .pDependencies = dependencies};
    return ok(vkCreateRenderPass(p->device, &info, NULL, out));
}
static bool pipeline(af_vk_post *p, VkRenderPass render_pass, VkPipeline *out) {
    VkShaderModule vertex = VK_NULL_HANDLE, fragment = VK_NULL_HANDLE;
    bool success = false;
    VkShaderModuleCreateInfo code = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                                     .codeSize = sizeof(af_fullscreen_vert),
                                     .pCode = af_fullscreen_vert};
    if (!ok(vkCreateShaderModule(p->device, &code, NULL, &vertex)))
        goto done;
    code.codeSize = sizeof(af_post_frag);
    code.pCode = af_post_frag;
    if (!ok(vkCreateShaderModule(p->device, &code, NULL, &fragment)))
        goto done;
    VkPipelineShaderStageCreateInfo stages[] = {{.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                                 .stage = VK_SHADER_STAGE_VERTEX_BIT,
                                                 .module = vertex,
                                                 .pName = "main"},
                                                {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                                 .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                                                 .module = fragment,
                                                 .pName = "main"}};
    VkPipelineVertexInputStateCreateInfo input = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo assembly = {.sType =
                                                           VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
                                                       .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    VkPipelineViewportStateCreateInfo viewport = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .scissorCount = 1};
    VkPipelineRasterizationStateCreateInfo raster = {.sType =
                                                         VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
                                                     .polygonMode = VK_POLYGON_MODE_FILL,
                                                     .cullMode = VK_CULL_MODE_NONE,
                                                     .lineWidth = 1};
    VkPipelineMultisampleStateCreateInfo samples = {.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
                                                    .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};
    VkPipelineColorBlendAttachmentState attachment = {.colorWriteMask = 15};
    VkPipelineColorBlendStateCreateInfo blend = {.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
                                                 .attachmentCount = 1,
                                                 .pAttachments = &attachment};
    VkDynamicState states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
                                                .dynamicStateCount = 2,
                                                .pDynamicStates = states};
    VkGraphicsPipelineCreateInfo info = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
                                         .stageCount = 2,
                                         .pStages = stages,
                                         .pVertexInputState = &input,
                                         .pInputAssemblyState = &assembly,
                                         .pViewportState = &viewport,
                                         .pRasterizationState = &raster,
                                         .pMultisampleState = &samples,
                                         .pColorBlendState = &blend,
                                         .pDynamicState = &dynamic,
                                         .layout = p->pipeline_layout,
                                         .renderPass = render_pass,
                                         .basePipelineIndex = -1};
    success = ok(vkCreateGraphicsPipelines(p->device, VK_NULL_HANDLE, 1, &info, NULL, out));
done:
    if (vertex)
        vkDestroyShaderModule(p->device, vertex, NULL);
    if (fragment)
        vkDestroyShaderModule(p->device, fragment, NULL);
    return success;
}
static bool gi_render_pass(af_vk_post *p) {
    VkAttachmentDescription attachments[2] = {
        {.format = VK_FORMAT_R16G16B16A16_SFLOAT,
         .samples = VK_SAMPLE_COUNT_1_BIT,
         .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
         .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
         .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
         .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
         .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
         .finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {.format = VK_FORMAT_R16G16B16A16_SFLOAT,
         .samples = VK_SAMPLE_COUNT_1_BIT,
         .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
         .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
         .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
         .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
         .initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
         .finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
    };
    VkAttachmentReference references[2] = {{.attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
                                           {.attachment = 1, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}};
    VkSubpassDescription subpass = {.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    .colorAttachmentCount = 2,
                                    .pColorAttachments = references};
    VkSubpassDependency dependencies[] = {
        {.srcSubpass = VK_SUBPASS_EXTERNAL,
         .dstSubpass = 0,
         .srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
         .dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
         .srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
         .dstAccessMask =
             VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT},
        {.srcSubpass = 0,
         .dstSubpass = VK_SUBPASS_EXTERNAL,
         .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
         .dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
         .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
         .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT}};
    VkRenderPassCreateInfo info = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
                                   .attachmentCount = 2,
                                   .pAttachments = attachments,
                                   .subpassCount = 1,
                                   .pSubpasses = &subpass,
                                   .dependencyCount = 2,
                                   .pDependencies = dependencies};
    return ok(vkCreateRenderPass(p->device, &info, NULL, &p->gi_pass));
}
static bool gi_pipeline(af_vk_post *p) {
    VkShaderModule vertex = VK_NULL_HANDLE, fragment = VK_NULL_HANDLE;
    bool success = false;
    VkShaderModuleCreateInfo code = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                                     .codeSize = sizeof(af_fullscreen_vert),
                                     .pCode = af_fullscreen_vert};
    if (!ok(vkCreateShaderModule(p->device, &code, NULL, &vertex)))
        goto done;
    code.codeSize = sizeof(af_gi_denoise_frag);
    code.pCode = af_gi_denoise_frag;
    if (!ok(vkCreateShaderModule(p->device, &code, NULL, &fragment)))
        goto done;
    VkPipelineShaderStageCreateInfo stages[] = {{.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                                 .stage = VK_SHADER_STAGE_VERTEX_BIT,
                                                 .module = vertex,
                                                 .pName = "main"},
                                                {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                                 .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                                                 .module = fragment,
                                                 .pName = "main"}};
    VkPipelineVertexInputStateCreateInfo input = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo assembly = {.sType =
                                                           VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
                                                       .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    VkPipelineViewportStateCreateInfo viewport = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .scissorCount = 1};
    VkPipelineRasterizationStateCreateInfo raster = {.sType =
                                                         VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
                                                     .polygonMode = VK_POLYGON_MODE_FILL,
                                                     .cullMode = VK_CULL_MODE_NONE,
                                                     .lineWidth = 1};
    VkPipelineMultisampleStateCreateInfo samples = {.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
                                                    .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};
    VkPipelineColorBlendAttachmentState blend_attachments[2] = {
        {.colorWriteMask =
             VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT},
        {.blendEnable = VK_TRUE,
         .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
         .dstColorBlendFactor = VK_BLEND_FACTOR_ONE,
         .colorBlendOp = VK_BLEND_OP_ADD,
         .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
         .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
         .alphaBlendOp = VK_BLEND_OP_ADD,
         .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT}};
    VkPipelineColorBlendStateCreateInfo blend = {.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
                                                 .attachmentCount = 2,
                                                 .pAttachments = blend_attachments};
    VkDynamicState states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
                                                .dynamicStateCount = 2,
                                                .pDynamicStates = states};
    VkGraphicsPipelineCreateInfo info = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
                                         .stageCount = 2,
                                         .pStages = stages,
                                         .pVertexInputState = &input,
                                         .pInputAssemblyState = &assembly,
                                         .pViewportState = &viewport,
                                         .pRasterizationState = &raster,
                                         .pMultisampleState = &samples,
                                         .pColorBlendState = &blend,
                                         .pDynamicState = &dynamic,
                                         .layout = p->gi_pipeline_layout,
                                         .renderPass = p->gi_pass,
                                         .basePipelineIndex = -1};
    success = ok(vkCreateGraphicsPipelines(p->device, VK_NULL_HANDLE, 1, &info, NULL, &p->gi_pipeline));
done:
    if (vertex)
        vkDestroyShaderModule(p->device, vertex, NULL);
    if (fragment)
        vkDestroyShaderModule(p->device, fragment, NULL);
    return success;
}
uint64_t af_vk_post_allocation_bytes(const af_vk_post *p) {
    return p ? p->allocation_bytes : 0;
}
void af_vk_post_destroy(af_vk_post *p) {
    if (!p)
        return;
    for (uint32_t i = 0; p->frames && i < p->count; ++i) {
        post_frame *f = &p->frames[i];
        if (f->scene)
            vkDestroyFramebuffer(p->device, f->scene, NULL);
        if (f->output)
            vkDestroyFramebuffer(p->device, f->output, NULL);
        for (unsigned j = 0; j < 2; ++j)
            if (f->gi_framebuffer[j])
                vkDestroyFramebuffer(p->device, f->gi_framebuffer[j], NULL);
        post_image *images[] = {&f->hdr, &f->motion, &f->blur[0], &f->blur[1], &f->upscaled, &f->gi};
        for (unsigned j = 0; j < sizeof(images) / sizeof(images[0]); ++j) {
            post_image *im = images[j];
            if (im->framebuffer)
                vkDestroyFramebuffer(p->device, im->framebuffer, NULL);
            if (im->view)
                vkDestroyImageView(p->device, im->view, NULL);
            if (im->image)
                vkDestroyImage(p->device, im->image, NULL);
            if (im->memory)
                vkFreeMemory(p->device, im->memory, NULL);
        }
    }
    post_image *shared[] = {&p->display, &p->history[0], &p->history[1], &p->gi_history[0], &p->gi_history[1]};
    for (unsigned j = 0; j < sizeof(shared) / sizeof(shared[0]); ++j) {
        post_image *im = shared[j];
        if (im->framebuffer)
            vkDestroyFramebuffer(p->device, im->framebuffer, NULL);
        if (im->view)
            vkDestroyImageView(p->device, im->view, NULL);
        if (im->image)
            vkDestroyImage(p->device, im->image, NULL);
        if (im->memory)
            vkFreeMemory(p->device, im->memory, NULL);
    }
    af_vk_texture_destroy(p->area_lut);
    af_vk_texture_destroy(p->search_lut);
    if (p->blur_pipeline)
        vkDestroyPipeline(p->device, p->blur_pipeline, NULL);
    if (p->output_pipeline)
        vkDestroyPipeline(p->device, p->output_pipeline, NULL);
    if (p->gi_pipeline)
        vkDestroyPipeline(p->device, p->gi_pipeline, NULL);
    if (p->pipeline_layout)
        vkDestroyPipelineLayout(p->device, p->pipeline_layout, NULL);
    if (p->gi_pipeline_layout)
        vkDestroyPipelineLayout(p->device, p->gi_pipeline_layout, NULL);
    if (p->pool)
        vkDestroyDescriptorPool(p->device, p->pool, NULL);
    if (p->layout)
        vkDestroyDescriptorSetLayout(p->device, p->layout, NULL);
    if (p->gi_layout)
        vkDestroyDescriptorSetLayout(p->device, p->gi_layout, NULL);
    if (p->sampler)
        vkDestroySampler(p->device, p->sampler, NULL);
    if (p->blur_pass)
        vkDestroyRenderPass(p->device, p->blur_pass, NULL);
    if (p->output_pass)
        vkDestroyRenderPass(p->device, p->output_pass, NULL);
    if (p->gi_pass)
        vkDestroyRenderPass(p->device, p->gi_pass, NULL);
    free(p->frames);
    free(p);
}
af_vk_post *af_vk_post_create(VkPhysicalDevice physical, VkDevice device, VkQueue queue, uint32_t family,
                              VkExtent2D extent, VkExtent2D render_extent, uint32_t count, const VkImageView *outputs,
                              VkFormat format, VkRenderPass scene_pass, const VkImageView *depths,
                              af_render_settings settings, bool motion, bool gi, bool upscale) {
    /* The denoiser reprojects with motion vectors, so they are mandatory. */
    motion = motion || gi;
    VkFormatProperties properties;
    vkGetPhysicalDeviceFormatProperties(physical, VK_FORMAT_R16G16B16A16_SFLOAT, &properties);
    VkFormatFeatureFlags required = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                                    VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    if (upscale)
        required |= VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
    if ((properties.optimalTilingFeatures & required) != required) {
        af_log("error", "RGBA16F HDR color attachments with linear sampling are required.");
        return NULL;
    }
    af_vk_post *p = calloc(1, sizeof(*p));
    if (!p)
        return NULL;
    p->physical = physical;
    p->device = device;
    p->extent = extent;
    p->half = (VkExtent2D){(extent.width + 1) / 2, (extent.height + 1) / 2};
    p->render_extent = render_extent;
    p->count = count;
    p->adapted_exposure = 1.0f;
    p->gi = gi;
    p->frames = calloc(count, sizeof(*p->frames));
    if (!p->frames)
        goto fail;
    if (!pass(p, VK_FORMAT_R16G16B16A16_SFLOAT, false, &p->blur_pass) || !pass(p, format, true, &p->output_pass) ||
        (gi && !gi_render_pass(p)))
        goto fail;
    VkDescriptorSetLayoutBinding bindings[3];
    for (uint32_t i = 0; i < 3; ++i)
        bindings[i] = (VkDescriptorSetLayoutBinding){i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                                                     VK_SHADER_STAGE_FRAGMENT_BIT, NULL};
    VkDescriptorSetLayoutCreateInfo layout = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = 3, .pBindings = bindings};
    if (!ok(vkCreateDescriptorSetLayout(device, &layout, NULL, &p->layout)))
        goto fail;
    VkPushConstantRange range = {VK_SHADER_STAGE_FRAGMENT_BIT, 0, 128};
    VkPipelineLayoutCreateInfo pipeline_layout = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                                                  .setLayoutCount = 1,
                                                  .pSetLayouts = &p->layout,
                                                  .pushConstantRangeCount = 1,
                                                  .pPushConstantRanges = &range};
    if (!ok(vkCreatePipelineLayout(device, &pipeline_layout, NULL, &p->pipeline_layout)) ||
        !pipeline(p, p->blur_pass, &p->blur_pipeline) || !pipeline(p, p->output_pass, &p->output_pipeline))
        goto fail;
    if (gi) {
        if (!ok(vkCreateDescriptorSetLayout(device, &layout, NULL, &p->gi_layout)))
            goto fail;
        VkPushConstantRange gi_range = {VK_SHADER_STAGE_FRAGMENT_BIT, 0, 48};
        VkPipelineLayoutCreateInfo gi_pipeline_layout = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                                                         .setLayoutCount = 1,
                                                         .pSetLayouts = &p->gi_layout,
                                                         .pushConstantRangeCount = 1,
                                                         .pPushConstantRanges = &gi_range};
        if (!ok(vkCreatePipelineLayout(device, &gi_pipeline_layout, NULL, &p->gi_pipeline_layout)) || !gi_pipeline(p))
            goto fail;
    }
    VkSamplerCreateInfo sampler = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                                   .magFilter = VK_FILTER_LINEAR,
                                   .minFilter = VK_FILTER_LINEAR,
                                   .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
                                   .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                   .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                   .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE};
    if (!ok(vkCreateSampler(device, &sampler, NULL, &p->sampler)))
        goto fail;
    unsigned set_count = upscale ? UPSCALED_COMPOSITE_SET + 1u : UPSCALED_EXTRACT_SET;
    unsigned gi_sets = gi ? 2u : 0u, sets_per_frame = set_count + gi_sets;
    VkDescriptorPoolSize size = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, count * sets_per_frame * 3};
    VkDescriptorPoolCreateInfo pool = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                                       .maxSets = count * sets_per_frame,
                                       .poolSizeCount = 1,
                                       .pPoolSizes = &size};
    if (!ok(vkCreateDescriptorPool(device, &pool, NULL, &p->pool)))
        goto fail;
    post_image *shared[] = {&p->display, &p->history[0], &p->history[1]};
    unsigned shared_count = settings.anti_aliasing >= AF_AA_SMAA ? 3u : settings.anti_aliasing != AF_AA_OFF ? 1u : 0u;
    for (unsigned j = 0; j < shared_count; ++j)
        if (!image(p, shared[j], extent) ||
            !framebuffer(p, p->blur_pass, &shared[j]->view, 1, extent, &shared[j]->framebuffer))
            goto fail;
    if (gi)
        for (unsigned j = 0; j < 2; ++j)
            if (!image(p, &p->gi_history[j], render_extent))
                goto fail;
    if (settings.anti_aliasing == AF_AA_SMAA) {
        unsigned char *lut = malloc(AREATEX_WIDTH * AREATEX_HEIGHT * 4);
        if (!lut)
            goto fail;
        for (unsigned i = 0; i < AREATEX_WIDTH * AREATEX_HEIGHT; ++i) {
            lut[4 * i] = areaTexBytes[2 * i];
            lut[4 * i + 1] = areaTexBytes[2 * i + 1];
            lut[4 * i + 2] = 0;
            lut[4 * i + 3] = 255;
        }
        p->area_lut =
            af_vk_texture_create(physical, device, queue, family, lut, AREATEX_WIDTH, AREATEX_HEIGHT, false, false, 1);
        free(lut);
        if (!p->area_lut)
            goto fail;
        lut = malloc(SEARCHTEX_WIDTH * SEARCHTEX_HEIGHT * 4);
        if (!lut)
            goto fail;
        for (unsigned i = 0; i < SEARCHTEX_WIDTH * SEARCHTEX_HEIGHT; ++i) {
            lut[4 * i] = searchTexBytes[i];
            lut[4 * i + 1] = lut[4 * i + 2] = 0;
            lut[4 * i + 3] = 255;
        }
        p->search_lut = af_vk_texture_create(physical, device, queue, family, lut, SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT,
                                             false, false, 1);
        free(lut);
        if (!p->search_lut)
            goto fail;
        p->allocation_bytes +=
            af_vk_texture_info(p->area_lut).allocation_bytes + af_vk_texture_info(p->search_lut).allocation_bytes;
    }
    for (uint32_t i = 0; i < count; ++i) {
        post_frame *f = &p->frames[i];
        if (!image(p, &f->hdr, render_extent) || (motion && !image(p, &f->motion, render_extent)) ||
            (gi && !image(p, &f->gi, render_extent)) ||
            (upscale && !allocate_image(p, &f->upscaled, extent,
                                        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                            VK_IMAGE_USAGE_SAMPLED_BIT)) ||
            (settings.bloom && (!image(p, &f->blur[0], p->half) || !image(p, &f->blur[1], p->half))))
            goto fail;
        VkImageView views[] = {f->hdr.view, depths[i], f->motion.view, f->gi.view};
        if (!framebuffer(p, scene_pass, views,
                         gi       ? 4u
                         : motion ? 3u
                                  : 2u,
                         render_extent, &f->scene) ||
            !framebuffer(p, p->output_pass, &outputs[i], 1, extent, &f->output))
            goto fail;
        if (gi)
            for (unsigned parity = 0; parity < 2; ++parity) {
                VkImageView gi_views[] = {p->gi_history[parity].view, f->hdr.view};
                if (!framebuffer(p, p->gi_pass, gi_views, 2, render_extent, &f->gi_framebuffer[parity]))
                    goto fail;
            }
        for (unsigned j = 0; settings.bloom && j < 2; ++j)
            if (!framebuffer(p, p->blur_pass, &f->blur[j].view, 1, p->half, &f->blur[j].framebuffer))
                goto fail;
        VkDescriptorSetLayout layouts[UPSCALED_COMPOSITE_SET + 1];
        for (unsigned j = 0; j < set_count; ++j)
            layouts[j] = p->layout;
        VkDescriptorSetAllocateInfo allocate = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                                                .descriptorPool = p->pool,
                                                .descriptorSetCount = set_count,
                                                .pSetLayouts = layouts};
        if (!ok(vkAllocateDescriptorSets(device, &allocate, f->sets)))
            goto fail;
        /* Fixed descriptors for both history parities; no in-flight rewrites. */
        VkImageView sources[] = {f->hdr.view,        f->hdr.view,        f->blur[0].view,    f->blur[1].view,
                                 f->hdr.view,        p->display.view,    p->history[0].view, p->display.view,
                                 f->hdr.view,        f->hdr.view,        p->history[0].view, p->history[1].view,
                                 p->history[0].view, p->history[1].view, f->upscaled.view,   f->upscaled.view};
        for (unsigned j = 0; j < set_count; ++j)
            if (!sources[j])
                sources[j] = f->hdr.view;
        for (unsigned j = 0; j < set_count; ++j) {
            VkDescriptorImageInfo images[] = {
                {p->sampler, sources[j], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                {p->sampler,
                 j == 0 || j == 10 || j == 11 || j == UPSCALED_COMPOSITE_SET ? f->blur[0].view
                 : j == 7                                                    ? p->history[1].view
                 : j == 8                                                    ? p->history[1].view
                 : j == 9                                                    ? p->history[0].view
                                                                             : sources[j],
                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                p->search_lut
                    ? af_vk_texture_image(p->search_lut)
                    : (VkDescriptorImageInfo){p->sampler, f->hdr.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
            if (j == 6 && p->area_lut)
                images[1] = af_vk_texture_image(p->area_lut);
            if (motion && (j == 0 || j == 4 || j == 5 || j == 8 || j == 9 || j == 10 || j == 11 || j == 12 || j == 13 ||
                           j == UPSCALED_COMPOSITE_SET))
                images[2] =
                    (VkDescriptorImageInfo){p->sampler, f->motion.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            if (!images[1].imageView)
                images[1] = (VkDescriptorImageInfo){p->sampler, f->hdr.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            VkWriteDescriptorSet writes[3];
            for (uint32_t k = 0; k < 3; ++k)
                writes[k] = (VkWriteDescriptorSet){.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                                   .dstSet = f->sets[j],
                                                   .dstBinding = k,
                                                   .descriptorCount = 1,
                                                   .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                                   .pImageInfo = &images[k]};
            vkUpdateDescriptorSets(device, 3, writes, 0, NULL);
        }
        if (gi) {
            VkDescriptorSetLayout gi_layouts[2] = {p->gi_layout, p->gi_layout};
            VkDescriptorSetAllocateInfo gi_allocate = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                                                       .descriptorPool = p->pool,
                                                       .descriptorSetCount = 2,
                                                       .pSetLayouts = gi_layouts};
            if (!ok(vkAllocateDescriptorSets(device, &gi_allocate, f->gi_sets)))
                goto fail;
            /* Parity N writes history N and reads history 1-N. */
            for (unsigned parity = 0; parity < 2; ++parity) {
                VkDescriptorImageInfo gi_images[] = {
                    {p->sampler, f->gi.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                    {p->sampler, f->motion.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                    {p->sampler, p->gi_history[1 - parity].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
                VkWriteDescriptorSet gi_writes[3];
                for (uint32_t k = 0; k < 3; ++k)
                    gi_writes[k] = (VkWriteDescriptorSet){.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                                          .dstSet = f->gi_sets[parity],
                                                          .dstBinding = k,
                                                          .descriptorCount = 1,
                                                          .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                                          .pImageInfo = &gi_images[k]};
                vkUpdateDescriptorSets(device, 3, gi_writes, 0, NULL);
            }
        }
    }
    return p;
fail:
    af_vk_post_destroy(p);
    return NULL;
}
VkImage af_vk_post_motion_image(const af_vk_post *p, uint32_t index) {
    return p->frames[index].motion.image;
}
VkImage af_vk_post_scene_image(const af_vk_post *p, uint32_t image_index) {
    return p->frames[image_index].hdr.image;
}
void af_vk_post_upscaler_images(const af_vk_post *p, uint32_t index, af_vulkan_upscaler_frame *frame) {
    const post_frame *f = &p->frames[index];
    frame->color = (af_vulkan_upscaler_image){f->hdr.image, f->hdr.view};
    frame->motion = (af_vulkan_upscaler_image){f->motion.image, f->motion.view};
    frame->output = (af_vulkan_upscaler_image){f->upscaled.image, f->upscaled.view};
}
VkFramebuffer af_vk_post_scene_framebuffer(const af_vk_post *p, uint32_t image_index) {
    return p->frames[image_index].scene;
}
VkRenderPass af_vk_post_output_pass(const af_vk_post *p) {
    return p->output_pass;
}
void af_vk_post_reset_temporal(af_vk_post *p) {
    if (p) {
        p->history_valid = false;
        p->history_index = 0;
        p->gi_history_valid = false;
        p->gi_history_index = 0;
        p->adapted_exposure = 1.0f;
    }
}
void af_vk_post_denoise_gi(af_vk_post *p, VkCommandBuffer command, uint32_t index, af_rect area) {
    if (!p || !p->gi)
        return;
    post_frame *f = &p->frames[index];
    VkImageSubresourceRange subresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (!p->gi_initialized) {
        /* First use: clear both history buffers so the first reprojection reads zeros. */
        VkImageMemoryBarrier barriers[2];
        for (unsigned i = 0; i < 2; ++i)
            barriers[i] = (VkImageMemoryBarrier){.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                                                 .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                                                 .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                                 .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                                                 .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                                 .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                                 .image = p->gi_history[i].image,
                                                 .subresourceRange = subresource};
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0,
                             NULL, 2, barriers);
        VkClearColorValue zero = {0};
        for (unsigned i = 0; i < 2; ++i)
            vkCmdClearColorImage(command, p->gi_history[i].image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero, 1,
                                 &subresource);
        for (unsigned i = 0; i < 2; ++i) {
            barriers[i].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barriers[i].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barriers[i].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barriers[i].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        }
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL,
                             0, NULL, 2, barriers);
        p->gi_initialized = true;
        p->gi_history_valid = false;
    }
    VkExtent2D size = p->render_extent;
    unsigned parity = p->gi_history_index % 2;
    struct {
        af_vec4 viewport, texel, params;
    } push = {.viewport = {area.x, area.y, area.x + area.width, area.y + area.height},
              .texel = {1.0f / (float)size.width, 1.0f / (float)size.height, 0.08f, p->gi_history_valid ? 1.0f : 0.0f},
              .params = {4.0f, 2.5f, 0, 0}};
    VkClearValue clear = {0};
    VkRenderPassBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
                                   .renderPass = p->gi_pass,
                                   .framebuffer = f->gi_framebuffer[parity],
                                   .renderArea = {.extent = size},
                                   .clearValueCount = 1,
                                   .pClearValues = &clear};
    vkCmdBeginRenderPass(command, &begin, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport viewport = {.width = (float)size.width, .height = (float)size.height, .maxDepth = 1};
    VkRect2D scissor = {.extent = size};
    vkCmdSetViewport(command, 0, 1, &viewport);
    vkCmdSetScissor(command, 0, 1, &scissor);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, p->gi_pipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, p->gi_pipeline_layout, 0, 1, &f->gi_sets[parity],
                            0, NULL);
    vkCmdPushConstants(command, p->gi_pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
    vkCmdDraw(command, 3, 1, 0, 0);
    vkCmdEndRenderPass(command);
    p->gi_history_valid = true;
    ++p->gi_history_index;
}
static void draw_pass(af_vk_post *p, VkCommandBuffer command, VkFramebuffer target, bool output, VkExtent2D extent,
                      VkDescriptorSet set, const void *push) {
    VkClearValue clear = {0};
    VkRenderPassBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
                                   .renderPass = output ? p->output_pass : p->blur_pass,
                                   .framebuffer = target,
                                   .renderArea = {.extent = extent},
                                   .clearValueCount = 1,
                                   .pClearValues = &clear};
    vkCmdBeginRenderPass(command, &begin, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport viewport = {.width = (float)extent.width, .height = (float)extent.height, .maxDepth = 1};
    VkRect2D scissor = {.extent = extent};
    vkCmdSetViewport(command, 0, 1, &viewport);
    vkCmdSetScissor(command, 0, 1, &scissor);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, output ? p->output_pipeline : p->blur_pipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, p->pipeline_layout, 0, 1, &set, 0, NULL);
    vkCmdPushConstants(command, p->pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, 128, push);
    vkCmdDraw(command, 3, 1, 0, 0);
    if (!output)
        vkCmdEndRenderPass(command);
}
void af_vk_post_draw(af_vk_post *p, VkCommandBuffer command, uint32_t index, af_render_settings settings, af_rect area,
                     af_vec2 jitter, af_debug_view debug_view, bool upscaled, float time_seconds, float delta_time) {
    post_frame *f = &p->frames[index];
    float target_exposure = exp2f(
        settings.auto_exposure ? fminf(fmaxf(settings.exposure, settings.auto_exposure_min), settings.auto_exposure_max)
                               : settings.exposure);
    if (p->adapted_exposure <= 0.0f || !isfinite(p->adapted_exposure))
        p->adapted_exposure = target_exposure;
    if (delta_time > 0.0f && settings.auto_exposure && settings.auto_exposure_speed > 0.0f) {
        float blend = 1.0f - expf(-settings.auto_exposure_speed * delta_time);
        p->adapted_exposure += (target_exposure - p->adapted_exposure) * blend;
    } else {
        p->adapted_exposure = target_exposure;
    }
    float sat = settings.saturation <= 0.0f && settings.contrast <= 0.0f && settings.color_filter.x <= 0.0f &&
                        settings.color_filter.y <= 0.0f && settings.color_filter.z <= 0.0f
                    ? 1.0f
                    : settings.saturation;
    float con = settings.contrast <= 0.0f ? 1.0f : settings.contrast;
    af_vec3 filter =
        (settings.color_filter.x <= 0.0f && settings.color_filter.y <= 0.0f && settings.color_filter.z <= 0.0f)
            ? (af_vec3){1.0f, 1.0f, 1.0f}
            : settings.color_filter;
    struct {
        af_vec4 settings, filter, viewport, aa, temporal, grading, lens, dof_grain;
    } push = {
        .settings = {p->adapted_exposure,
                     !settings.tone_mapping ? 0.0f
                                            : (settings.tone_mapper == AF_TONE_MAP_OFF ? (float)AF_TONE_MAP_REINHARD
                                                                                       : (float)settings.tone_mapper),
                     settings.bloom ? settings.bloom_strength : 0.0f, settings.bloom_threshold},
        .filter = {0, 0, 0, settings.bloom ? settings.bloom_radius : 0.0f},
        .viewport = {area.x, area.y, area.x + area.width, area.y + area.height},
        .aa = {(float)settings.anti_aliasing, settings.aa_sharpness, settings.taa_history_weight,
               p->history_valid ? 1.0f : 0.0f},
        .temporal = {jitter.x, jitter.y, time_seconds, settings.motion_blur ? settings.motion_blur_strength : 0.0f},
        .grading = {sat, con, filter.x, filter.y},
        .lens = {filter.z, settings.vignette ? settings.vignette_intensity : 0.0f, settings.vignette_smoothness,
                 settings.chromatic_aberration ? settings.chromatic_aberration_strength : 0.0f},
        .dof_grain = {settings.dof_focus_distance, settings.dof_focus_range,
                      settings.depth_of_field ? settings.dof_blur_radius : 0.0f,
                      settings.film_grain ? settings.film_grain_intensity : 0.0f}};
    bool bloom = settings.bloom && settings.bloom_strength > 0, aa = settings.anti_aliasing != AF_AA_OFF,
         taa = settings.anti_aliasing == AF_AA_TAA;
    unsigned parity = p->history_index % 2;
    if (taa) {
        if (!p->initialized) {
            VkImageMemoryBarrier barriers[2];
            for (unsigned i = 0; i < 2; ++i)
                barriers[i] = (VkImageMemoryBarrier){.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                                                     .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                                                     .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                                     .srcAccessMask = VK_ACCESS_SHADER_READ_BIT |
                                                                      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                                     .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                                                     .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                                     .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                                     .image = p->history[i].image,
                                                     .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
            vkCmdPipelineBarrier(command,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 2, barriers);
            VkClearColorValue zero = {0};
            for (unsigned i = 0; i < 2; ++i)
                vkCmdClearColorImage(command, p->history[i].image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero, 1,
                                     &barriers[i].subresourceRange);
            for (unsigned i = 0; i < 2; ++i) {
                barriers[i].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                barriers[i].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                barriers[i].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                barriers[i].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            }
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
                                 NULL, 0, NULL, 2, barriers);
            p->initialized = true;
        }
        push.filter.z = 10;
        draw_pass(p, command, p->history[parity].framebuffer, false, p->extent, f->sets[8 + parity], &push);
        p->history_valid = true;
        ++p->history_index;
    } else
        p->history_valid = false;
    if (debug_view != AF_DEBUG_SHADED) {
        push.filter.z = (float)(10 + (unsigned)debug_view);
        draw_pass(p, command, f->output, true, p->extent, f->sets[8 + parity], &push);
        return;
    }
    unsigned last = settings.anti_aliasing == AF_AA_SMAA ? 7u : aa ? 5u : 4u;
    for (unsigned stage = bloom ? 1u : 4u; stage <= last; ++stage) {
        bool output = stage == last;
        VkExtent2D extent = stage >= 4 ? p->extent : p->half;
        unsigned mode = stage == 4 ? (aa ? 5u : 0u) : stage >= 5 ? (last == 7 ? stage + 2 : 6u) : stage;
        VkFramebuffer target = output       ? f->output
                               : stage == 4 ? p->display.framebuffer
                               : stage == 5 ? p->history[0].framebuffer
                               : stage == 6 ? p->history[1].framebuffer
                                            : f->blur[stage == 2 ? 1 : 0].framebuffer;
        push.filter.x = 1.0f / (float)(stage == 1 ? p->extent.width : p->half.width);
        push.filter.y = 1.0f / (float)(stage == 1 ? p->extent.height : p->half.height);
        push.filter.z = (float)mode;
        unsigned set = stage >= 5 ? stage : stage == 4 ? (bloom ? 0u : 4u) : stage;
        if (taa && stage == 1)
            set = 12 + parity;
        if (taa && stage == 4)
            set = bloom ? 10 + parity : 12 + parity;
        if (upscaled && stage == 1)
            set = UPSCALED_EXTRACT_SET;
        if (upscaled && stage == 4)
            set = UPSCALED_COMPOSITE_SET;
        draw_pass(p, command, target, output, extent, f->sets[set], &push);
    }
}
