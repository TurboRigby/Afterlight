/* SPDX-License-Identifier: MPL-2.0 */
#include <afterlight/extension_vulkan.h>
#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <new>

#include "encode_spv.h"
#include "easu_spv.h"
#include "rcas_spv.h"

#define A_CPU 1
#include <ffx_a.h>
#include <ffx_fsr1.h>
#undef A_CPU

namespace {
enum Pass { Encode, Easu, Rcas, PassCount };

struct Image {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
};

struct Slot {
    Image encoded, scaled;
    std::array<VkDescriptorSet, PassCount> sets{};
};

struct Constants {
    uint32_t easu[4][4], rcas[4];
};

struct Context {
    af_vulkan_upscaler_config config;
    std::unique_ptr<Slot[]> slots;
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    std::array<VkPipeline, PassCount> pipelines{};
    Constants constants{};

    explicit Context(const af_vulkan_upscaler_config &value) : config(value) {}
    ~Context()
    {
        VkDevice device = config.device;
        for (VkPipeline pipeline : pipelines)
            if (pipeline) vkDestroyPipeline(device, pipeline, nullptr);
        if (pool) vkDestroyDescriptorPool(device, pool, nullptr);
        if (slots) {
            for (uint32_t i = 0; i < config.frames_in_flight; ++i) {
                for (const Image *image : {&slots[i].encoded, &slots[i].scaled}) {
                    if (image->view) vkDestroyImageView(device, image->view, nullptr);
                    if (image->image) vkDestroyImage(device, image->image, nullptr);
                    if (image->memory) vkFreeMemory(device, image->memory, nullptr);
                }
            }
        }
        if (sampler) vkDestroySampler(device, sampler, nullptr);
        if (layout) vkDestroyPipelineLayout(device, layout, nullptr);
        if (set_layout) vkDestroyDescriptorSetLayout(device, set_layout, nullptr);
    }
};

bool image_create(const Context &ctx, VkExtent2D extent, Image &image)
{
    VkDevice device = ctx.config.device;
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    info.extent = {extent.width, extent.height, 1};
    info.mipLevels = info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    if (vkCreateImage(device, &info, nullptr, &image.image) != VK_SUCCESS) return false;
    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device, image.image, &requirements);
    VkPhysicalDeviceMemoryProperties memory{};
    vkGetPhysicalDeviceMemoryProperties(ctx.config.physical_device, &memory);
    uint32_t type = 0;
    for (; type < memory.memoryTypeCount; ++type)
        if ((requirements.memoryTypeBits & (1u << type)) &&
            (memory.memoryTypes[type].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) break;
    if (type == memory.memoryTypeCount) return false;
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = type;
    if (vkAllocateMemory(device, &allocation, nullptr, &image.memory) != VK_SUCCESS ||
        vkBindImageMemory(device, image.image, image.memory, 0) != VK_SUCCESS) return false;
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = image.image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = info.format;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    return vkCreateImageView(device, &view, nullptr, &image.view) == VK_SUCCESS;
}

bool make_pipeline(Context &ctx, Pass pass, const uint32_t *code, size_t size)
{
    VkShaderModuleCreateInfo shader{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    shader.codeSize = size;
    shader.pCode = code;
    VkShaderModule module;
    VkDevice device = ctx.config.device;
    if (vkCreateShaderModule(device, &shader, nullptr, &module) != VK_SUCCESS) return false;
    VkComputePipelineCreateInfo pipeline{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    pipeline.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipeline.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipeline.stage.module = module;
    pipeline.stage.pName = "main";
    pipeline.layout = ctx.layout;
    VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipeline,
                                                nullptr, &ctx.pipelines[pass]);
    vkDestroyShaderModule(device, module, nullptr);
    return result == VK_SUCCESS;
}

bool setup(Context &ctx)
{
    VkDevice device = ctx.config.device;
    float width = static_cast<float>(ctx.config.render_extent.width);
    float height = static_cast<float>(ctx.config.render_extent.height);
    auto &easu = ctx.constants.easu;
    FsrEasuCon(easu[0], easu[1], easu[2], easu[3], width, height, width, height,
               static_cast<float>(ctx.config.output_extent.width),
               static_cast<float>(ctx.config.output_extent.height));
    FsrRcasCon(ctx.constants.rcas, .75f);
    VkDescriptorSetLayoutBinding bindings[] = {
        {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
    };
    VkDescriptorSetLayoutCreateInfo sets{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    sets.bindingCount = 2;
    sets.pBindings = bindings;
    if (vkCreateDescriptorSetLayout(device, &sets, nullptr, &ctx.set_layout) != VK_SUCCESS) return false;
    VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Constants)};
    layout.setLayoutCount = 1;
    layout.pSetLayouts = &ctx.set_layout;
    layout.pushConstantRangeCount = 1;
    layout.pPushConstantRanges = &push;
    if (vkCreatePipelineLayout(device, &layout, nullptr, &ctx.layout) != VK_SUCCESS) return false;

    VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(device, &sampler, nullptr, &ctx.sampler) != VK_SUCCESS) return false;
    uint32_t count = ctx.config.frames_in_flight;
    uint32_t set_count = count * PassCount;
    VkDescriptorPoolSize sizes[] = {
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, set_count},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, set_count},
    };
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool.maxSets = set_count;
    pool.poolSizeCount = 2;
    pool.pPoolSizes = sizes;
    if (vkCreateDescriptorPool(device, &pool, nullptr, &ctx.pool) != VK_SUCCESS) return false;
    ctx.slots.reset(new (std::nothrow) Slot[count]);
    if (!ctx.slots) return false;
    std::array<VkDescriptorSetLayout, PassCount> layouts;
    layouts.fill(ctx.set_layout);
    VkDescriptorSetAllocateInfo allocation{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocation.descriptorPool = ctx.pool;
    allocation.descriptorSetCount = PassCount;
    allocation.pSetLayouts = layouts.data();
    for (uint32_t i = 0; i < count; ++i) {
        Slot &slot = ctx.slots[i];
        if (!image_create(ctx, ctx.config.render_extent, slot.encoded) ||
            !image_create(ctx, ctx.config.output_extent, slot.scaled) ||
            vkAllocateDescriptorSets(device, &allocation, slot.sets.data()) != VK_SUCCESS) return false;
    }
    return make_pipeline(ctx, Encode, af_fsr_encode, sizeof(af_fsr_encode)) &&
           make_pipeline(ctx, Easu, af_fsr_easu, sizeof(af_fsr_easu)) &&
           make_pipeline(ctx, Rcas, af_fsr_rcas, sizeof(af_fsr_rcas));
}

