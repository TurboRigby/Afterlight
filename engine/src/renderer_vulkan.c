/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "renderer.h"
#include "log.h"
#include "vulkan_device_policy.h"
#include "ui_vulkan.h"
#include "texture_vulkan.h"
#include "shadow_vulkan.h"
#include "shadow_math.h"
#include "post_vulkan.h"
#include "ibl.h"
#include "local_shadow.h"
#include <math.h>

#include <vulkan/vulkan.h>
#include <SDL3/SDL_vulkan.h>
#include <stdatomic.h>
#include <stddef.h>
#include "mesh_vert.h"
#include "mesh_frag.h"
#define af_fullscreen_vert af_sky_vertex
#include "fullscreen_vert.h"
#undef af_fullscreen_vert
#include "sky_frag.h"
#include <stdlib.h>
#include <string.h>

#define AF_FRAMES_IN_FLIGHT AF_UI_FRAMES_IN_FLIGHT
_Static_assert(AF_FRAMES_IN_FLIGHT==AF_SHADOW_FRAME_COUNT,"Independent shadow maps for each frame");
#define AF_PORTABILITY_SUBSET "VK_KHR_portability_subset"

typedef struct af_gpu_buffer {
    VkBuffer handle;
    VkDeviceMemory memory;
} af_gpu_buffer;

typedef struct af_gpu_light {af_vec4 position_range,color_intensity,direction_inner,params;} af_gpu_light;
typedef struct af_frame_uniform {
    af_mat4 view_projection;
    af_vec4 direction_intensity,color_ambient;
    af_mat4 shadow_matrices[4];
    af_vec4 cascade_splits,camera_position,camera_forward,shadow_params,cascade_params,render_params;
    af_gpu_light lights[AF_SCENE_MAX_LOCAL_LIGHTS];
    af_vec4 light_count;
    af_mat4 local_shadow_matrices[AF_LOCAL_SHADOW_LAYERS];
    af_mat4 inverse_view_projection;af_vec4 sky_params;
} af_frame_uniform;
_Static_assert(sizeof(af_frame_uniform)==3104,"Shader frame layout");
typedef struct af_shadow_draw {af_mat4 model;size_t mesh;uint32_t skin_base;} af_shadow_draw;
#define AF_SKIN_INSTANCES 128u
#define AF_SKIN_MATRICES (AF_SKIN_INSTANCES*AF_ANIMATION_MAX_JOINTS+1u)
typedef struct af_skin_instance {af_object_id object;af_mat4 matrices[AF_ANIMATION_MAX_JOINTS];size_t count;} af_skin_instance;
typedef struct af_environment_asset {char name[AF_MESH_NAME_CAPACITY];af_vk_texture *specular,*diffuse,*sky;} af_environment_asset;
typedef struct af_texture_asset {char name[AF_MESH_NAME_CAPACITY];af_vk_texture *texture;bool normal;} af_texture_asset;

typedef struct af_frame {
    VkCommandBuffer command;
    VkSemaphore acquired;
    VkFence fence;
    af_gpu_buffer uniform,skin_buffer;
    VkDescriptorSet descriptor;
    VkQueryPool timestamps;
    bool timestamp_submitted;
    uint64_t shadow_signatures[4],local_shadow_signatures[24];
} af_frame;

typedef struct af_depth_attachment {
    VkImage image;
    VkDeviceMemory memory;
    VkImageView view;
} af_depth_attachment;

typedef struct af_swapchain {
    VkSwapchainKHR handle;
    VkExtent2D extent;
    VkSurfaceFormatKHR surface_format;
    af_vk_post *post;
    VkRenderPass render_pass;
    VkPipelineLayout pipeline_layout;
    VkPipeline pipeline,sky_pipeline;
    uint32_t image_count;
    VkImage *images;
    VkImageView *views;
    VkFramebuffer *framebuffers;
    VkFormat depth_format;
    af_depth_attachment *depths;
    /* Presentation waits are tied to images, not frames in flight. */
    VkSemaphore *present_ready;
} af_swapchain;

typedef struct af_gpu_mesh {
    af_gpu_buffer vertices;
    af_gpu_buffer indices;
    uint32_t index_count,max_joint;bool skinned;
    af_vec3 minimum, maximum;
} af_gpu_mesh;

typedef struct af_mesh_asset {
    char name[AF_MESH_NAME_CAPACITY];
    af_gpu_mesh gpu;
} af_mesh_asset;

struct af_renderer {
    SDL_Window *window;
    const af_scene *previous_scene;float interpolation_alpha;
    af_vk_ui *ui;
    af_vk_shadows *shadows,*local_shadows;
    uint32_t local_shadow_layers,local_shadow_resolution,local_shadow_lights,local_shadow_overflow;
    uint32_t shadow_resolution,shadow_layers;
    af_shadow_draw *shadow_draws;
    af_shadow_bounds *shadow_bounds;
    size_t shadow_capacity,shadow_count,shadow_draw_calls;
    const af_scene *posed_scene;const af_mat4 *posed_worlds;size_t posed_count;
    af_skin_instance skins[AF_SKIN_INSTANCES];size_t skin_count;uint64_t skin_revision;
    uint64_t mesh_revision;uint32_t shadow_passes,shadow_cached_passes;
    af_gpu_buffer capture_buffer;
    af_vec2 capture_position;
    VkFormat capture_format;
    bool capture_supported,capture_pending,capture_ready;
    af_vk_texture *white,*flat_normal;
    af_vk_texture *environment,*irradiance,*brdf;uint32_t environment_profile;
    af_environment_asset environments[16];size_t environment_count;char active_environment[AF_MESH_NAME_CAPACITY];
    af_texture_asset *textures;
    size_t texture_count,texture_capacity,missing_textures;
    VkDescriptorSetLayout frame_layout;
    VkDescriptorPool frame_pool;
    VkInstance instance;
    VkDebugUtilsMessengerEXT debug_messenger;
    VkSurfaceKHR surface;
    VkPhysicalDevice physical_device;
    VkDevice device;
    float anisotropy;
    uint32_t timestamp_bits;
    float timestamp_period;
    bool timing_valid;
    double gpu_ms[5];double cpu_fence_ms,cpu_acquire_ms;
    uint32_t graphics_family;
    uint32_t present_family;
    VkQueue graphics_queue;
    VkQueue present_queue;
    VkCommandPool command_pool;
    af_frame frames[AF_FRAMES_IN_FLIGHT];
    uint32_t frame_index;
    af_swapchain swapchain;
    af_mesh_asset *meshes;
    size_t mesh_count;
    size_t mesh_capacity;
    size_t draw_calls;
    size_t missing_meshes;
    bool resize_pending;
    bool swapchain_invalid;
    bool validation_enabled;
    atomic_uint validation_errors;
    uint64_t rendered_frames;
    uint64_t swapchain_rebuilds;
};

static bool vk_ok(VkResult result, const char *operation)
{
    if (result == VK_SUCCESS) return true;
    af_log("error", "%s failed (VkResult %d).", operation, (int)result);
    return false;
}

static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT *data, void *user_data)
{
    (void)type;
    af_renderer *renderer = user_data;
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        atomic_fetch_add(&renderer->validation_errors, 1);
    af_log((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) ? "validation error" : "validation",
        "%s", data->pMessage);
    return VK_FALSE;
}

static bool instance_extension_available(const char *name)
{
    uint32_t count = 0;
    if (vkEnumerateInstanceExtensionProperties(NULL, &count, NULL) != VK_SUCCESS) return false;
    VkExtensionProperties *extensions = calloc(count, sizeof(*extensions));
    if (!extensions) return false;
    bool found = false;
    if (vkEnumerateInstanceExtensionProperties(NULL, &count, extensions) == VK_SUCCESS) {
        for (uint32_t i = 0; i < count; ++i)
            if (strcmp(extensions[i].extensionName, name) == 0) found = true;
    }
    free(extensions);
    return found;
}

static bool validation_layer_available(void)
{
    uint32_t count = 0;
    if (vkEnumerateInstanceLayerProperties(&count, NULL) != VK_SUCCESS) return false;
    VkLayerProperties *layers = calloc(count, sizeof(*layers));
    if (!layers) return false;
    bool found = false;
    if (vkEnumerateInstanceLayerProperties(&count, layers) == VK_SUCCESS) {
        for (uint32_t i = 0; i < count; ++i)
            if (strcmp(layers[i].layerName, "VK_LAYER_KHRONOS_validation") == 0) found = true;
    }
    free(layers);
    return found;
}

static bool create_instance(af_renderer *renderer, bool enable_validation)
{
    Uint32 sdl_count = 0;
    const char *const *sdl_extensions = SDL_Vulkan_GetInstanceExtensions(&sdl_count);
    if (!sdl_extensions) {
        af_log("error", "SDL Vulkan extensions: %s", SDL_GetError());
        return false;
    }
    const char **extensions = calloc((size_t)sdl_count + 2, sizeof(*extensions));
    if (!extensions) return false;
    uint32_t extension_count = sdl_count;
    for (Uint32 i = 0; i < sdl_count; ++i) extensions[i] = sdl_extensions[i];
    VkInstanceCreateFlags flags = 0;
    if (instance_extension_available(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
        bool already_present = false;
        for (uint32_t i = 0; i < extension_count; ++i)
            if (strcmp(extensions[i], VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME) == 0)
                already_present = true;
        if (!already_present) extensions[extension_count++] = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
        flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }
    renderer->validation_enabled = enable_validation && validation_layer_available() &&
        instance_extension_available(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    if (enable_validation && !renderer->validation_enabled)
        af_log("warning", "Validation requested but the layer or debug-utils extension is unavailable.");
    if (renderer->validation_enabled) extensions[extension_count++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
    const char *validation_layer = "VK_LAYER_KHRONOS_validation";
    VkDebugUtilsMessengerCreateInfoEXT debug_info = {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = debug_callback,
        .pUserData = renderer
    };
    VkApplicationInfo application = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "Afterlight",
        .applicationVersion = VK_MAKE_VERSION(0, 1, 0),
        .pEngineName = "Afterlight",
        .engineVersion = VK_MAKE_VERSION(0, 1, 0),
        .apiVersion = VK_API_VERSION_1_1
    };
    VkInstanceCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = renderer->validation_enabled ? &debug_info : NULL,
        .flags = flags,
        .pApplicationInfo = &application,
        .enabledExtensionCount = extension_count,
        .ppEnabledExtensionNames = extensions,
        .enabledLayerCount = renderer->validation_enabled ? 1u : 0u,
        .ppEnabledLayerNames = renderer->validation_enabled ? &validation_layer : NULL
    };
    VkResult result = vkCreateInstance(&info, NULL, &renderer->instance);
    free(extensions);
    if (!vk_ok(result, "vkCreateInstance")) return false;
    if (renderer->validation_enabled) {
        PFN_vkCreateDebugUtilsMessengerEXT create_debug =
            (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(renderer->instance,
                "vkCreateDebugUtilsMessengerEXT");
        if (!create_debug || !vk_ok(create_debug(renderer->instance, &debug_info, NULL,
            &renderer->debug_messenger), "vkCreateDebugUtilsMessengerEXT")) return false;
        af_log("info", "Vulkan validation enabled.");
    }
    return true;
}

static bool device_extension_available(VkPhysicalDevice device, const char *name)
{
    uint32_t count = 0;
    if (vkEnumerateDeviceExtensionProperties(device, NULL, &count, NULL) != VK_SUCCESS) return false;
    VkExtensionProperties *extensions = calloc(count, sizeof(*extensions));
    if (!extensions) return false;
    bool found = false;
    if (vkEnumerateDeviceExtensionProperties(device, NULL, &count, extensions) == VK_SUCCESS) {
        for (uint32_t i = 0; i < count; ++i)
            if (strcmp(extensions[i].extensionName, name) == 0) found = true;
    }
    free(extensions);
    return found;
}

static bool find_queue_families(af_renderer *renderer, VkPhysicalDevice device,
    uint32_t *graphics, uint32_t *present)
{
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, NULL);
    VkQueueFamilyProperties *families = calloc(count, sizeof(*families));
    if (!families) return false;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families);
    *graphics = UINT32_MAX;
    *present = UINT32_MAX;
    for (uint32_t i = 0; i < count; ++i) {
        VkBool32 supported = VK_FALSE;
        if (families[i].queueCount == 0) continue;
        if (vkGetPhysicalDeviceSurfaceSupportKHR(device, i, renderer->surface, &supported) != VK_SUCCESS)
            continue;
        bool draws = (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
        if (draws && *graphics == UINT32_MAX) *graphics = i;
        if (supported && *present == UINT32_MAX) *present = i;
        if (draws && supported) {
            *graphics = *present = i;
            break;
        }
    }
    free(families);
    return *graphics != UINT32_MAX && *present != UINT32_MAX;
}

static bool create_device(af_renderer *renderer)
{
    uint32_t count = 0;
    if (!vk_ok(vkEnumeratePhysicalDevices(renderer->instance, &count, NULL), "vkEnumeratePhysicalDevices"))
        return false;
    if (count == 0) {
        af_log("error", "No Vulkan GPU found. Install a Vulkan driver (MoltenVK on macOS).");
        return false;
    }
    VkPhysicalDevice *devices = calloc(count, sizeof(*devices));
    if (!devices) return false;
    if (!vk_ok(vkEnumeratePhysicalDevices(renderer->instance, &count, devices), "vkEnumeratePhysicalDevices")) {
        free(devices);
        return false;
    }
    int best_score = -1;
    VkPhysicalDeviceProperties selected_properties = {0};
    VkPhysicalDeviceDriverProperties selected_driver = {0};
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t graphics = 0, present = 0, formats = 0, modes = 0;
        VkPhysicalDeviceProperties properties;
        VkSurfaceCapabilitiesKHR capabilities;
        vkGetPhysicalDeviceProperties(devices[i], &properties);
        if (properties.apiVersion < VK_API_VERSION_1_1 || properties.limits.maxBoundDescriptorSets<5 ||
            !device_extension_available(devices[i], VK_KHR_SWAPCHAIN_EXTENSION_NAME) ||
            !find_queue_families(renderer, devices[i], &graphics, &present)) continue;
        if (vkGetPhysicalDeviceSurfaceFormatsKHR(devices[i], renderer->surface, &formats, NULL) != VK_SUCCESS ||
            vkGetPhysicalDeviceSurfacePresentModesKHR(devices[i], renderer->surface, &modes, NULL) != VK_SUCCESS ||
            vkGetPhysicalDeviceSurfaceCapabilitiesKHR(devices[i], renderer->surface, &capabilities) != VK_SUCCESS ||
            formats == 0 || modes == 0 ||
            !(capabilities.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)) continue;
        VkPhysicalDeviceDriverProperties driver = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
        if (properties.apiVersion >= VK_API_VERSION_1_2 ||
            device_extension_available(devices[i], VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME)) {
            VkPhysicalDeviceProperties2 query = {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &driver
            };
            vkGetPhysicalDeviceProperties2(devices[i], &query);
        }
#ifdef __APPLE__
        const bool prefer_moltenvk = true;
#else
        const bool prefer_moltenvk = false;
#endif
        int score = af_vulkan_device_score(properties.deviceType, driver.driverID, prefer_moltenvk);
        if (score > best_score) {
            best_score = score;
            renderer->physical_device = devices[i];
            renderer->graphics_family = graphics;
            renderer->present_family = present;
            selected_properties = properties;
            selected_driver = driver;
        }
    }
    free(devices);
    if (best_score < 0) {
        af_log("error", "No Vulkan 1.1 device supports graphics and presentation for this window.");
        return false;
    }
    uint32_t queue_families=0;vkGetPhysicalDeviceQueueFamilyProperties(renderer->physical_device,&queue_families,NULL);
    VkQueueFamilyProperties *queue_properties=calloc(queue_families,sizeof(*queue_properties));
    if(!queue_properties)return false;vkGetPhysicalDeviceQueueFamilyProperties(renderer->physical_device,&queue_families,queue_properties);
    renderer->timestamp_bits=queue_properties[renderer->graphics_family].timestampValidBits;free(queue_properties);
    renderer->timestamp_period=selected_properties.limits.timestampPeriod;
    float priority = 1.0f;
    uint32_t families[] = {renderer->graphics_family, renderer->present_family};
    VkDeviceQueueCreateInfo queues[2] = {0};
    uint32_t queue_count = families[0] == families[1] ? 1u : 2u;
    for (uint32_t i = 0; i < queue_count; ++i) {
        queues[i] = (VkDeviceQueueCreateInfo){
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = families[i], .queueCount = 1, .pQueuePriorities = &priority
        };
    }
    const char *extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME, AF_PORTABILITY_SUBSET};
    uint32_t extension_count = device_extension_available(renderer->physical_device, AF_PORTABILITY_SUBSET) ? 2u : 1u;
    VkPhysicalDeviceFeatures supported;vkGetPhysicalDeviceFeatures(renderer->physical_device,&supported);
    VkPhysicalDeviceFeatures enabled={.samplerAnisotropy=supported.samplerAnisotropy};
    renderer->anisotropy=supported.samplerAnisotropy?fminf(8,selected_properties.limits.maxSamplerAnisotropy):1;
    VkDeviceCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = queue_count, .pQueueCreateInfos = queues,
        .pEnabledFeatures=&enabled,
        .enabledExtensionCount = extension_count, .ppEnabledExtensionNames = extensions
    };
    if (!vk_ok(vkCreateDevice(renderer->physical_device, &info, NULL, &renderer->device), "vkCreateDevice"))
        return false;
    vkGetDeviceQueue(renderer->device, renderer->graphics_family, 0, &renderer->graphics_queue);
    vkGetDeviceQueue(renderer->device, renderer->present_family, 0, &renderer->present_queue);
    af_log("info", "GPU: %s (Vulkan %u.%u.%u)", selected_properties.deviceName,
        VK_VERSION_MAJOR(selected_properties.apiVersion), VK_VERSION_MINOR(selected_properties.apiVersion),
        VK_VERSION_PATCH(selected_properties.apiVersion));
    af_log("info", "Driver: %s%s%s", selected_driver.driverName[0] ? selected_driver.driverName : "unknown",
        selected_driver.driverInfo[0] ? " | " : "", selected_driver.driverInfo);
    return true;
}

