/* SPDX-License-Identifier: MPL-2.0 */
#include "post_vulkan.h"
#include "../../third_party/smaa/AreaTex.h"
#include "../../third_party/smaa/SearchTex.h"
#include "fullscreen_vert.h"
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
typedef struct post_frame {
  post_image hdr, motion, blur[2];
  VkFramebuffer scene, output;
  VkDescriptorSet sets[14];
} post_frame;
struct af_vk_post {
  VkPhysicalDevice physical;
  VkDevice device;
  VkExtent2D extent, half;
  uint32_t count;
  uint64_t allocation_bytes;
  unsigned allocation_attempts;
  VkRenderPass blur_pass, output_pass;
  VkPipeline blur_pipeline, output_pipeline;
  VkPipelineLayout pipeline_layout;
  VkDescriptorSetLayout layout;
  VkDescriptorPool pool;
  VkSampler sampler;
  post_frame *frames;
  post_image display, history[2];
  bool initialized, history_valid;
  unsigned history_index;
  af_vk_texture *area_lut, *search_lut;
};
static bool ok(VkResult r) {
  if (r == VK_SUCCESS)
    return true;
  af_log("error", "HDR/post resource failed (%d).", (int)r);
  return false;
}
static bool framebuffer(af_vk_post *p, VkRenderPass pass,
                        const VkImageView *views, uint32_t count,
                        VkExtent2D extent, VkFramebuffer *out) {
  VkFramebufferCreateInfo info = {.sType =
                                      VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
                                  .renderPass = pass,
                                  .attachmentCount = count,
                                  .pAttachments = views,
                                  .width = extent.width,
                                  .height = extent.height,
                                  .layers = 1};
  return ok(vkCreateFramebuffer(p->device, &info, NULL, out));
}
static bool image(af_vk_post *p, post_image *out, VkExtent2D extent) {
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
                            .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                     VK_IMAGE_USAGE_SAMPLED_BIT |
                                     VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                     VK_IMAGE_USAGE_TRANSFER_DST_BIT,
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
        (properties.memoryTypes[i].propertyFlags &
         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
      type = i;
      break;
    }
  if (type == UINT32_MAX)
    return false;
  p->allocation_bytes += requirements.size;
  VkMemoryAllocateInfo allocation = {.sType =
                                         VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                                     .allocationSize = requirements.size,
                                     .memoryTypeIndex = type};
  if (!ok(vkAllocateMemory(p->device, &allocation, NULL, &out->memory)) ||
      !ok(vkBindImageMemory(p->device, out->image, out->memory, 0)))
    return false;
  VkImageViewCreateInfo view = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image = out->image,
      .viewType = VK_IMAGE_VIEW_TYPE_2D,
      .format = info.format,
      .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
  return ok(vkCreateImageView(p->device, &view, NULL, &out->view));
}
static bool pass(af_vk_post *p, VkFormat format, bool output,
                 VkRenderPass *out) {
  VkAttachmentDescription attachment = {
      .format = format,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
      .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
      .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
      .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
      .finalLayout = output ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
                            : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  VkAttachmentReference reference = {0,
                                     VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkSubpassDescription subpass = {.pipelineBindPoint =
                                      VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  .colorAttachmentCount = 1,
                                  .pColorAttachments = &reference};
  VkSubpassDependency dependencies[] = {
      {.srcSubpass = VK_SUBPASS_EXTERNAL,
       .dstSubpass = 0,
       .srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                       VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
       .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
       .srcAccessMask =
           VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
       .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT},
      {.srcSubpass = 0,
       .dstSubpass = VK_SUBPASS_EXTERNAL,
       .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
       .dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
       .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
       .dstAccessMask = VK_ACCESS_SHADER_READ_BIT}};
  VkRenderPassCreateInfo info = {.sType =
                                     VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
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
  VkShaderModuleCreateInfo code = {
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = sizeof(af_fullscreen_vert),
      .pCode = af_fullscreen_vert};
  if (!ok(vkCreateShaderModule(p->device, &code, NULL, &vertex)))
    goto done;
  code.codeSize = sizeof(af_post_frag);
  code.pCode = af_post_frag;
  if (!ok(vkCreateShaderModule(p->device, &code, NULL, &fragment)))
    goto done;
  VkPipelineShaderStageCreateInfo stages[] = {
      {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
       .stage = VK_SHADER_STAGE_VERTEX_BIT,
       .module = vertex,
       .pName = "main"},
      {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
       .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
       .module = fragment,
       .pName = "main"}};
  VkPipelineVertexInputStateCreateInfo input = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  VkPipelineInputAssemblyStateCreateInfo assembly = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
  VkPipelineViewportStateCreateInfo viewport = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
      .viewportCount = 1,
      .scissorCount = 1};
  VkPipelineRasterizationStateCreateInfo raster = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .polygonMode = VK_POLYGON_MODE_FILL,
      .cullMode = VK_CULL_MODE_NONE,
      .lineWidth = 1};
  VkPipelineMultisampleStateCreateInfo samples = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};
  VkPipelineColorBlendAttachmentState attachment = {.colorWriteMask = 15};
  VkPipelineColorBlendStateCreateInfo blend = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .attachmentCount = 1,
      .pAttachments = &attachment};
  VkDynamicState states[] = {VK_DYNAMIC_STATE_VIEWPORT,
                             VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dynamic = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
      .dynamicStateCount = 2,
      .pDynamicStates = states};
  VkGraphicsPipelineCreateInfo info = {
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
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
  success = ok(vkCreateGraphicsPipelines(p->device, VK_NULL_HANDLE, 1, &info,
                                         NULL, out));
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
    post_image *images[] = {&f->hdr, &f->motion, &f->blur[0], &f->blur[1]};
    for (unsigned j = 0; j < 4; ++j) {
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
  post_image *shared[] = {&p->display, &p->history[0], &p->history[1]};
  for (unsigned j = 0; j < 3; ++j) {
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
  if (p->pipeline_layout)
    vkDestroyPipelineLayout(p->device, p->pipeline_layout, NULL);
  if (p->pool)
    vkDestroyDescriptorPool(p->device, p->pool, NULL);
  if (p->layout)
    vkDestroyDescriptorSetLayout(p->device, p->layout, NULL);
  if (p->sampler)
    vkDestroySampler(p->device, p->sampler, NULL);
  if (p->blur_pass)
    vkDestroyRenderPass(p->device, p->blur_pass, NULL);
  if (p->output_pass)
    vkDestroyRenderPass(p->device, p->output_pass, NULL);
  free(p->frames);
  free(p);
}
af_vk_post *af_vk_post_create(VkPhysicalDevice physical, VkDevice device,
                              VkQueue queue, uint32_t family, VkExtent2D extent,
                              uint32_t count, const VkImageView *outputs,
                              VkFormat format, VkRenderPass scene_pass,
                              const VkImageView *depths,
                              af_render_settings settings, bool motion) {
  VkFormatProperties properties;
  vkGetPhysicalDeviceFormatProperties(physical, VK_FORMAT_R16G16B16A16_SFLOAT,
                                      &properties);
  VkFormatFeatureFlags required =
      VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
      VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
      VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
  if ((properties.optimalTilingFeatures & required) != required) {
    af_log("error",
           "RGBA16F HDR color attachments with linear sampling are required.");
    return NULL;
  }
  af_vk_post *p = calloc(1, sizeof(*p));
  if (!p)
    return NULL;
  p->physical = physical;
  p->device = device;
  p->extent = extent;
  p->half = (VkExtent2D){(extent.width + 1) / 2, (extent.height + 1) / 2};
  p->count = count;
  p->frames = calloc(count, sizeof(*p->frames));
  if (!p->frames)
    goto fail;
  if (!pass(p, VK_FORMAT_R16G16B16A16_SFLOAT, false, &p->blur_pass) ||
      !pass(p, format, true, &p->output_pass))
    goto fail;
  VkDescriptorSetLayoutBinding bindings[3];
  for (uint32_t i = 0; i < 3; ++i)
    bindings[i] = (VkDescriptorSetLayoutBinding){
        i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
        VK_SHADER_STAGE_FRAGMENT_BIT, NULL};
  VkDescriptorSetLayoutCreateInfo layout = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .bindingCount = 3,
      .pBindings = bindings};
  if (!ok(vkCreateDescriptorSetLayout(device, &layout, NULL, &p->layout)))
    goto fail;
  VkPushConstantRange range = {VK_SHADER_STAGE_FRAGMENT_BIT, 0, 80};
  VkPipelineLayoutCreateInfo pipeline_layout = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 1,
      .pSetLayouts = &p->layout,
      .pushConstantRangeCount = 1,
      .pPushConstantRanges = &range};
  if (!ok(vkCreatePipelineLayout(device, &pipeline_layout, NULL,
                                 &p->pipeline_layout)) ||
      !pipeline(p, p->blur_pass, &p->blur_pipeline) ||
      !pipeline(p, p->output_pass, &p->output_pipeline))
    goto fail;
  VkSamplerCreateInfo sampler = {
      .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
      .magFilter = VK_FILTER_LINEAR,
      .minFilter = VK_FILTER_LINEAR,
      .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
      .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE};
  if (!ok(vkCreateSampler(device, &sampler, NULL, &p->sampler)))
    goto fail;
  VkDescriptorPoolSize size = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                               count * 42};
  VkDescriptorPoolCreateInfo pool = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .maxSets = count * 14,
      .poolSizeCount = 1,
      .pPoolSizes = &size};
  if (!ok(vkCreateDescriptorPool(device, &pool, NULL, &p->pool)))
    goto fail;
  post_image *shared[] = {&p->display, &p->history[0], &p->history[1]};
  unsigned shared_count = settings.anti_aliasing >= AF_AA_SMAA  ? 3u
                          : settings.anti_aliasing != AF_AA_OFF ? 1u
                                                                : 0u;
  for (unsigned j = 0; j < shared_count; ++j)
    if (!image(p, shared[j], extent) ||
        !framebuffer(p, p->blur_pass, &shared[j]->view, 1, extent,
                     &shared[j]->framebuffer))
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
        af_vk_texture_create(physical, device, queue, family, lut,
                             AREATEX_WIDTH, AREATEX_HEIGHT, false, false, 1);
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
    p->search_lut = af_vk_texture_create(physical, device, queue, family, lut,
                                         SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT,
                                         false, false, 1);
    free(lut);
    if (!p->search_lut)
      goto fail;
    p->allocation_bytes += af_vk_texture_info(p->area_lut).allocation_bytes +
                           af_vk_texture_info(p->search_lut).allocation_bytes;
  }
  for (uint32_t i = 0; i < count; ++i) {
    post_frame *f = &p->frames[i];
    if (!image(p, &f->hdr, extent) ||
        (motion && !image(p, &f->motion, extent)) ||
        (settings.bloom &&
         (!image(p, &f->blur[0], p->half) || !image(p, &f->blur[1], p->half))))
      goto fail;
    VkImageView views[] = {f->hdr.view, depths[i], f->motion.view};
    if (!framebuffer(p, scene_pass, views, motion ? 3u : 2u, extent,
                     &f->scene) ||
        !framebuffer(p, p->output_pass, &outputs[i], 1, extent, &f->output))
      goto fail;
    for (unsigned j = 0; settings.bloom && j < 2; ++j)
      if (!framebuffer(p, p->blur_pass, &f->blur[j].view, 1, p->half,
                       &f->blur[j].framebuffer))
        goto fail;
    VkDescriptorSetLayout layouts[14];
    for (unsigned j = 0; j < 14; ++j)
      layouts[j] = p->layout;
    VkDescriptorSetAllocateInfo allocate = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = p->pool,
        .descriptorSetCount = 14,
        .pSetLayouts = layouts};
    if (!ok(vkAllocateDescriptorSets(device, &allocate, f->sets)))
      goto fail;
    /* Fixed descriptors for both history parities; no in-flight rewrites. */
    VkImageView sources[] = {
        f->hdr.view,        f->hdr.view,        f->blur[0].view,
        f->blur[1].view,    f->hdr.view,        p->display.view,
        p->history[0].view, p->display.view,    f->hdr.view,
        f->hdr.view,        p->history[0].view, p->history[1].view,
        p->history[0].view, p->history[1].view};
    for (unsigned j = 0; j < 14; ++j)
      if (!sources[j])
        sources[j] = f->hdr.view;
    for (unsigned j = 0; j < 14; ++j) {
      VkDescriptorImageInfo images[] = {
          {p->sampler, sources[j], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
          {p->sampler,
           j == 0 || j == 10 || j == 11 ? f->blur[0].view
           : j == 7                     ? p->history[1].view
           : j == 8                     ? p->history[1].view
           : j == 9                     ? p->history[0].view
                                        : sources[j],
           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
          p->search_lut ? af_vk_texture_image(p->search_lut)
                        : (VkDescriptorImageInfo){
                              p->sampler, f->hdr.view,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
      if (j == 6 && p->area_lut)
        images[1] = af_vk_texture_image(p->area_lut);
      if (motion && (j == 5 || j == 8 || j == 9))
        images[2] =
            (VkDescriptorImageInfo){p->sampler, f->motion.view,
                                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
      if (!images[1].imageView)
        images[1] = (VkDescriptorImageInfo){
            p->sampler, f->hdr.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
      VkWriteDescriptorSet writes[3];
      for (uint32_t k = 0; k < 3; ++k)
        writes[k] = (VkWriteDescriptorSet){
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = f->sets[j],
            .dstBinding = k,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &images[k]};
      vkUpdateDescriptorSets(device, 3, writes, 0, NULL);
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
VkFramebuffer af_vk_post_scene_framebuffer(const af_vk_post *p,
                                           uint32_t image_index) {
  return p->frames[image_index].scene;
}
VkRenderPass af_vk_post_output_pass(const af_vk_post *p) {
  return p->output_pass;
}
void af_vk_post_reset_temporal(af_vk_post *p) {
  if (p) {
    p->history_valid = false;
    p->history_index = 0;
  }
}
static void draw_pass(af_vk_post *p, VkCommandBuffer command,
                      VkFramebuffer target, bool output, VkExtent2D extent,
                      VkDescriptorSet set, const void *push) {
  VkClearValue clear = {0};
  VkRenderPassBeginInfo begin = {
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
      .renderPass = output ? p->output_pass : p->blur_pass,
      .framebuffer = target,
      .renderArea = {.extent = extent},
      .clearValueCount = 1,
      .pClearValues = &clear};
  vkCmdBeginRenderPass(command, &begin, VK_SUBPASS_CONTENTS_INLINE);
  VkViewport viewport = {.width = (float)extent.width,
                         .height = (float)extent.height,
                         .maxDepth = 1};
  VkRect2D scissor = {.extent = extent};
  vkCmdSetViewport(command, 0, 1, &viewport);
  vkCmdSetScissor(command, 0, 1, &scissor);
  vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    output ? p->output_pipeline : p->blur_pipeline);
  vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          p->pipeline_layout, 0, 1, &set, 0, NULL);
  vkCmdPushConstants(command, p->pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT,
                     0, 80, push);
  vkCmdDraw(command, 3, 1, 0, 0);
  if (!output)
    vkCmdEndRenderPass(command);
}
void af_vk_post_draw(af_vk_post *p, VkCommandBuffer command, uint32_t index,
                     af_render_settings settings, af_rect area, af_vec2 jitter,
                     af_debug_view debug_view) {
  post_frame *f = &p->frames[index];
  struct {
    af_vec4 settings, filter, viewport, aa, temporal;
  } push = {{exp2f(settings.exposure), settings.tone_mapping ? 1.0f : 0.0f,
             settings.bloom ? settings.bloom_strength : 0,
             settings.bloom_threshold},
            {0, 0, 0, settings.bloom_radius},
            {area.x, area.y, area.x + area.width, area.y + area.height},
            {(float)settings.anti_aliasing, settings.aa_sharpness,
             settings.taa_history_weight, p->history_valid ? 1.f : 0},
            {jitter.x, jitter.y, 0, 0}};
  bool bloom = settings.bloom && settings.bloom_strength > 0,
       aa = settings.anti_aliasing != AF_AA_OFF,
       taa = settings.anti_aliasing == AF_AA_TAA;
  unsigned parity = p->history_index % 2;
  if (taa) {
    if (!p->initialized) {
      VkImageMemoryBarrier barriers[2];
      for (unsigned i = 0; i < 2; ++i)
        barriers[i] = (VkImageMemoryBarrier){
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
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
                           VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                               VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                           VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL,
                           2, barriers);
      VkClearColorValue zero = {0};
      for (unsigned i = 0; i < 2; ++i)
        vkCmdClearColorImage(command, p->history[i].image,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero, 1,
                             &barriers[i].subresourceRange);
      for (unsigned i = 0; i < 2; ++i) {
        barriers[i].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barriers[i].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barriers[i].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barriers[i].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
      }
      vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                           VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0,
                           NULL, 2, barriers);
      p->initialized = true;
    }
    push.filter.z = 10;
    draw_pass(p, command, p->history[parity].framebuffer, false, p->extent,
              f->sets[8 + parity], &push);
    p->history_valid = true;
    ++p->history_index;
  } else
    p->history_valid = false;
  if (debug_view != AF_DEBUG_SHADED) {
    push.filter.z = (float)(10 + (unsigned)debug_view);
    draw_pass(p, command, f->output, true, p->extent, f->sets[8 + parity],
              &push);
    return;
  }
  unsigned last = settings.anti_aliasing == AF_AA_SMAA ? 7u : aa ? 5u : 4u;
  for (unsigned stage = bloom ? 1u : 4u; stage <= last; ++stage) {
    bool output = stage == last;
    VkExtent2D extent = stage >= 4 ? p->extent : p->half;
    unsigned mode = stage == 4   ? (aa ? 5u : 0u)
                    : stage >= 5 ? (last == 7 ? stage + 2 : 6u)
                                 : stage;
    VkFramebuffer target = output       ? f->output
                           : stage == 4 ? p->display.framebuffer
                           : stage == 5 ? p->history[0].framebuffer
                           : stage == 6
                               ? p->history[1].framebuffer
                               : f->blur[stage == 2 ? 1 : 0].framebuffer;
    push.filter.x =
        1.0f / (float)(stage == 1 ? p->extent.width : p->half.width);
    push.filter.y =
        1.0f / (float)(stage == 1 ? p->extent.height : p->half.height);
    push.filter.z = (float)mode;
    unsigned set = stage >= 5 ? stage : stage == 4 ? (bloom ? 0u : 4u) : stage;
    if (taa && stage == 1)
      set = 12 + parity;
    if (taa && stage == 4)
      set = bloom ? 10 + parity : 12 + parity;
    draw_pass(p, command, target, output, extent, f->sets[set], &push);
  }
}