bool compute_queue_supported(const af_vulkan_upscaler_config &config)
{
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(config.physical_device, &count, nullptr);
    auto families = std::unique_ptr<VkQueueFamilyProperties[]>(new (std::nothrow) VkQueueFamilyProperties[count]);
    if (!families) return false;
    vkGetPhysicalDeviceQueueFamilyProperties(config.physical_device, &count, families.get());
    return config.graphics_queue_family < count &&
           (families[config.graphics_queue_family].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0;
}

int32_t start(const af_extension_host *host, void **state)
{
    *state = const_cast<af_extension_host *>(host);
    return 0;
}

bool create(void *state, const af_vulkan_upscaler_config *config, void **out)
{
    auto *host = static_cast<const af_extension_host *>(state);
    if (config->struct_size < sizeof(*config) || config->frames_in_flight == 0 ||
        config->color_format != VK_FORMAT_R16G16B16A16_SFLOAT ||
        config->output_format != VK_FORMAT_R16G16B16A16_SFLOAT ||
        !compute_queue_supported(*config)) {
        host->log("AMD FSR 1.0 requires RGBA16F HDR and compute on the graphics queue.");
        return false;
    }
    std::unique_ptr<Context> context(new (std::nothrow) Context(*config));
    if (!context || !setup(*context)) {
        host->log("Could not create AMD FSR 1.0 Vulkan resources.");
        return false;
    }
    *out = context.release();
    char message[128];
    std::snprintf(message, sizeof(message), "AMD FSR 1.0 (EASU + RCAS): %ux%u -> %ux%u.",
                  config->render_extent.width, config->render_extent.height,
                  config->output_extent.width, config->output_extent.height);
    host->log(message);
    return true;
}

void transition(VkCommandBuffer command, VkImage image, bool sampled)
{
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.srcAccessMask = sampled ? VK_ACCESS_SHADER_WRITE_BIT : 0;
    barrier.dstAccessMask = sampled ? VK_ACCESS_SHADER_READ_BIT : VK_ACCESS_SHADER_WRITE_BIT;
    barrier.oldLayout = sampled ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = sampled ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(command, sampled ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void dispatch(const Context &ctx, VkCommandBuffer command, Pass pass, VkDescriptorSet set,
              VkImageView input, VkImageView output, VkExtent2D extent)
{
    VkDescriptorImageInfo images[] = {
        {ctx.sampler, input, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {VK_NULL_HANDLE, output, VK_IMAGE_LAYOUT_GENERAL},
    };
    VkWriteDescriptorSet writes[2]{};
    for (uint32_t i = 0; i < 2; ++i) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = set;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = i == 0 ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[i].pImageInfo = &images[i];
    }
    vkUpdateDescriptorSets(ctx.config.device, 2, writes, 0, nullptr);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, ctx.pipelines[pass]);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, ctx.layout, 0, 1, &set, 0, nullptr);
    vkCmdDispatch(command, (extent.width + 7) / 8, (extent.height + 7) / 8, 1);
}

bool record(void *state, const af_vulkan_upscaler_frame *frame)
{
    auto &ctx = *static_cast<Context *>(state);
    if (frame->struct_size < sizeof(*frame) || frame->frame_slot >= ctx.config.frames_in_flight ||
        !frame->color.view || !frame->output.view ||
        frame->jitter_pixels[0] != 0 || frame->jitter_pixels[1] != 0) return false;
    Slot &slot = ctx.slots[frame->frame_slot];
    VkCommandBuffer command = frame->command_buffer;
    vkCmdPushConstants(command, ctx.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Constants), &ctx.constants);
    transition(command, slot.encoded.image, false);
    transition(command, slot.scaled.image, false);
    dispatch(ctx, command, Encode, slot.sets[Encode], frame->color.view, slot.encoded.view, ctx.config.render_extent);
    transition(command, slot.encoded.image, true);
    dispatch(ctx, command, Easu, slot.sets[Easu], slot.encoded.view, slot.scaled.view, ctx.config.output_extent);
    transition(command, slot.scaled.image, true);
    dispatch(ctx, command, Rcas, slot.sets[Rcas], slot.scaled.view, frame->output.view, ctx.config.output_extent);
    return true;
}

void destroy(void *context) { delete static_cast<Context *>(context); }
void stop(void *) {}
}

AF_EXTENSION_EXPORT const af_extension_api *af_extension_get_api(uint32_t version)
{
    static const af_vulkan_upscaler_api upscaler = {
        AF_VULKAN_UPSCALER_ABI_VERSION, sizeof(af_vulkan_upscaler_api), 2.f / 3.f,
        create, record, destroy, AF_VULKAN_UPSCALER_SPATIAL,
    };
    static const af_extension_api api = {
        AF_EXTENSION_ABI_VERSION, sizeof(af_extension_api), "AMD FSR 1.0",
        start, nullptr, stop, &upscaler,
    };
    return version == AF_EXTENSION_ABI_VERSION ? &api : nullptr;
}