static bool create_pipeline(af_renderer *renderer)
{
    af_swapchain *swapchain = &renderer->swapchain;
    VkShaderModule vertex_shader = VK_NULL_HANDLE, fragment_shader = VK_NULL_HANDLE;
    VkShaderModuleCreateInfo shader_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = sizeof(af_mesh_vert), .pCode = af_mesh_vert
    };
    if (!vk_ok(vkCreateShaderModule(renderer->device, &shader_info, NULL, &vertex_shader),
        "vkCreateShaderModule (vertex)")) return false;
    bool success = false;
    shader_info.codeSize = sizeof(af_mesh_frag);
    shader_info.pCode = af_mesh_frag;
    if (!vk_ok(vkCreateShaderModule(renderer->device, &shader_info, NULL, &fragment_shader),
        "vkCreateShaderModule (fragment)")) goto cleanup;
    VkPipelineShaderStageCreateInfo stages[] = {
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vertex_shader, .pName = "main"},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fragment_shader, .pName = "main"}
    };
    VkVertexInputBindingDescription binding = {
        .binding = 0, .stride = sizeof(af_vertex), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
    };
    VkVertexInputAttributeDescription attributes[] = {
        {.location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = offsetof(af_vertex, position)},
        {.location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32A32_SFLOAT,
            .offset = offsetof(af_vertex, color)},
        {.location=2,.binding=0,.format=VK_FORMAT_R32G32B32_SFLOAT,.offset=offsetof(af_vertex,normal)},
        {.location=3,.binding=0,.format=VK_FORMAT_R32G32_SFLOAT,.offset=offsetof(af_vertex,uv)},
        {.location=4,.binding=0,.format=VK_FORMAT_R16G16B16A16_UINT,.offset=offsetof(af_vertex,joints)},
        {.location=5,.binding=0,.format=VK_FORMAT_R32G32B32A32_SFLOAT,.offset=offsetof(af_vertex,weights)}
    };
    VkPipelineVertexInputStateCreateInfo vertex_input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1, .pVertexBindingDescriptions = &binding,
        .vertexAttributeDescriptionCount = 6, .pVertexAttributeDescriptions = attributes
    };
    VkPipelineInputAssemblyStateCreateInfo assembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
    };
    VkPipelineViewportStateCreateInfo viewport = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1, .scissorCount = 1
    };
    VkPipelineRasterizationStateCreateInfo raster = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE, .lineWidth = 1.0f
    };
    VkPipelineMultisampleStateCreateInfo multisample = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT
    };
    VkPipelineDepthStencilStateCreateInfo depth = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE, .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_LESS
    };
    VkPipelineColorBlendAttachmentState attachment = {
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
    };
    VkPipelineColorBlendStateCreateInfo blending = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1, .pAttachments = &attachment
    };
    VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2, .pDynamicStates = dynamic_states
    };
    VkPushConstantRange push_range = {.stageFlags = VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT, .size = 128};
    VkPhysicalDeviceProperties material_limits;vkGetPhysicalDeviceProperties(renderer->physical_device,&material_limits);
    if(material_limits.limits.maxBoundDescriptorSets<5){af_log("error","Scene materials require five bound descriptor sets.");return false;}
    VkDescriptorSetLayout layouts[]={renderer->frame_layout,af_vk_texture_layout(renderer->white),af_vk_shadows_layout(renderer->shadows),af_vk_texture_layout(renderer->flat_normal),af_vk_texture_layout(renderer->white)};
    VkPipelineLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount=5,.pSetLayouts=layouts,
        .pushConstantRangeCount = 1, .pPushConstantRanges = &push_range
    };
    if (!vk_ok(vkCreatePipelineLayout(renderer->device, &layout_info, NULL, &swapchain->pipeline_layout),
        "vkCreatePipelineLayout")) goto cleanup;
    VkGraphicsPipelineCreateInfo pipeline_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2, .pStages = stages, .pVertexInputState = &vertex_input,
        .pInputAssemblyState = &assembly, .pViewportState = &viewport,
        .pRasterizationState = &raster, .pMultisampleState = &multisample,
        .pColorBlendState = &blending, .pDynamicState = &dynamic, .pDepthStencilState = &depth,
        .layout = swapchain->pipeline_layout, .renderPass = swapchain->render_pass,
        .subpass = 0, .basePipelineIndex = -1
    };
    success = vk_ok(vkCreateGraphicsPipelines(renderer->device, VK_NULL_HANDLE, 1, &pipeline_info,
        NULL, &swapchain->pipeline), "vkCreateGraphicsPipelines");
cleanup:
    if (fragment_shader) vkDestroyShaderModule(renderer->device, fragment_shader, NULL);
    vkDestroyShaderModule(renderer->device, vertex_shader, NULL);
    return success;
}

static bool create_sky_pipeline(af_renderer *r){
    VkShaderModule vertex=VK_NULL_HANDLE,fragment=VK_NULL_HANDLE;bool success=false;
    VkShaderModuleCreateInfo code={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=sizeof(af_sky_vertex),.pCode=af_sky_vertex};
    if(!vk_ok(vkCreateShaderModule(r->device,&code,NULL,&vertex),"sky vertex"))goto done;
    code.codeSize=sizeof(af_sky_frag);code.pCode=af_sky_frag;if(!vk_ok(vkCreateShaderModule(r->device,&code,NULL,&fragment),"sky fragment"))goto done;
    VkPipelineShaderStageCreateInfo stages[]={{.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_VERTEX_BIT,.module=vertex,.pName="main"},{.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_FRAGMENT_BIT,.module=fragment,.pName="main"}};
    VkPipelineVertexInputStateCreateInfo input={.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo assembly={.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    VkPipelineViewportStateCreateInfo viewport={.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,.viewportCount=1,.scissorCount=1};
    VkPipelineRasterizationStateCreateInfo raster={.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,.polygonMode=VK_POLYGON_MODE_FILL,.cullMode=VK_CULL_MODE_NONE,.lineWidth=1};
    VkPipelineMultisampleStateCreateInfo samples={.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT};
    VkPipelineColorBlendAttachmentState attachment={.colorWriteMask=15};VkPipelineColorBlendStateCreateInfo blend={.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,.attachmentCount=1,.pAttachments=&attachment};
    VkPipelineDepthStencilStateCreateInfo depth={.sType=VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    VkDynamicState states[]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};VkPipelineDynamicStateCreateInfo dynamic={.sType=VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,.dynamicStateCount=2,.pDynamicStates=states};
    VkGraphicsPipelineCreateInfo info={.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,.stageCount=2,.pStages=stages,.pVertexInputState=&input,.pInputAssemblyState=&assembly,.pViewportState=&viewport,.pRasterizationState=&raster,.pMultisampleState=&samples,.pColorBlendState=&blend,.pDepthStencilState=&depth,.pDynamicState=&dynamic,.layout=r->swapchain.pipeline_layout,.renderPass=r->swapchain.render_pass,.basePipelineIndex=-1};
    success=vk_ok(vkCreateGraphicsPipelines(r->device,VK_NULL_HANDLE,1,&info,NULL,&r->swapchain.sky_pipeline),"sky pipeline");
 done:if(vertex)vkDestroyShaderModule(r->device,vertex,NULL);if(fragment)vkDestroyShaderModule(r->device,fragment,NULL);return success;
}
static void destroy_buffer(af_renderer *renderer, af_gpu_buffer *buffer)
{
    if (buffer->handle) vkDestroyBuffer(renderer->device, buffer->handle, NULL);
    if (buffer->memory) vkFreeMemory(renderer->device, buffer->memory, NULL);
    *buffer = (af_gpu_buffer){0};
}

static void destroy_mesh(af_renderer *renderer, af_gpu_mesh *mesh)
{
    destroy_buffer(renderer, &mesh->vertices);
    destroy_buffer(renderer, &mesh->indices);
    mesh->index_count = 0;
}

static bool upload_buffer(af_renderer *renderer, const void *data, VkDeviceSize size,
    VkBufferUsageFlags usage, af_gpu_buffer *buffer)
{
    VkBufferCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = size,
        .usage = usage, .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };
    if (!vk_ok(vkCreateBuffer(renderer->device, &info, NULL, &buffer->handle), "vkCreateBuffer")) return false;
    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(renderer->device, buffer->handle, &requirements);
    VkPhysicalDeviceMemoryProperties properties;
    vkGetPhysicalDeviceMemoryProperties(renderer->physical_device, &properties);
    uint32_t memory_type = UINT32_MAX;
    for (uint32_t i = 0; i < properties.memoryTypeCount; ++i) {
        VkMemoryPropertyFlags flags = properties.memoryTypes[i].propertyFlags;
        if (!(requirements.memoryTypeBits & (1u << i)) || !(flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) continue;
        memory_type = i;
        if (flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) break;
    }
    if (memory_type == UINT32_MAX) {
        af_log("error", "No host-visible memory type for mesh data.");
        return false;
    }
    VkMemoryAllocateInfo allocation = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size, .memoryTypeIndex = memory_type
    };
    if (!vk_ok(vkAllocateMemory(renderer->device, &allocation, NULL, &buffer->memory), "vkAllocateMemory") ||
        !vk_ok(vkBindBufferMemory(renderer->device, buffer->handle, buffer->memory, 0), "vkBindBufferMemory"))
        return false;
    void *mapped = NULL;
    if (!vk_ok(vkMapMemory(renderer->device, buffer->memory, 0, VK_WHOLE_SIZE, 0, &mapped), "vkMapMemory")) return false;
    memcpy(mapped, data, (size_t)size);
    bool flushed = true;
    if (!(properties.memoryTypes[memory_type].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
        VkMappedMemoryRange range = {
            .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, .memory = buffer->memory,
            .offset = 0, .size = VK_WHOLE_SIZE
        };
        flushed = vk_ok(vkFlushMappedMemoryRanges(renderer->device, 1, &range), "vkFlushMappedMemoryRanges");
    }
    vkUnmapMemory(renderer->device, buffer->memory);
    return flushed;
}

static size_t find_mesh_index(const af_renderer *renderer, const char *name)
{
    for (size_t i = 0; i < renderer->mesh_count; ++i)
        if (strcmp(renderer->meshes[i].name, name) == 0) return i;
    return renderer->mesh_count;
}

bool af_renderer_upload_mesh(af_renderer *renderer, const char *name, const af_mesh_data *mesh)
{
    if (!name || !name[0] || strlen(name) >= AF_MESH_NAME_CAPACITY || !mesh ||
        !mesh->vertices || !mesh->indices || mesh->vertex_count == 0 ||
        mesh->index_count == 0 || mesh->index_count % 3 != 0 ||
        (uint64_t)mesh->vertex_count * sizeof(af_vertex) > SIZE_MAX ||
        (uint64_t)mesh->index_count * sizeof(uint32_t) > SIZE_MAX) {
        af_log("error", "Mesh requires a valid asset name, vertices, and indexed whole triangles.");
        return false;
    }
    for (uint32_t i = 0; i < mesh->index_count; ++i) {
        if (mesh->indices[i] >= mesh->vertex_count) {
            af_log("error", "Mesh index %u is out of range (vertex count %u).", mesh->indices[i], mesh->vertex_count);
            return false;
        }
    }
    char asset_name[AF_MESH_NAME_CAPACITY];
    memcpy(asset_name, name, strlen(name) + 1);
    af_gpu_mesh uploaded = {0};
    if (!upload_buffer(renderer, mesh->vertices, (VkDeviceSize)mesh->vertex_count * sizeof(af_vertex),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &uploaded.vertices) ||
        !upload_buffer(renderer, mesh->indices, (VkDeviceSize)mesh->index_count * sizeof(uint32_t),
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT, &uploaded.indices)) goto fail;
    uploaded.index_count = mesh->index_count;
    uploaded.minimum = uploaded.maximum = mesh->vertices[0].position;
    for (uint32_t i = 0; i < mesh->vertex_count; ++i) {
        const af_vertex *v=&mesh->vertices[i];float weights[]={v->weights.x,v->weights.y,v->weights.z,v->weights.w};float sum=0;
        for(unsigned j=0;j<4;++j){if(!isfinite(weights[j])||weights[j]<0||v->joints[j]>=AF_ANIMATION_MAX_JOINTS)goto fail;sum+=weights[j];if(v->joints[j]>uploaded.max_joint)uploaded.max_joint=v->joints[j];}
        if(sum>0){if(fabsf(sum-1)>.001f)goto fail;uploaded.skinned=true;}
        af_vec3 p = mesh->vertices[i].position;
        if (!isfinite(p.x) || !isfinite(p.y) || !isfinite(p.z)) goto fail;
        uploaded.minimum.x = fminf(uploaded.minimum.x,p.x); uploaded.maximum.x = fmaxf(uploaded.maximum.x,p.x);
        uploaded.minimum.y = fminf(uploaded.minimum.y,p.y); uploaded.maximum.y = fmaxf(uploaded.maximum.y,p.y);
        uploaded.minimum.z = fminf(uploaded.minimum.z,p.z); uploaded.maximum.z = fmaxf(uploaded.maximum.z,p.z);
    }
    size_t index = find_mesh_index(renderer, asset_name);
    if (index == renderer->mesh_count && renderer->mesh_count == renderer->mesh_capacity) {
        if (renderer->mesh_capacity > SIZE_MAX / 2 / sizeof(*renderer->meshes)) goto fail;
        size_t capacity = renderer->mesh_capacity ? renderer->mesh_capacity * 2 : 8;
        af_mesh_asset *assets = realloc(renderer->meshes, capacity * sizeof(*assets));
        if (!assets) goto fail;
        renderer->meshes = assets;
        renderer->mesh_capacity = capacity;
    }
    if (!af_renderer_wait_idle(renderer)) goto fail;
    if (index < renderer->mesh_count) destroy_mesh(renderer, &renderer->meshes[index].gpu);
    else ++renderer->mesh_count;
    renderer->meshes[index] = (af_mesh_asset){.gpu = uploaded};
    memcpy(renderer->meshes[index].name, asset_name, strlen(asset_name) + 1);++renderer->mesh_revision;
    return true;
fail:
    destroy_mesh(renderer, &uploaded);
    return false;
}

bool af_renderer_remove_mesh(af_renderer *renderer, const char *name)
{
    if (!name) return false;
    size_t index = find_mesh_index(renderer, name);
    if (index == renderer->mesh_count || !af_renderer_wait_idle(renderer)) return false;
    destroy_mesh(renderer, &renderer->meshes[index].gpu);
    memmove(&renderer->meshes[index], &renderer->meshes[index+1],
        (renderer->mesh_count-index-1) * sizeof(*renderer->meshes));
    --renderer->mesh_count;++renderer->mesh_revision;
    return true;
}

static VkFormat select_depth_format(af_renderer *renderer)
{
    const VkFormat formats[] = {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT_S8_UINT};
    for (size_t i = 0; i < sizeof(formats)/sizeof(formats[0]); ++i) {
        VkFormatProperties properties;
        vkGetPhysicalDeviceFormatProperties(renderer->physical_device, formats[i], &properties);
        if (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) return formats[i];
    }
    return VK_FORMAT_UNDEFINED;
}

static bool create_depth_attachment(af_renderer *renderer, af_depth_attachment *depth)
{
    af_swapchain *swapchain = &renderer->swapchain;
    VkImageCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D,
        .format = swapchain->depth_format,
        .extent = {swapchain->extent.width, swapchain->extent.height, 1},
        .mipLevels = 1, .arrayLayers = 1, .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL, .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
    };
    if (!vk_ok(vkCreateImage(renderer->device, &info, NULL, &depth->image), "vkCreateImage (depth)")) return false;
    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(renderer->device, depth->image, &requirements);
    VkPhysicalDeviceMemoryProperties properties;
    vkGetPhysicalDeviceMemoryProperties(renderer->physical_device, &properties);
    uint32_t memory_type = UINT32_MAX;
    for (uint32_t i = 0; i < properties.memoryTypeCount; ++i) {
        if ((requirements.memoryTypeBits & (1u << i)) &&
            (properties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            memory_type = i;
            break;
        }
    }
    if (memory_type == UINT32_MAX) {
        af_log("error", "No device-local memory for depth attachment.");
        return false;
    }
    VkMemoryAllocateInfo allocation = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size, .memoryTypeIndex = memory_type
    };
    if (!vk_ok(vkAllocateMemory(renderer->device, &allocation, NULL, &depth->memory), "vkAllocateMemory (depth)") ||
        !vk_ok(vkBindImageMemory(renderer->device, depth->image, depth->memory, 0), "vkBindImageMemory")) return false;
    VkImageViewCreateInfo view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = depth->image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = swapchain->depth_format,
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT, .levelCount = 1, .layerCount = 1}
    };
    return vk_ok(vkCreateImageView(renderer->device, &view_info, NULL, &depth->view), "vkCreateImageView (depth)");
}

static void destroy_swapchain(af_renderer *renderer)
{
    af_swapchain *swapchain = &renderer->swapchain;
    af_vk_ui_release_pipeline(renderer->ui);
    af_vk_post_destroy(swapchain->post);
    for (uint32_t i = 0; i < swapchain->image_count; ++i) {
        if (swapchain->framebuffers && swapchain->framebuffers[i])
            vkDestroyFramebuffer(renderer->device, swapchain->framebuffers[i], NULL);
        if (swapchain->depths) {
            af_depth_attachment *depth = &swapchain->depths[i];
            if (depth->view) vkDestroyImageView(renderer->device, depth->view, NULL);
            if (depth->image) vkDestroyImage(renderer->device, depth->image, NULL);
            if (depth->memory) vkFreeMemory(renderer->device, depth->memory, NULL);
        }
        if (swapchain->views && swapchain->views[i])
            vkDestroyImageView(renderer->device, swapchain->views[i], NULL);
        if (swapchain->present_ready && swapchain->present_ready[i])
            vkDestroySemaphore(renderer->device, swapchain->present_ready[i], NULL);
    }
    if(swapchain->sky_pipeline)vkDestroyPipeline(renderer->device,swapchain->sky_pipeline,NULL);
    if (swapchain->pipeline) vkDestroyPipeline(renderer->device, swapchain->pipeline, NULL);
    if (swapchain->pipeline_layout) vkDestroyPipelineLayout(renderer->device, swapchain->pipeline_layout, NULL);
    if (swapchain->render_pass) vkDestroyRenderPass(renderer->device, swapchain->render_pass, NULL);
    if (swapchain->handle) vkDestroySwapchainKHR(renderer->device, swapchain->handle, NULL);
    free(swapchain->images);
    free(swapchain->views);
    free(swapchain->framebuffers);
    free(swapchain->depths);
    free(swapchain->present_ready);
    *swapchain = (af_swapchain){0};
}

static uint32_t clamp_extent(uint32_t value, uint32_t minimum, uint32_t maximum)
{
    return value < minimum ? minimum : (value > maximum ? maximum : value);
}

static bool create_swapchain(af_renderer *renderer)
{
    int width = 0, height = 0;
    if (!SDL_GetWindowSizeInPixels(renderer->window, &width, &height)) {
        af_log("error", "Framebuffer size: %s", SDL_GetError());
        return false;
    }
    if (width <= 0 || height <= 0) return true; /* Try again after restore. */
    VkSurfaceCapabilitiesKHR capabilities;
    if (!vk_ok(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(renderer->physical_device, renderer->surface,
        &capabilities), "vkGetPhysicalDeviceSurfaceCapabilitiesKHR")) return false;
    VkExtent2D extent = capabilities.currentExtent;
    if (extent.width == UINT32_MAX) {
        extent.width = clamp_extent((uint32_t)width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        extent.height = clamp_extent((uint32_t)height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
    }
    if (extent.width == 0 || extent.height == 0) return true;
    uint32_t format_count = 0;
    if (!vk_ok(vkGetPhysicalDeviceSurfaceFormatsKHR(renderer->physical_device, renderer->surface,
        &format_count, NULL), "vkGetPhysicalDeviceSurfaceFormatsKHR") || format_count == 0) return false;
    VkSurfaceFormatKHR *formats = calloc(format_count, sizeof(*formats));
    if (!formats) return false;
    if (!vk_ok(vkGetPhysicalDeviceSurfaceFormatsKHR(renderer->physical_device, renderer->surface,
        &format_count, formats), "vkGetPhysicalDeviceSurfaceFormatsKHR")) {
        free(formats);
        return false;
    }
    VkSurfaceFormatKHR format = formats[0];
    if (format_count == 1 && format.format == VK_FORMAT_UNDEFINED)
        format.format = VK_FORMAT_B8G8R8A8_SRGB;
    for (uint32_t i = 0; i < format_count; ++i) {
        if (formats[i].format == VK_FORMAT_B8G8R8A8_SRGB &&
            formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            format = formats[i];
            break;
        }
    }
    free(formats);
    /* SDL sends size/expose events when the window first appears, and may
       send duplicate resize notifications. Keep the existing resources when
       the surface configuration is unchanged. Vulkan out-of-date/suboptimal
       results bypass this optimization even if the size did not change. */
    af_swapchain *existing = &renderer->swapchain;
    if (existing->handle && !renderer->swapchain_invalid &&
        existing->extent.width == extent.width && existing->extent.height == extent.height &&
        existing->surface_format.format == format.format && existing->surface_format.colorSpace == format.colorSpace) {
        renderer->resize_pending = false;
        return true;
    }
    uint32_t image_count = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && image_count > capabilities.maxImageCount)
        image_count = capabilities.maxImageCount;
    VkCompositeAlphaFlagBitsKHR composite = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    const VkCompositeAlphaFlagBitsKHR alpha_modes[] = {
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR
    };
    for (size_t i = 0; i < sizeof(alpha_modes) / sizeof(alpha_modes[0]); ++i) {
        if (capabilities.supportedCompositeAlpha & alpha_modes[i]) {
            composite = alpha_modes[i];
            break;
        }
    }
    if (!vk_ok(vkDeviceWaitIdle(renderer->device), "vkDeviceWaitIdle")) return false;
    destroy_swapchain(renderer);
    af_swapchain *swapchain = &renderer->swapchain;
    swapchain->extent = extent;
    swapchain->surface_format = format;
    renderer->capture_supported=(capabilities.supportedUsageFlags&VK_IMAGE_USAGE_TRANSFER_SRC_BIT)&&
        (format.format==VK_FORMAT_B8G8R8A8_SRGB||format.format==VK_FORMAT_B8G8R8A8_UNORM||format.format==VK_FORMAT_R8G8B8A8_SRGB||format.format==VK_FORMAT_R8G8B8A8_UNORM);
    uint32_t families[] = {renderer->graphics_family, renderer->present_family};
    bool separate_queues = families[0] != families[1];
    VkSwapchainCreateInfoKHR info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = renderer->surface, .minImageCount = image_count,
        .imageFormat = format.format, .imageColorSpace = format.colorSpace,
        .imageExtent = extent, .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|(renderer->capture_supported?VK_IMAGE_USAGE_TRANSFER_SRC_BIT:0),
        .imageSharingMode = separate_queues ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = separate_queues ? 2u : 0u,
        .pQueueFamilyIndices = separate_queues ? families : NULL,
        .preTransform = capabilities.currentTransform, .compositeAlpha = composite,
        .presentMode = VK_PRESENT_MODE_FIFO_KHR, .clipped = VK_TRUE
    };
    if (!vk_ok(vkCreateSwapchainKHR(renderer->device, &info, NULL, &swapchain->handle), "vkCreateSwapchainKHR"))
        return false;
    if (!vk_ok(vkGetSwapchainImagesKHR(renderer->device, swapchain->handle,
        &swapchain->image_count, NULL), "vkGetSwapchainImagesKHR") || swapchain->image_count == 0) return false;
    swapchain->images = calloc(swapchain->image_count, sizeof(*swapchain->images));
    swapchain->views = calloc(swapchain->image_count, sizeof(*swapchain->views));
    swapchain->framebuffers = calloc(swapchain->image_count, sizeof(*swapchain->framebuffers));
    swapchain->depths = calloc(swapchain->image_count, sizeof(*swapchain->depths));
    swapchain->present_ready = calloc(swapchain->image_count, sizeof(*swapchain->present_ready));
    if (!swapchain->images || !swapchain->views || !swapchain->framebuffers || !swapchain->present_ready || !swapchain->depths)
        return false;
    uint32_t retrieved_count = swapchain->image_count;
    if (!vk_ok(vkGetSwapchainImagesKHR(renderer->device, swapchain->handle,
        &retrieved_count, swapchain->images), "vkGetSwapchainImagesKHR")) return false;
    swapchain->image_count = retrieved_count;
    swapchain->depth_format = select_depth_format(renderer);
    if (swapchain->depth_format == VK_FORMAT_UNDEFINED) {
        af_log("error", "No supported depth attachment format.");
        return false;
    }
    VkAttachmentDescription attachments[] = {
        {.format = VK_FORMAT_R16G16B16A16_SFLOAT, .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED, .finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {.format = swapchain->depth_format, .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED, .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL}
    };
    VkAttachmentReference color_reference = {.attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depth_reference = {.attachment = 1, .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1, .pColorAttachments = &color_reference,
        .pDepthStencilAttachment = &depth_reference
    };
    VkPipelineStageFlags attachment_stages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    VkSubpassDependency dependencies[] = {
        {.srcSubpass=VK_SUBPASS_EXTERNAL,.dstSubpass=0,
         .srcStageMask=VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|attachment_stages,.dstStageMask=attachment_stages,
         .srcAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
         .dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT},
        {.srcSubpass=0,.dstSubpass=VK_SUBPASS_EXTERNAL,.srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,.dstStageMask=VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
         .srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,.dstAccessMask=VK_ACCESS_SHADER_READ_BIT}
    };
    VkRenderPassCreateInfo pass_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 2, .pAttachments = attachments,
        .subpassCount = 1, .pSubpasses = &subpass,
        .dependencyCount = 2, .pDependencies = dependencies
    };
    if (!vk_ok(vkCreateRenderPass(renderer->device, &pass_info, NULL, &swapchain->render_pass), "vkCreateRenderPass"))
        return false;
    for (uint32_t i = 0; i < swapchain->image_count; ++i) {
        VkImageViewCreateInfo view_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = swapchain->images[i], .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = format.format,
            .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1}
        };
        if (!vk_ok(vkCreateImageView(renderer->device, &view_info, NULL, &swapchain->views[i]), "vkCreateImageView"))
            return false;
        if (!create_depth_attachment(renderer, &swapchain->depths[i])) return false;
        VkSemaphoreCreateInfo semaphore_info = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        if (!vk_ok(vkCreateSemaphore(renderer->device, &semaphore_info, NULL,
            &swapchain->present_ready[i]), "vkCreateSemaphore")) return false;
    }
    VkImageView *depth_views=calloc(swapchain->image_count,sizeof(*depth_views));if(!depth_views)return false;
    for(uint32_t i=0;i<swapchain->image_count;++i)depth_views[i]=swapchain->depths[i].view;
    swapchain->post=af_vk_post_create(renderer->physical_device,renderer->device,extent,swapchain->image_count,swapchain->views,format.format,swapchain->render_pass,depth_views);
    free(depth_views);if(!swapchain->post||!create_pipeline(renderer)||!create_sky_pipeline(renderer))return false;
    if (renderer->ui && !af_vk_ui_create_pipeline(renderer->ui, af_vk_post_output_pass(swapchain->post))) return false;
    renderer->resize_pending = false;
    renderer->swapchain_invalid = false;
    ++renderer->swapchain_rebuilds;
    af_log("info", "Swapchain: %ux%u, %u images, FIFO.", extent.width, extent.height, swapchain->image_count);
    return true;
}

static bool create_frames(af_renderer *renderer)
{
    VkCommandPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = renderer->graphics_family
    };
    if (!vk_ok(vkCreateCommandPool(renderer->device, &pool_info, NULL, &renderer->command_pool), "vkCreateCommandPool"))
        return false;
    VkCommandBuffer commands[AF_FRAMES_IN_FLIGHT];
    VkCommandBufferAllocateInfo allocation = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = renderer->command_pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = AF_FRAMES_IN_FLIGHT
    };
    if (!vk_ok(vkAllocateCommandBuffers(renderer->device, &allocation, commands), "vkAllocateCommandBuffers"))
        return false;
    for (uint32_t i = 0; i < AF_FRAMES_IN_FLIGHT; ++i) {
        renderer->frames[i].command = commands[i];
        VkSemaphoreCreateInfo semaphore_info = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VkFenceCreateInfo fence_info = {
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT
        };
        if (!vk_ok(vkCreateSemaphore(renderer->device, &semaphore_info, NULL,
            &renderer->frames[i].acquired), "vkCreateSemaphore") ||
            !vk_ok(vkCreateFence(renderer->device, &fence_info, NULL,
                &renderer->frames[i].fence), "vkCreateFence")) return false;
        if(renderer->timestamp_bits){VkQueryPoolCreateInfo query={.sType=VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,.queryType=VK_QUERY_TYPE_TIMESTAMP,.queryCount=5};
            if(!vk_ok(vkCreateQueryPool(renderer->device,&query,NULL,&renderer->frames[i].timestamps),"timestamp query pool"))return false;}
    }
    return true;
}

static void update_environment_descriptors(af_renderer *renderer){
    af_vk_texture *specular=renderer->environment,*diffuse=renderer->irradiance,*sky=renderer->environment;
    for(size_t i=0;i<renderer->environment_count;++i)if(!strcmp(renderer->active_environment,renderer->environments[i].name)){specular=renderer->environments[i].specular;diffuse=renderer->environments[i].diffuse;sky=renderer->environments[i].sky;break;}
    VkDescriptorImageInfo images[]={af_vk_texture_image(specular),af_vk_texture_image(diffuse),af_vk_texture_image(renderer->brdf),af_vk_texture_image(sky)};
    for(unsigned f=0;f<AF_FRAMES_IN_FLIGHT;++f){VkWriteDescriptorSet writes[5];
        VkDescriptorImageInfo local=af_vk_shadows_image(renderer->local_shadows,f);
        writes[4]=(VkWriteDescriptorSet){.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=renderer->frames[f].descriptor,.dstBinding=1,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.pImageInfo=&local};
        for(uint32_t i=0;i<4;++i)writes[i]=(VkWriteDescriptorSet){.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=renderer->frames[f].descriptor,.dstBinding=i+2,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.pImageInfo=&images[i]};
        vkUpdateDescriptorSets(renderer->device,5,writes,0,NULL);}
}
static bool create_environment(af_renderer *renderer,uint32_t profile){
    af_ibl_images images;if(!af_ibl_bake(profile,&images))return false;
    af_vk_texture *environment=af_vk_texture_create_hdr(renderer->physical_device,renderer->device,renderer->graphics_queue,renderer->graphics_family,images.specular,AF_IBL_WIDTH,AF_IBL_HEIGHT,AF_IBL_LEVELS,true);
    af_vk_texture *irradiance=af_vk_texture_create_hdr(renderer->physical_device,renderer->device,renderer->graphics_queue,renderer->graphics_family,images.diffuse,AF_IBL_DIFFUSE_WIDTH,AF_IBL_DIFFUSE_HEIGHT,1,true);
    af_vk_texture *brdf=af_vk_texture_create_hdr(renderer->physical_device,renderer->device,renderer->graphics_queue,renderer->graphics_family,images.brdf,AF_IBL_LUT_SIZE,AF_IBL_LUT_SIZE,1,false);
    af_ibl_free(&images);
    if(!environment||!irradiance||!brdf){af_vk_texture_destroy(environment);af_vk_texture_destroy(irradiance);af_vk_texture_destroy(brdf);return false;}
    if(!af_renderer_wait_idle(renderer)){af_vk_texture_destroy(environment);af_vk_texture_destroy(irradiance);af_vk_texture_destroy(brdf);return false;}
    af_vk_texture_destroy(renderer->environment);af_vk_texture_destroy(renderer->irradiance);af_vk_texture_destroy(renderer->brdf);
    renderer->environment=environment;renderer->irradiance=irradiance;renderer->brdf=brdf;renderer->environment_profile=profile;
    if(renderer->frame_pool)update_environment_descriptors(renderer);return true;
}
static bool create_frame_layout(af_renderer *renderer){
    VkDescriptorSetLayoutBinding bindings[7]={{.binding=0,.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT}};
    for(uint32_t i=1;i<6;++i)bindings[i]=(VkDescriptorSetLayoutBinding){i,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,NULL};
    bindings[6]=(VkDescriptorSetLayoutBinding){6,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_VERTEX_BIT,NULL};
    VkDescriptorSetLayoutCreateInfo layout={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=7,.pBindings=bindings};
    return vk_ok(vkCreateDescriptorSetLayout(renderer->device,&layout,NULL,&renderer->frame_layout),"frame layout");
}
static bool create_uniforms(af_renderer *renderer)
{
    VkDescriptorPoolSize sizes[]={{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,AF_FRAMES_IN_FLIGHT},{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,5*AF_FRAMES_IN_FLIGHT},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,AF_FRAMES_IN_FLIGHT}};
    VkDescriptorPoolCreateInfo pool={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=AF_FRAMES_IN_FLIGHT,.poolSizeCount=3,.pPoolSizes=sizes};
    if(!vk_ok(vkCreateDescriptorPool(renderer->device,&pool,NULL,&renderer->frame_pool),"frame pool"))return false;
    for(unsigned i=0;i<AF_FRAMES_IN_FLIGHT;++i){af_frame *frame=&renderer->frames[i];af_frame_uniform initial={0};
        af_mat4 *bones=calloc(AF_SKIN_MATRICES,sizeof(*bones));if(!bones)return false;for(unsigned j=0;j<AF_SKIN_MATRICES;++j)bones[j]=af_mat4_identity();
        bool bone_ok=upload_buffer(renderer,bones,AF_SKIN_MATRICES*sizeof(*bones),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,&frame->skin_buffer);free(bones);if(!bone_ok)return false;
        if(!upload_buffer(renderer,&initial,sizeof(initial),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,&frame->uniform))return false;
        VkDescriptorSetAllocateInfo allocate={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=renderer->frame_pool,.descriptorSetCount=1,.pSetLayouts=&renderer->frame_layout};
        if(!vk_ok(vkAllocateDescriptorSets(renderer->device,&allocate,&frame->descriptor),"frame descriptor"))return false;
        VkDescriptorBufferInfo buffer={frame->uniform.handle,0,sizeof(initial)};
        VkWriteDescriptorSet write={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=frame->descriptor,.dstBinding=0,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,.pBufferInfo=&buffer};
        vkUpdateDescriptorSets(renderer->device,1,&write,0,NULL);
        VkDescriptorBufferInfo bone_buffer={frame->skin_buffer.handle,0,AF_SKIN_MATRICES*sizeof(af_mat4)};write.dstBinding=6;write.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;write.pBufferInfo=&bone_buffer;vkUpdateDescriptorSets(renderer->device,1,&write,0,NULL);
    }update_environment_descriptors(renderer);return true;
}
af_renderer *af_renderer_create(SDL_Window *window, bool enable_validation)
{
    af_renderer *renderer = calloc(1, sizeof(*renderer));
    if (!renderer) return NULL;
    atomic_init(&renderer->validation_errors, 0);
    renderer->window = window;
    renderer->resize_pending = true;
    if (!create_instance(renderer, enable_validation)) goto fail;
    if (!SDL_Vulkan_CreateSurface(window, renderer->instance, NULL, &renderer->surface)) {
        af_log("error", "Vulkan surface creation: %s", SDL_GetError());
        goto fail;
    }
    if (!create_device(renderer) || !create_frames(renderer)||!create_frame_layout(renderer)) goto fail;
    const unsigned char white[]={255,255,255,255};
    renderer->white=af_vk_texture_create(renderer->physical_device,renderer->device,renderer->graphics_queue,renderer->graphics_family,white,1,1,true,true,renderer->anisotropy);
    const unsigned char flat[]={128,128,255,255};
    renderer->flat_normal=af_vk_texture_create(renderer->physical_device,renderer->device,renderer->graphics_queue,renderer->graphics_family,flat,1,1,false,true,renderer->anisotropy);
    af_shadow_settings shadows=af_shadow_settings_default();renderer->shadow_resolution=shadows.resolution;renderer->shadow_layers=shadows.mode==AF_SHADOW_CASCADED?4u:1u;
    renderer->shadows=af_vk_shadows_create(renderer->physical_device,renderer->device,shadows.resolution,renderer->shadow_layers,renderer->frame_layout);
    renderer->local_shadow_resolution=512;renderer->local_shadow_layers=1;
    renderer->local_shadows=af_vk_shadows_create(renderer->physical_device,renderer->device,512,1,renderer->frame_layout);
    if(!renderer->white||!renderer->flat_normal||!renderer->shadows||!renderer->local_shadows||!create_environment(renderer,0)||!create_uniforms(renderer)||!create_swapchain(renderer))goto fail;
    return renderer;
fail:
    af_renderer_destroy(renderer);
    return NULL;
}

void af_renderer_destroy(af_renderer *renderer)
{
    if (!renderer) return;
    if (renderer->device) {
        (void)vk_ok(vkDeviceWaitIdle(renderer->device), "vkDeviceWaitIdle during shutdown");
        destroy_swapchain(renderer);
        destroy_buffer(renderer,&renderer->capture_buffer);
        af_vk_ui_destroy(renderer->ui);
        af_vk_shadows_destroy(renderer->local_shadows);af_vk_shadows_destroy(renderer->shadows);free(renderer->shadow_draws);free(renderer->shadow_bounds);
        for(size_t i=0;i<renderer->texture_count;++i)af_vk_texture_destroy(renderer->textures[i].texture);
        free(renderer->textures);af_vk_texture_destroy(renderer->white);af_vk_texture_destroy(renderer->flat_normal);
        for(size_t i=0;i<renderer->environment_count;++i){af_vk_texture_destroy(renderer->environments[i].specular);af_vk_texture_destroy(renderer->environments[i].diffuse);af_vk_texture_destroy(renderer->environments[i].sky);}
        af_vk_texture_destroy(renderer->environment);af_vk_texture_destroy(renderer->irradiance);af_vk_texture_destroy(renderer->brdf);
        if(renderer->frame_pool)vkDestroyDescriptorPool(renderer->device,renderer->frame_pool,NULL);
        if(renderer->frame_layout)vkDestroyDescriptorSetLayout(renderer->device,renderer->frame_layout,NULL);
        for (size_t i = 0; i < renderer->mesh_count; ++i) destroy_mesh(renderer, &renderer->meshes[i].gpu);
        free(renderer->meshes);
        for (uint32_t i = 0; i < AF_FRAMES_IN_FLIGHT; ++i) {
            destroy_buffer(renderer,&renderer->frames[i].uniform);destroy_buffer(renderer,&renderer->frames[i].skin_buffer);
            if (renderer->frames[i].acquired) vkDestroySemaphore(renderer->device, renderer->frames[i].acquired, NULL);
            if(renderer->frames[i].timestamps)vkDestroyQueryPool(renderer->device,renderer->frames[i].timestamps,NULL);
            if (renderer->frames[i].fence) vkDestroyFence(renderer->device, renderer->frames[i].fence, NULL);
        }
        if (renderer->command_pool) vkDestroyCommandPool(renderer->device, renderer->command_pool, NULL);
        vkDestroyDevice(renderer->device, NULL);
    }
    if (renderer->surface) SDL_Vulkan_DestroySurface(renderer->instance, renderer->surface, NULL);
    if (renderer->debug_messenger) {
        PFN_vkDestroyDebugUtilsMessengerEXT destroy_debug =
            (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(renderer->instance,
                "vkDestroyDebugUtilsMessengerEXT");
        if (destroy_debug) destroy_debug(renderer->instance, renderer->debug_messenger, NULL);
    }
    if (renderer->instance) vkDestroyInstance(renderer->instance, NULL);
    free(renderer);
}

void af_renderer_resize(af_renderer *renderer)
{
    renderer->resize_pending = true;
}

bool af_renderer_wait_idle(af_renderer *renderer)
{
    return vk_ok(vkDeviceWaitIdle(renderer->device), "vkDeviceWaitIdle");
}

static af_rect resolve_viewport(af_renderer *renderer, af_rect rect)
{
    int width = 0, height = 0;
    SDL_GetWindowSize(renderer->window, &width, &height);
    float w = (float)(width > 0 ? width : 1), h = (float)(height > 0 ? height : 1);
    if (!isfinite(rect.x) || !isfinite(rect.y) || !isfinite(rect.width) || !isfinite(rect.height) ||
        rect.width <= 0 || rect.height <= 0) return (af_rect){0,0,w,h};
    rect.x = fmaxf(0, fminf(w-1,rect.x)); rect.y = fmaxf(0, fminf(h-1,rect.y));
    rect.width = fmaxf(1,fminf(w-rect.x,rect.width)); rect.height = fmaxf(1,fminf(h-rect.y,rect.height));
    return rect;
}

bool af_renderer_upload_overlay_atlas(af_renderer *renderer, const unsigned char *rgba, uint32_t width, uint32_t height)
{
    if (!rgba || !width || !height || width > 8192 || height > 8192 ||
        (uint64_t)width*height*4 > SIZE_MAX || !af_renderer_wait_idle(renderer)) return false;
    af_vk_ui *ui = af_vk_ui_create(renderer->physical_device, renderer->device,
        renderer->graphics_queue, renderer->graphics_family, rgba, width, height);
    if (!ui) return false;
    if (!af_vk_ui_create_pipeline(ui, af_vk_post_output_pass(renderer->swapchain.post))) { af_vk_ui_destroy(ui); return false; }
    af_vk_ui_destroy(renderer->ui);
    renderer->ui = ui;
    return true;
}

static bool object_world(af_renderer *renderer,const af_scene *scene,size_t index,af_mat4 *out){
    if(scene&&renderer->posed_scene==scene&&renderer->posed_count==af_scene_object_count(scene)&&index<renderer->posed_count){*out=renderer->posed_worlds[index];return true;}
    const af_scene_object *o=af_scene_object_at(scene,index);return o&&af_scene_interpolated_world_matrix(scene,renderer->previous_scene,o->id,renderer->interpolation_alpha,out);
}
static uint32_t skin_base(const af_renderer *renderer,af_object_id object,const af_gpu_mesh *mesh);
static af_shadow_bounds object_bounds(af_renderer *renderer,af_object_id object,af_mat4 world,const af_gpu_mesh *mesh);
af_object_id af_renderer_pick_object(af_renderer *renderer, const af_scene *scene,
    const af_camera *camera, af_rect viewport, af_vec2 point)
{
    viewport = resolve_viewport(renderer, viewport);
    if (!isfinite(point.x) || !isfinite(point.y) || point.x < viewport.x || point.y < viewport.y ||
        point.x >= viewport.x+viewport.width || point.y >= viewport.y+viewport.height) return AF_OBJECT_NONE;
    af_vec3 forward = af_camera_forward(camera);
    af_vec3 right = af_vec3_normalize(af_vec3_cross(forward,(af_vec3){0,1,0}));
    af_vec3 up = af_vec3_cross(right,forward);
    float spread = tanf(camera->vertical_fov*0.5f);
    float x = ((point.x-viewport.x)/viewport.width*2-1)*spread*viewport.width/viewport.height;
    float y = (1-(point.y-viewport.y)/viewport.height*2)*spread;
    af_vec3 ray = af_vec3_normalize(af_vec3_add(forward,af_vec3_add(af_vec3_scale(right,x),af_vec3_scale(up,y))));
    float closest = camera->far_clip;
    af_object_id selected = AF_OBJECT_NONE;
    for (size_t i = 0; i < af_scene_object_count(scene); ++i) {
        const af_scene_object *object = af_scene_object_at(scene,i);
        if (!af_scene_object_visible(scene,object->id)||!strcmp(object->mesh,"__node")) continue;
        size_t index = find_mesh_index(renderer,object->mesh);
        if (index == renderer->mesh_count) continue;
        af_mat4 inverse;
        af_mat4 world;
        if (!object_world(renderer,scene,i,&world)||!af_mat4_inverse(world, &inverse)) continue;
        af_vec4 origin = af_mat4_transform(inverse,(af_vec4){camera->position.x,camera->position.y,camera->position.z,1});
        af_vec4 direction = af_mat4_transform(inverse,(af_vec4){ray.x,ray.y,ray.z,0});
        af_gpu_mesh *mesh = &renderer->meshes[index].gpu;
        af_vec3 minimum=mesh->minimum,maximum=mesh->maximum;
        if(skin_base(renderer,object->id,mesh)){af_shadow_bounds bounds=object_bounds(renderer,object->id,world,mesh);minimum=(af_vec3){INFINITY,INFINITY,INFINITY};maximum=(af_vec3){-INFINITY,-INFINITY,-INFINITY};
            for(unsigned j=0;j<8;++j){af_vec3 p=bounds.corners[j];minimum=(af_vec3){fminf(minimum.x,p.x),fminf(minimum.y,p.y),fminf(minimum.z,p.z)};maximum=(af_vec3){fmaxf(maximum.x,p.x),fmaxf(maximum.y,p.y),fmaxf(maximum.z,p.z)};}
            origin=(af_vec4){camera->position.x,camera->position.y,camera->position.z,1};direction=(af_vec4){ray.x,ray.y,ray.z,0};}
        float o[] = {origin.x,origin.y,origin.z}, d[] = {direction.x,direction.y,direction.z};
        float lo[] = {minimum.x,minimum.y,minimum.z}, hi[] = {maximum.x,maximum.y,maximum.z};
        float near = camera->near_clip, far = closest;
        bool hit = true;
        for (unsigned axis = 0; axis < 3; ++axis) {
            if (fabsf(d[axis]) < 1e-12f) { if (o[axis] < lo[axis] || o[axis] > hi[axis]) hit = false; continue; }
            float a = (lo[axis]-o[axis])/d[axis], b = (hi[axis]-o[axis])/d[axis];
            near = fmaxf(near,fminf(a,b)); far = fminf(far,fmaxf(a,b));
            if (near > far) hit = false;
        }
        if (hit && near < closest) { closest = near; selected = object->id; }
    }
    return selected;
}

static uint32_t skin_base(const af_renderer *renderer,af_object_id object,const af_gpu_mesh *mesh){
    if(!mesh->skinned)return 0;for(size_t i=0;i<renderer->skin_count;++i)if(renderer->skins[i].object==object&&renderer->skins[i].count>mesh->max_joint)return 1u+(uint32_t)i*AF_ANIMATION_MAX_JOINTS;return 0;
}
static af_shadow_bounds object_bounds(af_renderer *renderer,af_object_id object,af_mat4 world,const af_gpu_mesh *mesh){
    uint32_t base=skin_base(renderer,object,mesh);if(!base)return af_shadow_transform_bounds(world,mesh->minimum,mesh->maximum);
    const af_skin_instance *skin=&renderer->skins[(base-1)/AF_ANIMATION_MAX_JOINTS];af_vec3 lo={INFINITY,INFINITY,INFINITY},hi={-INFINITY,-INFINITY,-INFINITY};
    for(size_t i=0;i<skin->count;++i){af_shadow_bounds b=af_shadow_transform_bounds(af_mat4_mul(world,skin->matrices[i]),mesh->minimum,mesh->maximum);
        for(unsigned j=0;j<8;++j){af_vec3 v=b.corners[j];lo=(af_vec3){fminf(lo.x,v.x),fminf(lo.y,v.y),fminf(lo.z,v.z)};hi=(af_vec3){fmaxf(hi.x,v.x),fmaxf(hi.y,v.y),fmaxf(hi.z,v.z)};}}
    return af_shadow_transform_bounds(af_mat4_identity(),lo,hi);
}
static bool prepare_shadows(af_renderer *renderer,const af_scene *scene,const af_camera *camera,af_rect area,af_frame_uniform *uniform,af_shadow_view *view)
{
    af_shadow_settings settings=af_scene_get_shadows(scene);uint32_t layers=settings.mode==AF_SHADOW_CASCADED?4u:1u;
    if(settings.resolution!=renderer->shadow_resolution||layers!=renderer->shadow_layers){
        if(!af_renderer_wait_idle(renderer))return false;
        af_vk_shadows *replacement=af_vk_shadows_create(renderer->physical_device,renderer->device,settings.resolution,layers,renderer->frame_layout);
        if(!replacement)return false;af_vk_shadows_destroy(renderer->shadows);renderer->shadows=replacement;
        renderer->shadow_resolution=settings.resolution;renderer->shadow_layers=layers;
        for(unsigned f=0;f<AF_FRAMES_IN_FLIGHT;++f)memset(renderer->frames[f].shadow_signatures,0,sizeof(renderer->frames[f].shadow_signatures));
    }
    size_t count=af_scene_object_count(scene);
    if(count>renderer->shadow_capacity){
        af_shadow_draw *draws=realloc(renderer->shadow_draws,count*sizeof(*draws));if(!draws)return false;renderer->shadow_draws=draws;
        af_shadow_bounds *bounds=realloc(renderer->shadow_bounds,count*sizeof(*bounds));if(!bounds)return false;renderer->shadow_bounds=bounds;renderer->shadow_capacity=count;
    }
    renderer->shadow_count=0;
    bool local_cast=false;for(size_t i=0;i<af_scene_local_light_count(scene);++i){const af_local_light *l=af_scene_local_light_at(scene,i);if(l->enabled&&l->cast_shadows&&l->intensity>0)local_cast=true;}
    for(size_t i=0;(settings.enabled||local_cast)&&i<count;++i){const af_scene_object *object=af_scene_object_at(scene,i);
        if(!object->material.cast_shadows||!af_scene_object_visible(scene,object->id))continue;
        size_t mesh=find_mesh_index(renderer,object->mesh);af_mat4 model;
        if(mesh==renderer->mesh_count||!object_world(renderer,scene,i,&model))continue;
        size_t n=renderer->shadow_count++;
        renderer->shadow_draws[n]=(af_shadow_draw){model,mesh,skin_base(renderer,object->id,&renderer->meshes[mesh].gpu)};
        renderer->shadow_bounds[n]=object_bounds(renderer,object->id,model,&renderer->meshes[mesh].gpu);
    }
    af_directional_light light=af_scene_get_light(scene);af_vec3 direction=af_vec3_normalize(light.direction),forward=af_camera_forward(camera);
    if(!af_shadow_build(camera,area.width/area.height,direction,settings,renderer->shadow_bounds,renderer->shadow_count,view))return false;
    *uniform=(af_frame_uniform){.view_projection=af_mat4_mul(af_mat4_perspective(camera->vertical_fov,area.width/area.height,camera->near_clip,camera->far_clip),af_camera_view(camera)),
        .direction_intensity={direction.x,direction.y,direction.z,light.intensity},.color_ambient={light.color.x,light.color.y,light.color.z,light.ambient},
        .cascade_splits={view->splits[0],view->splits[1],view->splits[2],view->splits[3]},
        .camera_position={camera->position.x,camera->position.y,camera->position.z,camera->near_clip},.camera_forward={forward.x,forward.y,forward.z,0},
        .shadow_params={settings.enabled?1.0f:0.0f,1.0f/(float)settings.resolution,0.00015f,settings.normal_bias},
        .cascade_params={(float)view->count,(float)settings.filter_radius,settings.blend_fraction,settings.debug_cascades?1.0f:0.0f}};
    af_render_settings render_settings=af_scene_get_render_settings(scene);uniform->render_params=(af_vec4){exp2f(render_settings.exposure),render_settings.tone_mapping?1.0f:0.0f,render_settings.environment_enabled?render_settings.environment_intensity:0,render_settings.environment_rotation};
    if(!af_mat4_inverse(uniform->view_projection,&uniform->inverse_view_projection))return false;
    uniform->sky_params=(af_vec4){render_settings.skybox_intensity,0,0,0};
    uint32_t local_layers=0,shadow_lights=0;unsigned requested=0,selected[AF_LOCAL_SHADOW_LIGHTS];
    af_local_light local_lights[AF_SCENE_MAX_LOCAL_LIGHTS];size_t local_count=af_scene_local_light_count(scene);for(size_t i=0;i<local_count;++i)local_lights[i]=*af_scene_local_light_at(scene,i);
    unsigned selected_count=af_local_shadow_select(local_lights,local_count,camera,area.width/area.height,selected,&requested);
    for(size_t i=0;i<af_scene_local_light_count(scene);++i){const af_local_light *local=af_scene_local_light_at(scene,i);if(!local->enabled)continue;
        af_vec3 ray=af_vec3_normalize(local->direction);
        unsigned n=(unsigned)uniform->light_count.x;uniform->lights[n]=(af_gpu_light){.position_range={local->position.x,local->position.y,local->position.z,local->range},
            .color_intensity={local->color.x,local->color.y,local->color.z,local->intensity},.direction_inner={ray.x,ray.y,ray.z,cosf(local->inner_angle)},
            .params={cosf(local->outer_angle),local->type==AF_LIGHT_SPOT?1.0f:0.0f,0,0}};uniform->light_count.x+=1;
        bool chosen=false;for(unsigned j=0;j<selected_count;++j)if(selected[j]==i)chosen=true;
        if(chosen){
            uniform->lights[n].params.z=(float)(local_layers+1);uniform->lights[n].params.w=local->shadow_bias;
            unsigned faces=local->type==AF_LIGHT_POINT?6u:1u;
            for(unsigned face=0;face<faces;++face)uniform->local_shadow_matrices[local_layers++]=af_local_shadow_matrix(*local,face);
            ++shadow_lights;
        }
    }
    renderer->local_shadow_lights=shadow_lights;renderer->local_shadow_overflow=requested-shadow_lights;
    uint32_t wanted_layers=local_layers?local_layers:1;
    if(wanted_layers!=renderer->local_shadow_layers||render_settings.local_shadow_resolution!=renderer->local_shadow_resolution){
        if(!af_renderer_wait_idle(renderer))return false;
        af_vk_shadows *replacement=af_vk_shadows_create(renderer->physical_device,renderer->device,render_settings.local_shadow_resolution,wanted_layers,renderer->frame_layout);if(!replacement)return false;
        af_vk_shadows_destroy(renderer->local_shadows);renderer->local_shadows=replacement;renderer->local_shadow_layers=wanted_layers;renderer->local_shadow_resolution=render_settings.local_shadow_resolution;
        update_environment_descriptors(renderer);
        for(unsigned f=0;f<AF_FRAMES_IN_FLIGHT;++f)memset(renderer->frames[f].local_shadow_signatures,0,sizeof(renderer->frames[f].local_shadow_signatures));
    }
    uniform->light_count.y=1.0f/(float)renderer->local_shadow_resolution;uniform->light_count.z=(float)local_layers;
    memcpy(uniform->shadow_matrices,view->matrices,sizeof(view->matrices));return true;
}
static uint64_t shadow_hash(uint64_t h,const void *data,size_t size){const unsigned char *p=data;for(size_t i=0;i<size;++i){h^=p[i];h*=UINT64_C(1099511628211);}return h;}
static uint64_t shadow_signature(af_renderer *r,af_mat4 matrix,af_shadow_settings settings,bool enabled){
    uint64_t h=shadow_hash(UINT64_C(14695981039346656037),&matrix,sizeof(matrix));
    h=shadow_hash(h,&settings.constant_bias,sizeof(float));h=shadow_hash(h,&settings.slope_bias,sizeof(float));h=shadow_hash(h,&r->mesh_revision,sizeof(r->mesh_revision));h=shadow_hash(h,&enabled,sizeof(enabled));h=shadow_hash(h,&r->skin_revision,sizeof(r->skin_revision));
    if(enabled)for(size_t i=0;i<r->shadow_count;++i){h=shadow_hash(h,&r->shadow_draws[i].model,sizeof(af_mat4));h=shadow_hash(h,&r->shadow_draws[i].mesh,sizeof(size_t));}
    return h?h:1;
}
bool af_renderer_draw(af_renderer *renderer, af_color color, const af_scene *scene, const af_camera *camera, af_rect viewport_rect, af_object_id highlighted, const af_overlay_frame *overlay, bool *rendered)
{
    *rendered = false;renderer->missing_textures=0;
    if (renderer->resize_pending || !renderer->swapchain.handle) {
        if (!create_swapchain(renderer)) return false;
        if (renderer->resize_pending || !renderer->swapchain.handle) return true;
    }
    af_render_settings environment_settings=af_scene_get_render_settings(scene);
    if((environment_settings.environment_enabled||environment_settings.skybox)&&!environment_settings.environment_asset[0]&&environment_settings.environment_profile!=renderer->environment_profile&&!create_environment(renderer,environment_settings.environment_profile))return false;
    bool environment_found=!environment_settings.environment_asset[0];
    for(size_t i=0;i<renderer->environment_count;++i)if(!strcmp(renderer->environments[i].name,environment_settings.environment_asset))environment_found=true;
    if(!environment_found&&(environment_settings.environment_enabled||environment_settings.skybox))++renderer->missing_textures;
    if(strcmp(renderer->active_environment,environment_settings.environment_asset)){
        if(!af_renderer_wait_idle(renderer))return false;snprintf(renderer->active_environment,sizeof(renderer->active_environment),"%s",environment_settings.environment_asset);update_environment_descriptors(renderer);
    }
    af_frame_uniform uniform;af_shadow_view shadow_view;
    if(!prepare_shadows(renderer,scene,camera,resolve_viewport(renderer,viewport_rect),&uniform,&shadow_view))return false;
    af_frame *frame = &renderer->frames[renderer->frame_index];
    Uint64 wait_start=SDL_GetPerformanceCounter();
    if (!vk_ok(vkWaitForFences(renderer->device, 1, &frame->fence, VK_TRUE, UINT64_MAX), "vkWaitForFences"))
        return false;
    renderer->cpu_fence_ms=(double)(SDL_GetPerformanceCounter()-wait_start)*1000/(double)SDL_GetPerformanceFrequency();
    if(frame->timestamp_submitted){uint64_t values[5];
        VkResult query=vkGetQueryPoolResults(renderer->device,frame->timestamps,0,5,sizeof(values),values,sizeof(uint64_t),VK_QUERY_RESULT_64_BIT);
        if(query==VK_SUCCESS){uint64_t mask=renderer->timestamp_bits==64?UINT64_MAX:((UINT64_C(1)<<renderer->timestamp_bits)-1);
            double scale=(double)renderer->timestamp_period/1000000;
            renderer->gpu_ms[0]=(double)((values[4]-values[0])&mask)*scale;
            for(unsigned i=1;i<5;++i)renderer->gpu_ms[i]=(double)((values[i]-values[i-1])&mask)*scale;renderer->timing_valid=true;
        }else if(query!=VK_NOT_READY)return vk_ok(query,"timestamp results");
    }
    void *skin_mapped=NULL;if(!vk_ok(vkMapMemory(renderer->device,frame->skin_buffer.memory,0,VK_WHOLE_SIZE,0,&skin_mapped),"map skin matrices"))return false;
    af_mat4 *skin_destination=skin_mapped;skin_destination[0]=af_mat4_identity();
    for(size_t i=0;i<renderer->skin_count;++i)for(unsigned j=0;j<AF_ANIMATION_MAX_JOINTS;++j)skin_destination[1+i*AF_ANIMATION_MAX_JOINTS+j]=j<renderer->skins[i].count?renderer->skins[i].matrices[j]:af_mat4_identity();
    VkMappedMemoryRange skin_range={.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,.memory=frame->skin_buffer.memory,.size=VK_WHOLE_SIZE};bool skin_flushed=vk_ok(vkFlushMappedMemoryRanges(renderer->device,1,&skin_range),"flush skin matrices");vkUnmapMemory(renderer->device,frame->skin_buffer.memory);if(!skin_flushed)return false;
    uint32_t image_index = 0;
    wait_start=SDL_GetPerformanceCounter();
    VkResult result = vkAcquireNextImageKHR(renderer->device, renderer->swapchain.handle,
        UINT64_MAX, frame->acquired, VK_NULL_HANDLE, &image_index);
    renderer->cpu_acquire_ms=(double)(SDL_GetPerformanceCounter()-wait_start)*1000/(double)SDL_GetPerformanceFrequency();
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        renderer->resize_pending = renderer->swapchain_invalid = true;
        return true;
    }
    if (result == VK_SUBOPTIMAL_KHR) renderer->resize_pending = renderer->swapchain_invalid = true;
    else if (!vk_ok(result, "vkAcquireNextImageKHR")) return false;
    if (overlay->index_count && (!renderer->ui || !af_vk_ui_prepare(renderer->ui, renderer->frame_index, overlay))) return false;
    void *mapped=NULL;if(!vk_ok(vkMapMemory(renderer->device,frame->uniform.memory,0,VK_WHOLE_SIZE,0,&mapped),"map frame uniform"))return false;
    memcpy(mapped,&uniform,sizeof(uniform));
    VkMappedMemoryRange range={.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,.memory=frame->uniform.memory,.size=VK_WHOLE_SIZE};
    bool flushed=vk_ok(vkFlushMappedMemoryRanges(renderer->device,1,&range),"flush frame uniform");vkUnmapMemory(renderer->device,frame->uniform.memory);if(!flushed)return false;
    if (!vk_ok(vkResetCommandBuffer(frame->command, 0), "vkResetCommandBuffer")) return false;
    VkCommandBufferBeginInfo begin = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    };
    if (!vk_ok(vkBeginCommandBuffer(frame->command, &begin), "vkBeginCommandBuffer")) return false;
    if(renderer->ui)af_vk_ui_record_uploads(renderer->ui,frame->command,renderer->frame_index);
    if(frame->timestamps){vkCmdResetQueryPool(frame->command,frame->timestamps,0,5);vkCmdWriteTimestamp(frame->command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,frame->timestamps,0);}
    renderer->shadow_draw_calls=0;renderer->shadow_passes=renderer->shadow_cached_passes=0;
    for(uint32_t layer=0;layer<shadow_view.count;++layer){
        uint64_t signature=shadow_signature(renderer,shadow_view.matrices[layer],af_scene_get_shadows(scene),uniform.shadow_params.x>.5f);
        if(frame->shadow_signatures[layer]==signature){++renderer->shadow_cached_passes;continue;}frame->shadow_signatures[layer]=signature;++renderer->shadow_passes;
        af_vk_shadows_begin(renderer->shadows,frame->command,renderer->frame_index,layer,af_scene_get_shadows(scene),frame->descriptor);
        for(size_t i=0;uniform.shadow_params.x>.5f&&i<renderer->shadow_count;++i){
            if(!af_shadow_intersects(shadow_view.matrices[layer],&renderer->shadow_bounds[i]))continue;
            af_shadow_draw draw=renderer->shadow_draws[i];const af_gpu_mesh *mesh=&renderer->meshes[draw.mesh].gpu;
            af_mat4 mvp=af_mat4_mul(shadow_view.matrices[layer],draw.model);af_vk_shadows_push(renderer->shadows,frame->command,mvp.elements,draw.skin_base);
            VkDeviceSize offset=0;vkCmdBindVertexBuffers(frame->command,0,1,&mesh->vertices.handle,&offset);
            vkCmdBindIndexBuffer(frame->command,mesh->indices.handle,0,VK_INDEX_TYPE_UINT32);vkCmdDrawIndexed(frame->command,mesh->index_count,1,0,0,0);++renderer->shadow_draw_calls;
        }
        vkCmdEndRenderPass(frame->command);
    }
    for(uint32_t layer=0;layer<renderer->local_shadow_layers;++layer){
        af_shadow_settings bias=af_scene_get_shadows(scene);bias.constant_bias=1;bias.slope_bias=1.5f;
        uint64_t signature=shadow_signature(renderer,uniform.local_shadow_matrices[layer],bias,layer<(uint32_t)uniform.light_count.z);
        if(frame->local_shadow_signatures[layer]==signature){++renderer->shadow_cached_passes;continue;}frame->local_shadow_signatures[layer]=signature;++renderer->shadow_passes;
        af_vk_shadows_begin(renderer->local_shadows,frame->command,renderer->frame_index,layer,bias,frame->descriptor);
        for(size_t i=0;layer<(uint32_t)uniform.light_count.z&&i<renderer->shadow_count;++i){
            if(!af_shadow_intersects(uniform.local_shadow_matrices[layer],&renderer->shadow_bounds[i]))continue;
            af_shadow_draw draw=renderer->shadow_draws[i];const af_gpu_mesh *mesh=&renderer->meshes[draw.mesh].gpu;
            af_mat4 mvp=af_mat4_mul(uniform.local_shadow_matrices[layer],draw.model);af_vk_shadows_push(renderer->local_shadows,frame->command,mvp.elements,draw.skin_base);
            VkDeviceSize offset=0;vkCmdBindVertexBuffers(frame->command,0,1,&mesh->vertices.handle,&offset);vkCmdBindIndexBuffer(frame->command,mesh->indices.handle,0,VK_INDEX_TYPE_UINT32);vkCmdDrawIndexed(frame->command,mesh->index_count,1,0,0,0);++renderer->shadow_draw_calls;
        }vkCmdEndRenderPass(frame->command);
    }
    if(frame->timestamps)vkCmdWriteTimestamp(frame->command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,frame->timestamps,1);
    VkClearValue clears[] = {
        {.color = {.float32 = {color.r, color.g, color.b, color.a}}},
        {.depthStencil = {.depth = 1.0f}}
    };
    VkRenderPassBeginInfo pass = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = renderer->swapchain.render_pass,
        .framebuffer = af_vk_post_scene_framebuffer(renderer->swapchain.post,image_index),
        .renderArea = {.extent = renderer->swapchain.extent},
        .clearValueCount = 2, .pClearValues = clears
    };
    vkCmdBeginRenderPass(frame->command, &pass, VK_SUBPASS_CONTENTS_INLINE);
    size_t draw_calls = 0, missing_meshes = 0;
    size_t object_count = af_scene_object_count(scene);
    if (object_count > 0||environment_settings.skybox) {
        VkExtent2D extent = renderer->swapchain.extent;
        int logical_width = 0, logical_height = 0;
        SDL_GetWindowSize(renderer->window, &logical_width, &logical_height);
        af_rect area = resolve_viewport(renderer, viewport_rect);
        float sx = (float)extent.width / (float)(logical_width > 0 ? logical_width : 1);
        float sy = (float)extent.height / (float)(logical_height > 0 ? logical_height : 1);
        VkViewport viewport = {.x = area.x*sx, .y = area.y*sy,
            .width = area.width*sx, .height = area.height*sy, .maxDepth = 1.0f};
        VkRect2D scissor = {.offset = {(int32_t)viewport.x,(int32_t)viewport.y},
            .extent = {(uint32_t)viewport.width,(uint32_t)viewport.height}};
        VkDeviceSize offset = 0;
        if(environment_settings.skybox){
            vkCmdBindPipeline(frame->command,VK_PIPELINE_BIND_POINT_GRAPHICS,renderer->swapchain.sky_pipeline);
            vkCmdBindDescriptorSets(frame->command,VK_PIPELINE_BIND_POINT_GRAPHICS,renderer->swapchain.pipeline_layout,0,1,&frame->descriptor,0,NULL);
            vkCmdSetViewport(frame->command,0,1,&viewport);vkCmdSetScissor(frame->command,0,1,&scissor);vkCmdDraw(frame->command,3,1,0,0);
        }
        vkCmdBindPipeline(frame->command, VK_PIPELINE_BIND_POINT_GRAPHICS, renderer->swapchain.pipeline);
        vkCmdBindDescriptorSets(frame->command,VK_PIPELINE_BIND_POINT_GRAPHICS,renderer->swapchain.pipeline_layout,0,1,&frame->descriptor,0,NULL);
        VkDescriptorSet shadow_descriptor=af_vk_shadows_descriptor(renderer->shadows,renderer->frame_index);
        vkCmdBindDescriptorSets(frame->command,VK_PIPELINE_BIND_POINT_GRAPHICS,renderer->swapchain.pipeline_layout,2,1,&shadow_descriptor,0,NULL);
        vkCmdSetViewport(frame->command, 0, 1, &viewport);
        vkCmdSetScissor(frame->command, 0, 1, &scissor);
        for (size_t i = 0; i < object_count; ++i) {
            const af_scene_object *object = af_scene_object_at(scene, i);
            if (!af_scene_object_visible(scene,object->id)||!strcmp(object->mesh,"__node")) continue;
            size_t index = find_mesh_index(renderer, object->mesh);
            if (index == renderer->mesh_count) {
                ++missing_meshes;
                continue;
            }
            const af_gpu_mesh *mesh = &renderer->meshes[index].gpu;
            af_color base=object->material.base_color;
            if(object->id==highlighted){base.r*=1.25f;base.g*=1.1f;base.b*=0.75f;}
            af_mat4 world;if(!object_world(renderer,scene,i,&world))continue;
            struct {af_mat4 model;af_color tint;af_vec4 params,surface,emission;} transform={world,base,{object->material.unlit?1.0f:0.0f,object->material.receive_shadows?1.0f:0.0f,object->material.physically_based?1.0f:0.0f,object->material.normal_map[0]?1.0f:0.0f},{object->material.roughness,object->material.metallic,object->material.normal_strength,(float)skin_base(renderer,object->id,mesh)},{object->material.emission.x,object->material.emission.y,object->material.emission.z,object->material.emission_strength}};
            _Static_assert(sizeof(transform)==128,"Portable draw push constant limit");
            af_vk_texture *texture=renderer->white;
            if(object->material.texture[0]){bool found=false;for(size_t t=0;t<renderer->texture_count;++t)if(!renderer->textures[t].normal&&!strcmp(renderer->textures[t].name,object->material.texture)){texture=renderer->textures[t].texture;found=true;break;}
                if(!found)++renderer->missing_textures;}
            VkDescriptorSet descriptor=af_vk_texture_descriptor(texture);
            vkCmdBindDescriptorSets(frame->command,VK_PIPELINE_BIND_POINT_GRAPHICS,renderer->swapchain.pipeline_layout,1,1,&descriptor,0,NULL);
            af_vk_texture *normal=renderer->flat_normal;
            if(object->material.normal_map[0]){bool found=false;for(size_t t=0;t<renderer->texture_count;++t)if(renderer->textures[t].normal&&!strcmp(renderer->textures[t].name,object->material.normal_map)){normal=renderer->textures[t].texture;found=true;break;}if(!found)++renderer->missing_textures;}
            VkDescriptorSet normal_descriptor=af_vk_texture_descriptor(normal);
            vkCmdBindDescriptorSets(frame->command,VK_PIPELINE_BIND_POINT_GRAPHICS,renderer->swapchain.pipeline_layout,3,1,&normal_descriptor,0,NULL);
            af_vk_texture *mr=renderer->white;
            if(object->material.metallic_roughness_map[0]){bool found=false;for(size_t t=0;t<renderer->texture_count;++t)if(renderer->textures[t].normal&&!strcmp(renderer->textures[t].name,object->material.metallic_roughness_map)){mr=renderer->textures[t].texture;found=true;break;}if(!found)++renderer->missing_textures;}
            VkDescriptorSet mr_descriptor=af_vk_texture_descriptor(mr);
            vkCmdBindDescriptorSets(frame->command,VK_PIPELINE_BIND_POINT_GRAPHICS,renderer->swapchain.pipeline_layout,4,1,&mr_descriptor,0,NULL);
            vkCmdPushConstants(frame->command,renderer->swapchain.pipeline_layout,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(transform),&transform);
            vkCmdBindVertexBuffers(frame->command, 0, 1, &mesh->vertices.handle, &offset);
            vkCmdBindIndexBuffer(frame->command, mesh->indices.handle, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(frame->command, mesh->index_count, 1, 0, 0, 0);
            ++draw_calls;
        }
    }
    vkCmdEndRenderPass(frame->command);
    int logical_w=0,logical_h=0;SDL_GetWindowSize(renderer->window,&logical_w,&logical_h);
    af_rect post_area=resolve_viewport(renderer,viewport_rect);post_area.x/=(float)(logical_w?logical_w:1);post_area.y/=(float)(logical_h?logical_h:1);post_area.width/=(float)(logical_w?logical_w:1);post_area.height/=(float)(logical_h?logical_h:1);
    if(frame->timestamps)vkCmdWriteTimestamp(frame->command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,frame->timestamps,2);
    af_vk_post_draw(renderer->swapchain.post,frame->command,image_index,af_scene_get_render_settings(scene),post_area);
    if(frame->timestamps)vkCmdWriteTimestamp(frame->command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,frame->timestamps,3);
    if (renderer->ui && overlay->index_count)
        af_vk_ui_draw(renderer->ui, frame->command, renderer->frame_index, overlay, renderer->swapchain.extent);
    vkCmdEndRenderPass(frame->command);
    if(frame->timestamps)vkCmdWriteTimestamp(frame->command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,frame->timestamps,4);
    if(renderer->capture_pending){
        int w=0,h=0;SDL_GetWindowSize(renderer->window,&w,&h);
        uint32_t x=(uint32_t)fmaxf(0,fminf((float)renderer->swapchain.extent.width-1,renderer->capture_position.x*(float)renderer->swapchain.extent.width/(float)(w?w:1)));
        uint32_t y=(uint32_t)fmaxf(0,fminf((float)renderer->swapchain.extent.height-1,renderer->capture_position.y*(float)renderer->swapchain.extent.height/(float)(h?h:1)));
        VkImageMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.oldLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
            .image=renderer->swapchain.images[image_index],.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
        vkCmdPipelineBarrier(frame->command,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&barrier);
        VkBufferImageCopy copy={.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageOffset={(int32_t)x,(int32_t)y,0},.imageExtent={1,1,1}};
        vkCmdCopyImageToBuffer(frame->command,barrier.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,renderer->capture_buffer.handle,1,&copy);
        VkBufferMemoryBarrier host={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT,
            .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.buffer=renderer->capture_buffer.handle,.size=VK_WHOLE_SIZE};
        vkCmdPipelineBarrier(frame->command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,NULL,1,&host,0,NULL);
        barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;barrier.newLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;barrier.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT;barrier.dstAccessMask=0;
        vkCmdPipelineBarrier(frame->command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,0,0,NULL,0,NULL,1,&barrier);
        renderer->capture_pending=false;renderer->capture_ready=true;renderer->capture_format=renderer->swapchain.surface_format.format;
    }
    if (!vk_ok(vkEndCommandBuffer(frame->command), "vkEndCommandBuffer")) return false;
    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    VkSemaphore ready = renderer->swapchain.present_ready[image_index];
    VkSubmitInfo submit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1, .pWaitSemaphores = &frame->acquired, .pWaitDstStageMask = &wait_stage,
        .commandBufferCount = 1, .pCommandBuffers = &frame->command,
        .signalSemaphoreCount = 1, .pSignalSemaphores = &ready
    };
    /* Reset only after acquisition succeeds, so out-of-date frames cannot
       leave an unsignaled fence that hangs the next frame. */
    if (!vk_ok(vkResetFences(renderer->device, 1, &frame->fence), "vkResetFences") ||
        !vk_ok(vkQueueSubmit(renderer->graphics_queue, 1, &submit, frame->fence), "vkQueueSubmit")) return false;
    frame->timestamp_submitted=frame->timestamps!=VK_NULL_HANDLE;
    renderer->draw_calls = draw_calls;
    if (missing_meshes && missing_meshes != renderer->missing_meshes)
        af_log("warning", "%zu visible scene objects reference unloaded mesh assets.", missing_meshes);
    renderer->missing_meshes = missing_meshes;
    VkPresentInfoKHR present = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1, .pWaitSemaphores = &ready,
        .swapchainCount = 1, .pSwapchains = &renderer->swapchain.handle, .pImageIndices = &image_index
    };
    result = vkQueuePresentKHR(renderer->present_queue, &present);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
        renderer->resize_pending = renderer->swapchain_invalid = true;
    else if (!vk_ok(result, "vkQueuePresentKHR")) return false;
    renderer->frame_index = (renderer->frame_index + 1) % AF_FRAMES_IN_FLIGHT;
    if (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR) {
        ++renderer->rendered_frames;
        *rendered = true;
    }
    return true;
}

af_engine_stats af_renderer_stats(const af_renderer *renderer)
{
    uint64_t texture_bytes=0;uint32_t mip_levels=0;
    for(size_t i=0;i<renderer->texture_count;++i){af_texture_info info=af_vk_texture_info(renderer->textures[i].texture);texture_bytes+=info.allocation_bytes;mip_levels+=info.mip_levels;}
    return (af_engine_stats){
        .gpu_timing_supported=renderer->timestamp_bits!=0,.gpu_timing_valid=renderer->timing_valid,
        .gpu_frame_ms=renderer->gpu_ms[0],.gpu_shadow_ms=renderer->gpu_ms[1],.gpu_scene_ms=renderer->gpu_ms[2],.gpu_post_ms=renderer->gpu_ms[3],.gpu_overlay_ms=renderer->gpu_ms[4],
        .cpu_fence_ms=renderer->cpu_fence_ms,.cpu_acquire_ms=renderer->cpu_acquire_ms,
        .hdr_enabled=renderer->swapchain.post!=NULL,.local_shadow_lights=renderer->local_shadow_lights,.local_shadow_maps=renderer->local_shadow_lights?renderer->local_shadow_layers:0,.local_shadow_resolution=renderer->local_shadow_resolution,.local_shadow_overflow=renderer->local_shadow_overflow,
        .texture_bytes=texture_bytes,.texture_mip_levels=mip_levels,.texture_anisotropy=renderer->anisotropy,
        .rendered_frames = renderer->rendered_frames,
        .swapchain_rebuilds = renderer->swapchain_rebuilds,
        .loaded_meshes = renderer->mesh_count,
        .loaded_textures=renderer->texture_count,.missing_textures=renderer->missing_textures,
        .draw_calls = renderer->draw_calls,
        .shadow_draw_calls=renderer->shadow_draw_calls,.shadow_passes=renderer->shadow_passes,.shadow_cached_passes=renderer->shadow_cached_passes,.shadow_cascades=renderer->shadow_layers,.shadow_resolution=renderer->shadow_resolution,
        .missing_meshes = renderer->missing_meshes,
        .validation_errors = atomic_load(&renderer->validation_errors),
        .validation_enabled = renderer->validation_enabled
    };
}

static bool upload_texture(af_renderer *renderer,const char *name,const unsigned char *rgba,uint32_t width,uint32_t height,bool normal){
    if(!name||!name[0]||strlen(name)>=AF_MESH_NAME_CAPACITY||!rgba||!width||!height||width>8192||height>8192)return false;
    size_t index=0;while(index<renderer->texture_count&&strcmp(renderer->textures[index].name,name))++index;
    if(index==renderer->texture_count&&renderer->texture_count>=AF_SCENE_MAX_ASSETS)return false;
    if(!af_renderer_wait_idle(renderer))return false;
    af_vk_texture *texture=af_vk_texture_create(renderer->physical_device,renderer->device,renderer->graphics_queue,renderer->graphics_family,rgba,width,height,!normal,true,renderer->anisotropy);
    if(!texture)return false;
    if(index==renderer->texture_count){
        if(renderer->texture_count==renderer->texture_capacity){size_t n=renderer->texture_capacity?renderer->texture_capacity*2:8;
            af_texture_asset *assets=realloc(renderer->textures,n*sizeof(*assets));if(!assets){af_vk_texture_destroy(texture);return false;}renderer->textures=assets;renderer->texture_capacity=n;}
        renderer->textures[index]=(af_texture_asset){0};snprintf(renderer->textures[index].name,sizeof(renderer->textures[index].name),"%s",name);++renderer->texture_count;
    }else af_vk_texture_destroy(renderer->textures[index].texture);
    renderer->textures[index].texture=texture;renderer->textures[index].normal=normal;return true;
}

bool af_renderer_remove_texture(af_renderer *renderer,const char *name){
    for(size_t i=0;i<renderer->environment_count;++i)if(!strcmp(renderer->environments[i].name,name)){
        if(!af_renderer_wait_idle(renderer))return false;af_environment_asset *asset=&renderer->environments[i];af_vk_texture_destroy(asset->specular);af_vk_texture_destroy(asset->diffuse);af_vk_texture_destroy(asset->sky);
        *asset=renderer->environments[--renderer->environment_count];update_environment_descriptors(renderer);return true;}

    if(!name)return false;size_t i=0;while(i<renderer->texture_count&&strcmp(renderer->textures[i].name,name))++i;
    if(i==renderer->texture_count||!af_renderer_wait_idle(renderer))return false;
    af_vk_texture_destroy(renderer->textures[i].texture);memmove(&renderer->textures[i],&renderer->textures[i+1],(renderer->texture_count-i-1)*sizeof(*renderer->textures));--renderer->texture_count;return true;
}

bool af_renderer_test_capture_pixel(af_renderer *renderer,af_vec2 position){
    if(!renderer->capture_supported||renderer->capture_pending||!isfinite(position.x)||!isfinite(position.y))return false;
    if(!renderer->capture_buffer.handle){const unsigned char zero[4]={0};if(!upload_buffer(renderer,zero,4,VK_BUFFER_USAGE_TRANSFER_DST_BIT,&renderer->capture_buffer))return false;}
    renderer->capture_position=position;renderer->capture_pending=true;renderer->capture_ready=false;return true;
}
bool af_renderer_test_read_pixel(af_renderer *renderer,unsigned char rgba[4]){
    if(!rgba||!renderer->capture_ready||!af_renderer_wait_idle(renderer))return false;
    void *data=NULL;if(!vk_ok(vkMapMemory(renderer->device,renderer->capture_buffer.memory,0,VK_WHOLE_SIZE,0,&data),"map pixel"))return false;
    VkMappedMemoryRange range={.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,.memory=renderer->capture_buffer.memory,.size=VK_WHOLE_SIZE};
    bool success=vk_ok(vkInvalidateMappedMemoryRanges(renderer->device,1,&range),"invalidate pixel");
    if(success){const unsigned char *p=data;bool bgra=renderer->capture_format==VK_FORMAT_B8G8R8A8_SRGB||renderer->capture_format==VK_FORMAT_B8G8R8A8_UNORM;
        rgba[0]=p[bgra?2:0];rgba[1]=p[1];rgba[2]=p[bgra?0:2];rgba[3]=p[3];}
    vkUnmapMemory(renderer->device,renderer->capture_buffer.memory);renderer->capture_ready=false;return success;
}

bool af_renderer_texture_info(const af_renderer *renderer,const char *name,af_texture_info *out){
    if(!name||!out)return false;
    for(size_t i=0;i<renderer->texture_count;++i)if(!strcmp(renderer->textures[i].name,name)){*out=af_vk_texture_info(renderer->textures[i].texture);return true;}
    return false;
}

bool af_renderer_upload_texture(af_renderer *r,const char *name,const unsigned char *rgba,uint32_t w,uint32_t h){return upload_texture(r,name,rgba,w,h,false);}
bool af_renderer_upload_normal_map(af_renderer *r,const char *name,const unsigned char *rgba,uint32_t w,uint32_t h){return upload_texture(r,name,rgba,w,h,true);}

void af_renderer_set_interpolation(af_renderer *renderer,const af_scene *previous,float alpha){renderer->previous_scene=previous;renderer->interpolation_alpha=alpha;}

bool af_renderer_commit_asset(af_renderer *renderer,const char *staged,const char *target,bool mesh){
    if(!renderer||!staged||!target||!target[0]||strlen(target)>=AF_MESH_NAME_CAPACITY)return false;
    if(!mesh)for(size_t from=0;from<renderer->environment_count;++from)if(!strcmp(renderer->environments[from].name,staged)){
        if(!af_renderer_wait_idle(renderer))return false;size_t to=0;while(to<renderer->environment_count&&strcmp(renderer->environments[to].name,target))++to;
        if(to<renderer->environment_count){af_environment_asset swap=renderer->environments[to];renderer->environments[to]=renderer->environments[from];renderer->environments[from]=swap;snprintf(renderer->environments[from].name,AF_MESH_NAME_CAPACITY,"%s",staged);}
        else to=from;snprintf(renderer->environments[to].name,AF_MESH_NAME_CAPACITY,"%s",target);update_environment_descriptors(renderer);return true;
    }
    if(mesh){size_t from=find_mesh_index(renderer,staged),to=find_mesh_index(renderer,target);if(from==renderer->mesh_count)return false;++renderer->mesh_revision;
        if(to<renderer->mesh_count){af_gpu_mesh old=renderer->meshes[to].gpu;renderer->meshes[to].gpu=renderer->meshes[from].gpu;renderer->meshes[from].gpu=old;}
        else snprintf(renderer->meshes[from].name,sizeof(renderer->meshes[from].name),"%s",target);
    }else{size_t from=0,to=0;while(from<renderer->texture_count&&strcmp(renderer->textures[from].name,staged))++from;while(to<renderer->texture_count&&strcmp(renderer->textures[to].name,target))++to;
        if(from==renderer->texture_count)return false;if(to<renderer->texture_count){af_vk_texture *old=renderer->textures[to].texture;bool normal=renderer->textures[to].normal;renderer->textures[to].texture=renderer->textures[from].texture;renderer->textures[to].normal=renderer->textures[from].normal;renderer->textures[from].texture=old;renderer->textures[from].normal=normal;}
        else snprintf(renderer->textures[from].name,sizeof(renderer->textures[from].name),"%s",target);
    }return true;
}

bool af_renderer_upload_overlay_texture(af_renderer *renderer,uint32_t id,const unsigned char *rgba,uint32_t width,uint32_t height){
    return renderer&&id>=2&&rgba&&width&&height&&width<=8192&&height<=8192&&af_vk_ui_upload_texture(renderer->ui,renderer->graphics_queue,renderer->graphics_family,id,rgba,width,height);
}
bool af_renderer_remove_overlay_texture(af_renderer *renderer,uint32_t id){return renderer&&af_vk_ui_remove_texture(renderer->ui,id);}

bool af_renderer_upload_environment(af_renderer *renderer,const char *name,const float *rgba,uint32_t width,uint32_t height){
    if(!renderer||!name||!name[0]||strlen(name)>=AF_MESH_NAME_CAPACITY)return false;
    size_t index=0;while(index<renderer->environment_count&&strcmp(renderer->environments[index].name,name))++index;
    if(index==16)return false;af_ibl_images images;if(!af_ibl_bake_hdr(rgba,width,height,&images))return false;
    bool result=af_renderer_upload_environment_baked(renderer,name,&(af_hdr_file){(float*)rgba,width,height},&images);af_ibl_free(&images);return result;
}
bool af_renderer_upload_environment_baked(af_renderer *renderer,const char *name,const af_hdr_file *source,const af_ibl_images *baked){
    size_t index=0;while(index<renderer->environment_count&&strcmp(renderer->environments[index].name,name))++index;if(index==16)return false;
    af_ibl_images images=*baked;const float *rgba=source->rgba;uint32_t width=source->width,height=source->height;
    af_environment_asset asset={0};snprintf(asset.name,sizeof(asset.name),"%s",name);
    asset.specular=af_vk_texture_create_hdr(renderer->physical_device,renderer->device,renderer->graphics_queue,renderer->graphics_family,images.specular,AF_IBL_WIDTH,AF_IBL_HEIGHT,AF_IBL_LEVELS,true);
    asset.diffuse=af_vk_texture_create_hdr(renderer->physical_device,renderer->device,renderer->graphics_queue,renderer->graphics_family,images.diffuse,AF_IBL_DIFFUSE_WIDTH,AF_IBL_DIFFUSE_HEIGHT,1,true);
    asset.sky=af_vk_texture_create_hdr(renderer->physical_device,renderer->device,renderer->graphics_queue,renderer->graphics_family,rgba,width,height,1,true);
    if(!asset.specular||!asset.diffuse||!asset.sky||!af_renderer_wait_idle(renderer)){af_vk_texture_destroy(asset.specular);af_vk_texture_destroy(asset.diffuse);af_vk_texture_destroy(asset.sky);return false;}
    if(index<renderer->environment_count){af_vk_texture_destroy(renderer->environments[index].specular);af_vk_texture_destroy(renderer->environments[index].diffuse);af_vk_texture_destroy(renderer->environments[index].sky);}else ++renderer->environment_count;
    renderer->environments[index]=asset;update_environment_descriptors(renderer);return true;
}

bool af_renderer_set_skin_palette(af_renderer *r,af_object_id object,const af_mat4 *matrices,size_t count){
    if(!r||!object||count>AF_ANIMATION_MAX_JOINTS||(count&&!matrices))return false;size_t index=0;while(index<r->skin_count&&r->skins[index].object!=object)++index;
    if(!count){if(index<r->skin_count){r->skins[index]=r->skins[--r->skin_count];++r->skin_revision;}return true;}
    for(size_t i=0;i<count;++i)for(unsigned j=0;j<16;++j)if(!isfinite(matrices[i].elements[j]))return false;
    if(index<r->skin_count&&r->skins[index].count==count&&!memcmp(r->skins[index].matrices,matrices,count*sizeof(*matrices)))return true;
    if(index==AF_SKIN_INSTANCES)return false;if(index==r->skin_count)++r->skin_count;r->skins[index].object=object;r->skins[index].count=count;memcpy(r->skins[index].matrices,matrices,count*sizeof(*matrices));++r->skin_revision;return true;
}
void af_renderer_clear_skin_palettes(af_renderer *r){if(r&&r->skin_count){r->skin_count=0;++r->skin_revision;}}

void af_renderer_set_object_worlds(af_renderer *r,const af_scene *scene,const af_mat4 *world,size_t count){if(r){r->posed_scene=scene;r->posed_worlds=world;r->posed_count=count;}}
