/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "renderer.h"
#include "extension_runtime.h"
#include "water_internal.h"
#include "water_particles_internal.h"
#include "log.h"
#include "vulkan_device_policy.h"
#include "ui_vulkan.h"
#include "texture_vulkan.h"
#include "shadow_vulkan.h"
#include "shadow_math.h"
#include "post_vulkan.h"
#include "refraction_vulkan.h"
#include "ray_query_vulkan.h"
#include "ibl.h"
#include "local_shadow.h"
#include <math.h>

#include <vulkan/vulkan.h>
#include <SDL3/SDL_vulkan.h>
#include <stdatomic.h>
#include <stddef.h>
#include "mesh_vert.h"
#include "mesh_frag.h"
#include "mesh_ray_frag.h"
#define af_fullscreen_vert af_sky_vertex
#include "fullscreen_vert.h"
#undef af_fullscreen_vert
#include "sky_frag.h"
#include <stdlib.h>
#include <string.h>

#define AF_FRAMES_IN_FLIGHT AF_UI_FRAMES_IN_FLIGHT
_Static_assert(AF_FRAMES_IN_FLIGHT==AF_SHADOW_FRAME_COUNT,"Independent shadow maps for each frame");
enum {AF_RESOURCE_AA_MASK=3u,AF_RESOURCE_BLOOM=4u,AF_RESOURCE_MOTION=8u,AF_RESOURCE_REFRACTION=16u,AF_RESOURCE_RAY_SHADOWS=32u,AF_RESOURCE_RAY_REFLECTIONS=64u,
      AF_RESOURCE_UPSCALER=128u,
      AF_RESOURCE_RAY_QUERY=AF_RESOURCE_RAY_SHADOWS|AF_RESOURCE_RAY_REFLECTIONS};
#define AF_PORTABILITY_SUBSET "VK_KHR_portability_subset"
#define AF_STARTUP_EXTENSION_CAPACITY (AF_EXTENSION_LIMIT * AF_VULKAN_STARTUP_MAX_EXTENSIONS)

typedef struct af_gpu_buffer {
    VkBuffer handle;
    VkDeviceMemory memory;
    VkDeviceSize allocation_bytes,capacity;void *mapped;bool coherent;
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
    af_vec4 water_absorption,water_scattering,water_bounds,water_axes,water_screen,water_viewport,wind_time;
    af_mat4 unjittered_view_projection,previous_view_projection;
} af_frame_uniform;
_Static_assert(sizeof(af_frame_uniform)==3344,"Shader frame layout");
typedef struct af_blend_draw {size_t index;float depth;} af_blend_draw;
static int blend_compare(const void *a,const void *b){const af_blend_draw *x=a,*y=b;if(x->depth!=y->depth)return x->depth>y->depth?-1:1;return (x->index>y->index)-(x->index<y->index);}
typedef struct af_shadow_draw {af_mat4 model;size_t mesh;uint32_t skin_base;af_vk_texture *texture;float alpha,cutoff;uint32_t alpha_mode;bool ray_caster;size_t object_index;} af_shadow_draw;
#define AF_SKIN_INSTANCES 128u
#define AF_SKIN_MATRICES (AF_SKIN_INSTANCES*AF_ANIMATION_MAX_JOINTS+1u)
typedef struct af_skin_instance {af_object_id object;af_mat4 matrices[AF_ANIMATION_MAX_JOINTS];size_t count;} af_skin_instance;
typedef struct af_motion_record {af_mat4 model;uint32_t info[4];} af_motion_record;
_Static_assert(sizeof(af_motion_record)==80,"std430 temporal record");
typedef struct af_motion_snapshot {af_object_id id;af_mat4 model;uint64_t signature;bool visible;} af_motion_snapshot;
typedef struct af_environment_asset {char name[AF_MESH_NAME_CAPACITY];af_vk_texture *specular,*diffuse,*sky;} af_environment_asset;
typedef struct af_texture_asset {char name[AF_MESH_NAME_CAPACITY];af_vk_texture *texture;bool normal;} af_texture_asset;

typedef struct af_frame {
    VkCommandBuffer command;
    VkSemaphore acquired;
    VkFence fence;
    af_gpu_buffer uniform,skin_buffer,motion_buffer;
    VkDescriptorSet descriptor;
    VkQueryPool timestamps;
    bool timestamp_submitted;
    uint64_t shadow_signatures[4],local_shadow_signatures[24];
} af_frame;

typedef struct af_depth_attachment {
    VkDeviceSize allocation_bytes;
    VkImage image;
    VkDeviceMemory memory;
    VkImageView view;
} af_depth_attachment;

typedef struct af_swapchain {
    VkSwapchainKHR handle;
    VkExtent2D extent, render_extent;
    void *upscaler_context;
    bool upscaler_created;
    uint32_t resource_config;
    VkSurfaceFormatKHR surface_format;
    af_vk_post *post;af_vk_refraction *refraction;
    VkRenderPass render_pass,water_pass;
    VkPipelineLayout pipeline_layout;
    VkPipeline pipeline,blend_pipeline,sky_pipeline,water_attenuation,water_scattering;
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
    uint32_t vertex_count,index_count,max_joint;bool skinned;
    af_vec3 minimum, maximum;
} af_gpu_mesh;

typedef struct af_stream_frame {af_gpu_mesh gpu;uint64_t revision;} af_stream_frame;
typedef struct af_mesh_asset {
    char name[AF_MESH_NAME_CAPACITY];
    af_gpu_mesh gpu;
    bool dynamic,retired;uint64_t revision;
    af_vertex *cpu_vertices;uint32_t *cpu_indices;uint32_t vertex_count;
    af_stream_frame streams[AF_FRAMES_IN_FLIGHT];
} af_mesh_asset;

struct af_renderer {
    const af_physics *water_physics;
    af_water water_configs[AF_SCENE_MAX_WATERS];af_transform water_transforms[AF_SCENE_MAX_WATERS];float water_times[AF_SCENE_MAX_WATERS],water_alphas[AF_SCENE_MAX_WATERS];
    SDL_Window *window;
    const af_scene *previous_scene;float interpolation_alpha;
    af_vk_ui *ui;
    af_vk_shadows *shadows,*local_shadows;
    af_vk_ray_query *rays;
    bool ray_supported,ray_failed;
    uint64_t ray_revision;
    af_ray_instance *ray_inputs;
    size_t ray_input_capacity,ray_raster_casters;
    uint32_t local_shadow_layers,local_shadow_resolution,local_shadow_lights,local_shadow_overflow;
    uint32_t shadow_resolution,shadow_layers;
    af_shadow_draw *shadow_draws;
    af_shadow_bounds *shadow_bounds;
    size_t shadow_capacity,shadow_count,shadow_draw_calls;
    const af_scene *posed_scene;const af_mat4 *posed_worlds;size_t posed_count;
    af_skin_instance skins[AF_SKIN_INSTANCES];size_t skin_count;uint64_t skin_revision;
    af_skin_instance previous_skins[AF_SKIN_INSTANCES];size_t previous_skin_count;
    af_motion_snapshot *motion_previous,*motion_next;size_t motion_count,motion_next_count,motion_previous_capacity,motion_next_capacity;
    af_motion_record *motion_records;size_t motion_records_capacity;
    const af_vulkan_upscaler_api *upscaler;
    void *upscaler_state;
    bool upscaler_enabled, upscaler_failed;
    uint32_t upscaler_quality;
    af_mat4 previous_view_projection;af_camera temporal_camera;af_rect temporal_viewport;
    const af_scene *temporal_scene;uint64_t temporal_signature,temporal_frame,temporal_resets;
    uint32_t temporal_objects,temporal_skins;bool temporal_valid,temporal_supported;af_vec2 temporal_jitter;
    uint64_t texture_revision,mesh_revision;uint32_t shadow_passes,shadow_cached_passes;
    af_gpu_buffer capture_buffer;
    af_vec2 capture_position;VkExtent2D capture_extent;
    VkFormat capture_format;
    bool capture_supported,capture_pending,capture_ready,capture_motion;
    af_vk_texture *white,*flat_normal;
    af_vk_texture *environment,*irradiance,*brdf;uint32_t environment_profile;
    af_environment_asset environments[16];size_t environment_count;char active_environment[AF_MESH_NAME_CAPACITY];
    af_texture_asset *textures;
    size_t texture_count,texture_capacity,missing_textures;
    VkDescriptorSetLayout frame_layout;
    VkDescriptorPool frame_pool;
    VkInstance instance;
    uint32_t instance_api_version;
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
    uint64_t water_upload_bytes,water_stream_uploads,mesh_idle_waits;double water_upload_ms;
    size_t mesh_count;
    size_t mesh_capacity;
    size_t draw_calls;
    size_t missing_meshes;
    af_debug_view debug_view;bool culling_disabled;size_t visible_objects,culled_objects;double gpu_budget_ms;uint64_t gpu_budget_bytes;
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

static void negotiate_startup(af_renderer *, af_extensions *, const char **, uint32_t *,
                              const af_vulkan_requirements *supported, af_vulkan_requirements *enabled);

static void reject_startup(af_extensions *extensions)
{
    for (size_t i = 0; i < extensions->count; ++i)
        if (extensions->modules[i].startup)
            extensions->modules[i].startup_status = AF_EXTENSION_STARTUP_UNAVAILABLE;
}

static bool create_instance(af_renderer *renderer, bool enable_validation, af_extensions *startup)
{
    Uint32 sdl_count = 0;
    const char *const *sdl_extensions = SDL_Vulkan_GetInstanceExtensions(&sdl_count);
    if (!sdl_extensions) {
        af_log("error", "SDL Vulkan extensions: %s", SDL_GetError());
        return false;
    }
    const char **extensions = calloc((size_t)sdl_count + 2 + AF_STARTUP_EXTENSION_CAPACITY,
                                      sizeof(*extensions));
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
    uint32_t loader_version=VK_API_VERSION_1_0;
    if(vkEnumerateInstanceVersion(&loader_version)==VK_SUCCESS && loader_version>=VK_API_VERSION_1_2)application.apiVersion=VK_API_VERSION_1_2;
    renderer->instance_api_version = application.apiVersion;
    uint32_t core_extension_count = extension_count;
    negotiate_startup(renderer, startup, extensions, &extension_count, NULL, NULL);
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
    if (result != VK_SUCCESS && extension_count > core_extension_count) {
        af_log("warning", "Vulkan instance rejected extension requirements. Retrying built-in rendering.");
        reject_startup(startup);
        info.enabledExtensionCount = core_extension_count;
        result = vkCreateInstance(&info, NULL, &renderer->instance);
    }
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

static void negotiate_startup(af_renderer *renderer, af_extensions *startup,
                              const char **names, uint32_t *count,
                              const af_vulkan_requirements *supported, af_vulkan_requirements *enabled)
{
    bool device_stage = supported != NULL;
    for (size_t i = 0; i < startup->count; ++i) {
        af_loaded_extension *module = &startup->modules[i];
        const af_vulkan_startup_api *api = module->startup;
        if (!api || module->startup_status == AF_EXTENSION_STARTUP_UNAVAILABLE) continue;
        af_vulkan_requirements requirements = {.struct_size = sizeof(requirements)};
        bool ok = device_stage
            ? (!api->device_requirements || api->device_requirements(module->state, renderer->instance,
                                                  renderer->physical_device, &requirements))
            : (!api->instance_requirements || api->instance_requirements(module->state, &requirements));
        uint32_t previous_count = *count;
        ok = ok && requirements.extension_count <= AF_VULKAN_STARTUP_MAX_EXTENSIONS &&
             (!requirements.buffer_device_address || (device_stage && supported->buffer_device_address)) &&
             (!requirements.storage_image_write_without_format ||
              (device_stage && supported->storage_image_write_without_format)) &&
             (!requirements.device_features ||
              (device_stage && !(requirements.device_features & ~supported->device_features)));
        for (uint32_t j = 0; ok && j < requirements.extension_count; ++j) {
            const char *name = requirements.extensions[j];
            if (!name || !*name || strlen(name) >= VK_MAX_EXTENSION_NAME_SIZE) {
                ok = false;
                break;
            }
            bool exists = false;
            for (uint32_t n = 0; n < *count; ++n)
                if (!strcmp(names[n], name)) { exists = true; break; }
            if (exists) continue;
            ok = device_stage ? device_extension_available(renderer->physical_device, name)
                              : instance_extension_available(name);
            if (ok) names[(*count)++] = name;
            else af_log("warning", "Extension %s requires unavailable Vulkan extension %s.", module->api->id, name);
        }
        if (!ok) {
            *count = previous_count;
            module->startup_status = AF_EXTENSION_STARTUP_UNAVAILABLE;
            af_log("warning", "Vulkan %s requirements unavailable for %s.",
                   device_stage ? "device" : "instance", module->api->id);
        } else if (device_stage) {
            module->startup_status = AF_EXTENSION_STARTUP_READY;
            enabled->buffer_device_address |= requirements.buffer_device_address;
            enabled->storage_image_write_without_format |= requirements.storage_image_write_without_format;
            enabled->device_features |= requirements.device_features;
        }
    }
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

static bool create_device(af_renderer *renderer, af_extensions *startup)
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
    const char *extensions[5 + AF_STARTUP_EXTENSION_CAPACITY] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    uint32_t extension_count=1;
    if(device_extension_available(renderer->physical_device,AF_PORTABILITY_SUBSET))extensions[extension_count++]=AF_PORTABILITY_SUBSET;
    VkPhysicalDeviceRayQueryFeaturesKHR ray={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
    VkPhysicalDeviceAccelerationStructureFeaturesKHR acceleration={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR,.pNext=&ray};
    VkPhysicalDeviceBufferDeviceAddressFeatures address={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES,.pNext=&acceleration};
    bool query_available=selected_properties.apiVersion>=VK_API_VERSION_1_2 &&
        device_extension_available(renderer->physical_device,VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME) &&
        device_extension_available(renderer->physical_device,VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME) &&
        device_extension_available(renderer->physical_device,VK_KHR_RAY_QUERY_EXTENSION_NAME);
    bool address_available = renderer->instance_api_version >= VK_API_VERSION_1_2 &&
                             selected_properties.apiVersion >= VK_API_VERSION_1_2;
    if (address_available) {
        address.pNext = query_available ? &acceleration : NULL;
        VkPhysicalDeviceFeatures2 features = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &address};
        vkGetPhysicalDeviceFeatures2(renderer->physical_device, &features);
    }
    bool address_supported = address.bufferDeviceAddress != 0;
    renderer->ray_supported=query_available && ray.rayQuery && acceleration.accelerationStructure && address.bufferDeviceAddress && !getenv("AFTERLIGHT_DISABLE_RAY_QUERY");
    if(renderer->ray_supported){extensions[extension_count++]=VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME;extensions[extension_count++]=VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME;extensions[extension_count++]=VK_KHR_RAY_QUERY_EXTENSION_NAME;
        // Enable the needed feature bits only, not all queried optional features.
        address=(VkPhysicalDeviceBufferDeviceAddressFeatures){.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES,.pNext=&acceleration,.bufferDeviceAddress=VK_TRUE};
        acceleration=(VkPhysicalDeviceAccelerationStructureFeaturesKHR){.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR,.pNext=&ray,.accelerationStructure=VK_TRUE};
        ray=(VkPhysicalDeviceRayQueryFeaturesKHR){.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR,.rayQuery=VK_TRUE};}
    af_log("info","Hardware ray queries: %s.",renderer->ray_supported?"available":"unavailable (raster fallback)");
    VkPhysicalDeviceFeatures supported;vkGetPhysicalDeviceFeatures(renderer->physical_device,&supported);
    VkPhysicalDeviceFeatures enabled={.samplerAnisotropy=supported.samplerAnisotropy,.independentBlend=supported.independentBlend};
    renderer->temporal_supported=supported.independentBlend!=0;if(!renderer->temporal_supported)af_log("info","TAA requires independent attachment blending; TAA scenes will use FXAA on this device.");
    renderer->anisotropy=supported.samplerAnisotropy?fminf(8,selected_properties.limits.maxSamplerAnisotropy):1;
    uint32_t core_extension_count = extension_count;
    af_vulkan_requirements supported_startup = {
        .buffer_device_address = address_supported,
        .storage_image_write_without_format = supported.shaderStorageImageWriteWithoutFormat != VK_FALSE,
        .device_features = supported.shaderStorageImageExtendedFormats ?
                           AF_VULKAN_DEVICE_STORAGE_IMAGE_EXTENDED_FORMATS : 0,
    };
    af_vulkan_requirements enabled_startup = {0};
    negotiate_startup(renderer, startup, extensions, &extension_count, &supported_startup, &enabled_startup);
    bool address_requested = enabled_startup.buffer_device_address;
    enabled.shaderStorageImageWriteWithoutFormat = enabled_startup.storage_image_write_without_format;
    enabled.shaderStorageImageExtendedFormats =
        (enabled_startup.device_features & AF_VULKAN_DEVICE_STORAGE_IMAGE_EXTENDED_FORMATS) != 0;
    if (address_requested && !renderer->ray_supported)
        address = (VkPhysicalDeviceBufferDeviceAddressFeatures){
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES, .bufferDeviceAddress = VK_TRUE};
    VkDeviceCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = queue_count, .pQueueCreateInfos = queues,
        .pNext=(renderer->ray_supported || address_requested)?&address:NULL,
        .pEnabledFeatures=&enabled,
        .enabledExtensionCount = extension_count, .ppEnabledExtensionNames = extensions
    };
    VkResult result = vkCreateDevice(renderer->physical_device, &info, NULL, &renderer->device);
    if (result != VK_SUCCESS && (extension_count > core_extension_count || address_requested ||
                                  enabled.shaderStorageImageWriteWithoutFormat ||
                                  enabled.shaderStorageImageExtendedFormats)) {
        af_log("warning", "Vulkan device rejected extension requirements. Retrying built-in rendering.");
        reject_startup(startup);
        info.enabledExtensionCount = core_extension_count;
        info.pNext = renderer->ray_supported ? &address : NULL;
        enabled.shaderStorageImageWriteWithoutFormat = VK_FALSE;
        enabled.shaderStorageImageExtendedFormats = VK_FALSE;
        result = vkCreateDevice(renderer->physical_device, &info, NULL, &renderer->device);
    }
    if (!vk_ok(result, "vkCreateDevice"))
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
    if((swapchain->resource_config&AF_RESOURCE_RAY_QUERY)&&getenv("AFTERLIGHT_TEST_RAY_PIPELINE_FAIL"))return false;
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
    if(swapchain->resource_config&AF_RESOURCE_RAY_QUERY){shader_info.pCode=af_mesh_ray_frag;shader_info.codeSize=sizeof(af_mesh_ray_frag);}
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
    VkPipelineColorBlendAttachmentState motion_attachment[2]={attachment,attachment};
    VkPipelineColorBlendStateCreateInfo blending = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 2, .pAttachments = motion_attachment
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
    if(success){depth.depthWriteEnable=VK_FALSE;attachment.blendEnable=VK_TRUE;attachment.srcColorBlendFactor=VK_BLEND_FACTOR_SRC_ALPHA;attachment.dstColorBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;attachment.colorBlendOp=VK_BLEND_OP_ADD;attachment.srcAlphaBlendFactor=VK_BLEND_FACTOR_ONE;attachment.dstAlphaBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;attachment.alphaBlendOp=VK_BLEND_OP_ADD;motion_attachment[0]=attachment;if(!renderer->temporal_supported)motion_attachment[1]=attachment;success=vk_ok(vkCreateGraphicsPipelines(renderer->device,VK_NULL_HANDLE,1,&pipeline_info,NULL,&swapchain->blend_pipeline),"blend pipeline");}
    /* Prefer opaque walls/floors at coincident liquid boundaries. Both water
       passes need identical depth bias to avoid alternating attenuation/scatter. */
    raster.depthBiasEnable=VK_TRUE;raster.depthBiasConstantFactor=4;raster.depthBiasSlopeFactor=1;
    if(success){attachment.srcColorBlendFactor=VK_BLEND_FACTOR_ZERO;attachment.dstColorBlendFactor=VK_BLEND_FACTOR_SRC_COLOR;motion_attachment[0]=attachment;if(!renderer->temporal_supported)motion_attachment[1]=attachment;success=vk_ok(vkCreateGraphicsPipelines(renderer->device,VK_NULL_HANDLE,1,&pipeline_info,NULL,&swapchain->water_attenuation),"water attenuation pipeline");}
    if(success){attachment.srcColorBlendFactor=VK_BLEND_FACTOR_ONE;attachment.dstColorBlendFactor=VK_BLEND_FACTOR_ONE;motion_attachment[0]=attachment;if(!renderer->temporal_supported)motion_attachment[1]=attachment;success=vk_ok(vkCreateGraphicsPipelines(renderer->device,VK_NULL_HANDLE,1,&pipeline_info,NULL,&swapchain->water_scattering),"water scattering pipeline");}

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
    VkPipelineColorBlendAttachmentState attachment[2]={{.colorWriteMask=15},{.colorWriteMask=15}};VkPipelineColorBlendStateCreateInfo blend={.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,.attachmentCount=2,.pAttachments=attachment};
    VkPipelineDepthStencilStateCreateInfo depth={.sType=VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    VkDynamicState states[]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};VkPipelineDynamicStateCreateInfo dynamic={.sType=VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,.dynamicStateCount=2,.pDynamicStates=states};
    VkGraphicsPipelineCreateInfo info={.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,.stageCount=2,.pStages=stages,.pVertexInputState=&input,.pInputAssemblyState=&assembly,.pViewportState=&viewport,.pRasterizationState=&raster,.pMultisampleState=&samples,.pColorBlendState=&blend,.pDepthStencilState=&depth,.pDynamicState=&dynamic,.layout=r->swapchain.pipeline_layout,.renderPass=r->swapchain.render_pass,.basePipelineIndex=-1};
    success=vk_ok(vkCreateGraphicsPipelines(r->device,VK_NULL_HANDLE,1,&info,NULL,&r->swapchain.sky_pipeline),"sky pipeline");
 done:if(vertex)vkDestroyShaderModule(r->device,vertex,NULL);if(fragment)vkDestroyShaderModule(r->device,fragment,NULL);return success;
}
static void destroy_buffer(af_renderer *renderer, af_gpu_buffer *buffer)
{
    if(buffer->mapped)vkUnmapMemory(renderer->device,buffer->memory);
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
    bool ray_input=renderer->ray_supported && (usage&(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT|VK_BUFFER_USAGE_INDEX_BUFFER_BIT));
    if(ray_input)usage|=VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT|VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
    VkBufferCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = size,
        .usage = usage, .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };
    if (!vk_ok(vkCreateBuffer(renderer->device, &info, NULL, &buffer->handle), "vkCreateBuffer")) return false;
    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(renderer->device, buffer->handle, &requirements);buffer->allocation_bytes=requirements.size;
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
    VkMemoryAllocateFlagsInfo ray_flags={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,.flags=VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT};
    VkMemoryAllocateInfo allocation = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.pNext=ray_input?&ray_flags:NULL,
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
        if (!renderer->meshes[i].retired&&strcmp(renderer->meshes[i].name, name) == 0) return i;
    return renderer->mesh_count;
}

#include "mesh_stream_vulkan.h"

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
    uploaded.vertex_count=mesh->vertex_count;uploaded.index_count = mesh->index_count;
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
    ++renderer->mesh_idle_waits;
    if (!af_renderer_wait_idle(renderer)) goto fail;
    if (index < renderer->mesh_count) stream_destroy(renderer, &renderer->meshes[index]);
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
    if(index==renderer->mesh_count)return false;
    if(renderer->meshes[index].dynamic){renderer->meshes[index].retired=true;free(renderer->meshes[index].cpu_vertices);free(renderer->meshes[index].cpu_indices);renderer->meshes[index].cpu_vertices=NULL;renderer->meshes[index].cpu_indices=NULL;return true;}
    ++renderer->mesh_idle_waits;if(!af_renderer_wait_idle(renderer))return false;
    destroy_mesh(renderer, &renderer->meshes[index].gpu);
    memmove(&renderer->meshes[index], &renderer->meshes[index+1],
        (renderer->mesh_count-index-1) * sizeof(*renderer->meshes));
    --renderer->mesh_count;++renderer->mesh_revision;
    return true;
}

static void water_name(af_object_id id,char name[AF_MESH_NAME_CAPACITY]){snprintf(name,AF_MESH_NAME_CAPACITY,"__water_%llu",(unsigned long long)id);}
bool af_renderer_update_water(af_renderer *r,const af_physics *physics,const af_scene *s,float alpha){
    r->water_physics=physics;
    const af_water_runtime *runtime=ph_water_runtime(physics);
    for(size_t i=r->mesh_count;i>0;--i){const char *name=r->meshes[i-1].name;if(strncmp(name,"__water_",8))continue;bool keep=false;for(size_t k=0;k<af_scene_water_count(s);++k){char expected[64];water_name(af_scene_water_at(s,k)->owner,expected);if(!strcmp(name,expected))keep=true;}if(!r->meshes[i-1].retired&&!keep&&!af_renderer_remove_mesh(r,name))return false;}
    for(size_t i=0;i<af_scene_water_count(s);++i){af_water w=*af_scene_water_at(s,i);const af_scene_object *o=af_scene_get_object(s,w.owner);if(!o)return false;float time=0;bool live=false;
        for(size_t k=0;runtime&&k<runtime->count;++k)if(runtime->slots[k].config.owner==w.owner&&!memcmp(&runtime->slots[k].config,&w,sizeof(w))&&!memcmp(&runtime->slots[k].transform,&o->transform,sizeof(o->transform))){time=runtime->slots[k].time;live=true;if(runtime->slots[k].sleeping||(runtime->slots[k].particles&&runtime->slots[k].particles->sleeping))alpha=1;break;}
        float a=live?alpha:1;char name[64];water_name(w.owner,name);
        if(find_mesh_index(r,name)<r->mesh_count&&!memcmp(&r->water_configs[i],&w,sizeof(w))&&!memcmp(&r->water_transforms[i],&o->transform,sizeof(o->transform))&&r->water_times[i]==time&&r->water_alphas[i]==a)continue;
        af_vertex *v=NULL;uint32_t *ix=NULL;af_mesh_data mesh;bool ok=wa_mesh(live?runtime:NULL,s,w.owner,a,&v,&ix,&mesh)&&stream_set(r,name,&mesh);free(v);free(ix);if(!ok)return false;
        r->water_configs[i]=w;r->water_transforms[i]=o->transform;r->water_times[i]=time;r->water_alphas[i]=a;
    }return true;
}
static VkFormat select_depth_format(af_renderer *renderer)
{
    const VkFormat formats[] = {VK_FORMAT_D32_SFLOAT,VK_FORMAT_D16_UNORM, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT_S8_UINT};
    for (size_t i = 0; i < sizeof(formats)/sizeof(formats[0]); ++i) {
        VkFormatProperties properties;
        vkGetPhysicalDeviceFormatProperties(renderer->physical_device, formats[i], &properties);
        if ((properties.optimalTilingFeatures &(VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT|VK_FORMAT_FEATURE_TRANSFER_SRC_BIT|VK_FORMAT_FEATURE_TRANSFER_DST_BIT))==(VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT|VK_FORMAT_FEATURE_TRANSFER_SRC_BIT|VK_FORMAT_FEATURE_TRANSFER_DST_BIT)) return formats[i];
    }
    return VK_FORMAT_UNDEFINED;
}

static bool create_depth_attachment(af_renderer *renderer, af_depth_attachment *depth)
{
    af_swapchain *swapchain = &renderer->swapchain;
    VkImageCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D,
        .format = swapchain->depth_format,
        .extent = {swapchain->render_extent.width, swapchain->render_extent.height, 1},
        .mipLevels = 1, .arrayLayers = 1, .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL, .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
    };
    if (swapchain->resource_config & AF_RESOURCE_UPSCALER)
        info.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if (!vk_ok(vkCreateImage(renderer->device, &info, NULL, &depth->image), "vkCreateImage (depth)")) return false;
    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(renderer->device, depth->image, &requirements);depth->allocation_bytes=requirements.size;
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

static void destroy_upscaler_context(af_renderer *renderer)
{
    af_swapchain *swapchain = &renderer->swapchain;
    if (swapchain->upscaler_created) {
        renderer->upscaler->destroy(swapchain->upscaler_context);
        swapchain->upscaler_created = false;
        swapchain->upscaler_context = NULL;
    }
}

static bool upscaler_has_qualities(const af_vulkan_upscaler_api *api)
{
    return api && api->struct_size >= offsetof(af_vulkan_upscaler_api, quality_scales) +
                                      sizeof(api->quality_scales) &&
           api->quality_count && api->quality_names && api->quality_scales;
}

static uint32_t upscaler_quality_count(const af_vulkan_upscaler_api *api)
{
    return upscaler_has_qualities(api) ? api->quality_count : 0;
}

static float upscaler_render_scale(const af_renderer *renderer)
{
    const af_vulkan_upscaler_api *api = renderer->upscaler;
    if (!api) return 1.f;
    if (upscaler_has_qualities(api) && renderer->upscaler_quality < api->quality_count)
        return api->quality_scales[renderer->upscaler_quality];
    return api->render_scale;
}

void af_renderer_set_upscaler(af_renderer *renderer, const af_vulkan_upscaler_api *api, void *state)
{
    if (renderer->upscaler == api && renderer->upscaler_state == state) {
        (void)af_renderer_set_upscaler_enabled(renderer, true);
        return;
    }
    af_renderer_release_upscaler(renderer);
    if (renderer->swapchain.resource_config & AF_RESOURCE_UPSCALER)
        renderer->swapchain_invalid = renderer->resize_pending = true;
    renderer->upscaler = api;
    renderer->upscaler_state = state;
    renderer->upscaler_enabled = true;
    renderer->upscaler_failed = false;
    renderer->upscaler_quality = 0;
}

bool af_renderer_set_upscaler_enabled(af_renderer *renderer, bool enabled)
{
    if (!renderer || !renderer->upscaler) return false;
    if (renderer->upscaler_enabled != enabled) {
        renderer->upscaler_enabled = enabled;
        renderer->upscaler_failed = false;
    }
    return true;
}

bool af_renderer_upscaler_active(const af_renderer *renderer)
{
    return renderer && renderer->upscaler_enabled && !renderer->upscaler_failed &&
           renderer->swapchain.upscaler_created;
}

uint32_t af_renderer_upscaler_quality_count(const af_renderer *renderer)
{
    return renderer ? upscaler_quality_count(renderer->upscaler) : 0;
}

const char *af_renderer_upscaler_quality_name(const af_renderer *renderer, uint32_t index)
{
    if (!renderer || !upscaler_has_qualities(renderer->upscaler) ||
        index >= renderer->upscaler->quality_count)
        return NULL;
    return renderer->upscaler->quality_names[index];
}

uint32_t af_renderer_upscaler_quality(const af_renderer *renderer)
{
    return renderer ? renderer->upscaler_quality : 0;
}

bool af_renderer_set_upscaler_quality(af_renderer *renderer, uint32_t index)
{
    if (!renderer || !renderer->upscaler ||
        index >= upscaler_quality_count(renderer->upscaler))
        return false;
    if (renderer->upscaler_quality != index) {
        renderer->upscaler_quality = index;
        renderer->swapchain_invalid = renderer->resize_pending = true;
        renderer->temporal_valid = false;
    }
    return true;
}

void af_renderer_release_upscaler(af_renderer *renderer)
{
    if (!renderer || !renderer->swapchain.upscaler_created) return;
    (void)af_renderer_wait_idle(renderer);
    destroy_upscaler_context(renderer);
}

static void destroy_swapchain(af_renderer *renderer)
{
    af_swapchain *swapchain = &renderer->swapchain;
    destroy_upscaler_context(renderer);
    af_vk_ui_release_pipeline(renderer->ui);
    af_vk_post_destroy(swapchain->post);af_vk_refraction_destroy(swapchain->refraction);
    if(swapchain->water_pass)vkDestroyRenderPass(renderer->device,swapchain->water_pass,NULL);
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
    if(swapchain->water_attenuation)vkDestroyPipeline(renderer->device,swapchain->water_attenuation,NULL);
    if(swapchain->water_scattering)vkDestroyPipeline(renderer->device,swapchain->water_scattering,NULL);
    if(swapchain->blend_pipeline)vkDestroyPipeline(renderer->device,swapchain->blend_pipeline,NULL);
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

static bool create_swapchain(af_renderer *renderer,af_render_settings settings,uint32_t config)
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
        existing->resource_config==config && existing->surface_format.format == format.format && existing->surface_format.colorSpace == format.colorSpace) {
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
    swapchain->extent = extent;swapchain->resource_config=config;
    bool upscale = (config & AF_RESOURCE_UPSCALER) != 0;
    float scale = upscale ? upscaler_render_scale(renderer) : 1.f;
    swapchain->render_extent = (VkExtent2D){
        (uint32_t)fmaxf(1, (float)extent.width * scale),
        (uint32_t)fmaxf(1, (float)extent.height * scale),
    };
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
    VkAttachmentDescription attachments[3] = {
        {.format = VK_FORMAT_R16G16B16A16_SFLOAT, .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED, .finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {.format = swapchain->depth_format, .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED, .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL}
    };
    attachments[2]=attachments[0];
    VkAttachmentReference color_reference[] = {{.attachment=0,.layout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},{.attachment=(config&AF_RESOURCE_MOTION)?2u:VK_ATTACHMENT_UNUSED,.layout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}};
    VkAttachmentReference depth_reference = {.attachment = 1, .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 2, .pColorAttachments = color_reference,
        .pDepthStencilAttachment = &depth_reference
    };
    VkPipelineStageFlags attachment_stages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    VkSubpassDependency dependencies[] = {
        {.srcSubpass=VK_SUBPASS_EXTERNAL,.dstSubpass=0,
         .srcStageMask=VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|attachment_stages|VK_PIPELINE_STAGE_TRANSFER_BIT,.dstStageMask=attachment_stages,
         .srcAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT|VK_ACCESS_TRANSFER_READ_BIT,
         .dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT},
        {.srcSubpass=0,.dstSubpass=VK_SUBPASS_EXTERNAL,.srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,.dstStageMask=VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
         .srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,.dstAccessMask=VK_ACCESS_SHADER_READ_BIT}
    };
    if (upscale) dependencies[0].srcStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkRenderPassCreateInfo pass_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = (config&AF_RESOURCE_MOTION)?3u:2u, .pAttachments = attachments,
        .subpassCount = 1, .pSubpasses = &subpass,
        .dependencyCount = 2, .pDependencies = dependencies
    };
    if (!vk_ok(vkCreateRenderPass(renderer->device, &pass_info, NULL, &swapchain->render_pass), "vkCreateRenderPass"))
        return false;
    attachments[2].initialLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;attachments[2].loadOp=attachments[0].loadOp=attachments[1].loadOp=VK_ATTACHMENT_LOAD_OP_LOAD;attachments[0].initialLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;attachments[1].initialLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    if(!vk_ok(vkCreateRenderPass(renderer->device,&pass_info,NULL,&swapchain->water_pass),"water load render pass"))return false;
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
    swapchain->post=af_vk_post_create(renderer->physical_device,renderer->device,renderer->graphics_queue,renderer->graphics_family,extent,swapchain->render_extent,swapchain->image_count,swapchain->views,format.format,swapchain->render_pass,depth_views,settings,(config&AF_RESOURCE_MOTION)!=0,upscale);
    free(depth_views);swapchain->refraction=af_vk_refraction_create(renderer->physical_device,renderer->device,(config&AF_RESOURCE_REFRACTION)?swapchain->render_extent:(VkExtent2D){1,1},swapchain->image_count,swapchain->depth_format);if(!swapchain->post||!swapchain->refraction||!create_pipeline(renderer)||!create_sky_pipeline(renderer))return false;
    if (renderer->ui && !af_vk_ui_create_pipeline(renderer->ui, af_vk_post_output_pass(swapchain->post))) return false;
    if (upscale) {
        const af_vulkan_upscaler_config upscaler_config = {
            .struct_size = sizeof(af_vulkan_upscaler_config),
            .instance = renderer->instance, .physical_device = renderer->physical_device,
            .device = renderer->device, .graphics_queue_family = renderer->graphics_family,
            .frames_in_flight = AF_FRAMES_IN_FLIGHT, .render_extent = swapchain->render_extent,
            .output_extent = extent, .depth_format = swapchain->depth_format,
            .color_format = VK_FORMAT_R16G16B16A16_SFLOAT,
            .motion_format = VK_FORMAT_R16G16B16A16_SFLOAT,
            .output_format = VK_FORMAT_R16G16B16A16_SFLOAT,
            .quality = renderer->upscaler_quality,
        };
        if (!renderer->upscaler->create(renderer->upscaler_state, &upscaler_config,
                                        &swapchain->upscaler_context)) return false;
        swapchain->upscaler_created = true;
    }
    renderer->resize_pending = false;
    renderer->temporal_valid=false;
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
    VkDescriptorSetLayoutBinding bindings[12]={{.binding=0,.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT}};
    for(uint32_t i=1;i<6;++i)bindings[i]=(VkDescriptorSetLayoutBinding){i,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,NULL};
    bindings[6]=(VkDescriptorSetLayoutBinding){6,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_VERTEX_BIT,NULL};
    bindings[7]=(VkDescriptorSetLayoutBinding){7,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,NULL};bindings[8]=(VkDescriptorSetLayoutBinding){8,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,NULL};
    bindings[9]=(VkDescriptorSetLayoutBinding){9,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_VERTEX_BIT,NULL};
    bindings[10]=(VkDescriptorSetLayoutBinding){10,VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,1,VK_SHADER_STAGE_FRAGMENT_BIT,NULL};
    bindings[11]=(VkDescriptorSetLayoutBinding){11,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_FRAGMENT_BIT,NULL};
    VkDescriptorSetLayoutCreateInfo layout={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=renderer->ray_supported?12u:10u,.pBindings=bindings};
    return vk_ok(vkCreateDescriptorSetLayout(renderer->device,&layout,NULL,&renderer->frame_layout),"frame layout");
}
static bool create_uniforms(af_renderer *renderer)
{
    VkDescriptorPoolSize sizes[]={{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,AF_FRAMES_IN_FLIGHT},{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,7*AF_FRAMES_IN_FLIGHT},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,(renderer->ray_supported?3u:2u)*AF_FRAMES_IN_FLIGHT},{VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,AF_FRAMES_IN_FLIGHT}};
    VkDescriptorPoolCreateInfo pool={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=AF_FRAMES_IN_FLIGHT,.poolSizeCount=renderer->ray_supported?4u:3u,.pPoolSizes=sizes};
    if(!vk_ok(vkCreateDescriptorPool(renderer->device,&pool,NULL,&renderer->frame_pool),"frame pool"))return false;
    for(unsigned i=0;i<AF_FRAMES_IN_FLIGHT;++i){af_frame *frame=&renderer->frames[i];af_frame_uniform initial={0};
        af_mat4 *bones=calloc(2*AF_SKIN_MATRICES,sizeof(*bones));if(!bones)return false;for(unsigned j=0;j<2*AF_SKIN_MATRICES;++j)bones[j]=af_mat4_identity();
        bool bone_ok=upload_buffer(renderer,bones,2*AF_SKIN_MATRICES*sizeof(*bones),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,&frame->skin_buffer);free(bones);if(!bone_ok)return false;
        if(!upload_buffer(renderer,&initial,sizeof(initial),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,&frame->uniform))return false;
        VkDescriptorSetAllocateInfo allocate={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=renderer->frame_pool,.descriptorSetCount=1,.pSetLayouts=&renderer->frame_layout};
        if(!vk_ok(vkAllocateDescriptorSets(renderer->device,&allocate,&frame->descriptor),"frame descriptor"))return false;
        VkDescriptorBufferInfo buffer={frame->uniform.handle,0,sizeof(initial)};
        VkWriteDescriptorSet write={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=frame->descriptor,.dstBinding=0,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,.pBufferInfo=&buffer};
        vkUpdateDescriptorSets(renderer->device,1,&write,0,NULL);
        VkDescriptorBufferInfo bone_buffer={frame->skin_buffer.handle,0,2*AF_SKIN_MATRICES*sizeof(af_mat4)};write.dstBinding=6;write.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;write.pBufferInfo=&bone_buffer;vkUpdateDescriptorSets(renderer->device,1,&write,0,NULL);
        af_motion_record initial_motion={0};if(!upload_buffer(renderer,&initial_motion,sizeof(initial_motion),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,&frame->motion_buffer))return false;frame->motion_buffer.capacity=sizeof(initial_motion);
        VkDescriptorBufferInfo motion_buffer={frame->motion_buffer.handle,0,VK_WHOLE_SIZE};write.dstBinding=9;write.pBufferInfo=&motion_buffer;vkUpdateDescriptorSets(renderer->device,1,&write,0,NULL);
    }update_environment_descriptors(renderer);return true;
}
af_renderer *af_renderer_create(SDL_Window *window, bool enable_validation, af_extensions *startup)
{
    af_renderer *renderer = calloc(1, sizeof(*renderer));
    if (!renderer) return NULL;
    atomic_init(&renderer->validation_errors, 0);
    renderer->window = window;renderer->gpu_budget_ms=1000.0/60;renderer->gpu_budget_bytes=UINT64_C(512)*1024*1024;
    renderer->resize_pending = true;
    if (!create_instance(renderer, enable_validation, startup)) goto fail;
    if (!SDL_Vulkan_CreateSurface(window, renderer->instance, NULL, &renderer->surface)) {
        af_log("error", "Vulkan surface creation: %s", SDL_GetError());
        goto fail;
    }
    if (!create_device(renderer, startup) || !create_frames(renderer)||!create_frame_layout(renderer)) goto fail;
    const unsigned char white[]={255,255,255,255};
    renderer->white=af_vk_texture_create(renderer->physical_device,renderer->device,renderer->graphics_queue,renderer->graphics_family,white,1,1,true,true,renderer->anisotropy);
    const unsigned char flat[]={128,128,255,255};
    renderer->flat_normal=af_vk_texture_create(renderer->physical_device,renderer->device,renderer->graphics_queue,renderer->graphics_family,flat,1,1,false,true,renderer->anisotropy);
    af_shadow_settings shadows=af_shadow_settings_default();renderer->shadow_resolution=shadows.resolution;renderer->shadow_layers=shadows.mode==AF_SHADOW_CASCADED?4u:1u;
    renderer->shadows=af_vk_shadows_create(renderer->physical_device,renderer->device,shadows.resolution,renderer->shadow_layers,renderer->frame_layout,af_vk_texture_layout(renderer->white));
    renderer->local_shadow_resolution=512;renderer->local_shadow_layers=1;
    renderer->local_shadows=af_vk_shadows_create(renderer->physical_device,renderer->device,512,1,renderer->frame_layout,af_vk_texture_layout(renderer->white));
    if(!renderer->white||!renderer->flat_normal||!renderer->shadows||!renderer->local_shadows||!create_environment(renderer,0)||!create_uniforms(renderer)||!create_swapchain(renderer,af_render_settings_default(),0))goto fail;
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
        af_vk_ray_query_destroy(renderer->rays);
        destroy_buffer(renderer,&renderer->capture_buffer);
        af_vk_ui_destroy(renderer->ui);
        af_vk_shadows_destroy(renderer->local_shadows);af_vk_shadows_destroy(renderer->shadows);free(renderer->shadow_draws);free(renderer->shadow_bounds);
        for(size_t i=0;i<renderer->texture_count;++i)af_vk_texture_destroy(renderer->textures[i].texture);
        free(renderer->textures);af_vk_texture_destroy(renderer->white);af_vk_texture_destroy(renderer->flat_normal);
        for(size_t i=0;i<renderer->environment_count;++i){af_vk_texture_destroy(renderer->environments[i].specular);af_vk_texture_destroy(renderer->environments[i].diffuse);af_vk_texture_destroy(renderer->environments[i].sky);}
        af_vk_texture_destroy(renderer->environment);af_vk_texture_destroy(renderer->irradiance);af_vk_texture_destroy(renderer->brdf);
        if(renderer->frame_pool)vkDestroyDescriptorPool(renderer->device,renderer->frame_pool,NULL);
        if(renderer->frame_layout)vkDestroyDescriptorSetLayout(renderer->device,renderer->frame_layout,NULL);
        for (size_t i = 0; i < renderer->mesh_count; ++i) stream_destroy(renderer, &renderer->meshes[i]);
        free(renderer->meshes);
        for (uint32_t i = 0; i < AF_FRAMES_IN_FLIGHT; ++i) {
            destroy_buffer(renderer,&renderer->frames[i].uniform);destroy_buffer(renderer,&renderer->frames[i].skin_buffer);destroy_buffer(renderer,&renderer->frames[i].motion_buffer);
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
    free(renderer->ray_inputs);
    free(renderer->motion_previous);free(renderer->motion_next);free(renderer->motion_records);
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
        const af_water *water=af_scene_get_water(scene,object->id);if(water&&!water->enabled)continue;
        if (!af_scene_object_visible(scene,object->id)||(!water&&!strcmp(object->mesh,"__node"))) continue;
        char name[64];if(water)water_name(object->id,name);
        size_t index = find_mesh_index(renderer,water?name:object->mesh);
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
        af_vk_shadows *replacement=af_vk_shadows_create(renderer->physical_device,renderer->device,settings.resolution,layers,renderer->frame_layout,af_vk_texture_layout(renderer->white));
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
        if(!object->material.cast_shadows||object->material.alpha_mode==AF_ALPHA_BLEND||!af_scene_object_visible(scene,object->id))continue;
        size_t mesh=find_mesh_index(renderer,object->mesh);af_mat4 model;
        if(mesh==renderer->mesh_count||!object_world(renderer,scene,i,&model))continue;
        size_t n=renderer->shadow_count++;
        af_vk_texture *texture=renderer->white;for(size_t t=0;t<renderer->texture_count;++t)if(!renderer->textures[t].normal&&!strcmp(renderer->textures[t].name,object->material.texture)){texture=renderer->textures[t].texture;break;}
        renderer->shadow_draws[n]=(af_shadow_draw){model,mesh,skin_base(renderer,object->id,&renderer->meshes[mesh].gpu),texture,object->material.base_color.a,object->material.alpha_cutoff,(uint32_t)object->material.alpha_mode,false,i};
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
    int logical_width=1,logical_height=1;SDL_GetWindowSize(renderer->window,&logical_width,&logical_height);uniform->water_screen=(af_vec4){(float)renderer->swapchain.render_extent.width,(float)renderer->swapchain.render_extent.height,render_settings.refraction_strength,render_settings.refraction?1.f:0};uniform->water_viewport=(af_vec4){area.x/(float)logical_width,area.y/(float)logical_height,(area.x+area.width)/(float)logical_width,(area.y+area.height)/(float)logical_height};
    double water_time=af_physics_time(renderer->water_physics);af_vec3 air=af_wind_velocity(af_scene_get_wind(scene),water_time);uniform->wind_time=(af_vec4){air.x,air.z,(float)water_time,0};
    af_water_sample underwater=af_physics_sample_water(renderer->water_physics,scene,camera->position);const af_water *water=af_scene_get_water(scene,underwater.owner);
    if(render_settings.underwater&&water&&underwater.inside&&camera->position.y>underwater.bottom&&camera->position.y<underwater.height+.1f){const af_scene_object *o=af_scene_get_object(scene,water->owner);float blend=fmaxf(0,fminf(1,(underwater.height+.1f-camera->position.y)/.2f));blend=blend*blend*(3-2*blend);float yaw=o->transform.rotation.y;af_vec3 origin=o->transform.position;af_vec2 size=water->half_size;
        if(water->mode==AF_WATER_RIVER){float c=cosf(yaw),s=sinf(yaw);af_vec3 d=af_vec3_sub(camera->position,origin),local={c*d.x-s*d.z,d.y,s*d.x+c*d.z},tangent;float side,along,height;if(wa_river_coordinates(water,local,&side,&along,&height,&tangent)){float length=wa_river_length(water);local=af_vec3_add(local,af_vec3_add((af_vec3){-side*tangent.z,0,side*tangent.x},af_vec3_scale(tangent,length*.5f-along)));origin.x+=c*local.x+s*local.z;origin.z+=-s*local.x+c*local.z;yaw+=atan2f(tangent.x,tangent.z);size.y=length*.5f;}}
        af_vec3 illumination=af_vec3_add((af_vec3){light.ambient,light.ambient,light.ambient},af_vec3_scale(light.color,light.intensity*.12f));uniform->water_absorption=(af_vec4){water->absorption.x,water->absorption.y,water->absorption.z,blend};uniform->water_scattering=(af_vec4){water->scattering.x*illumination.x,water->scattering.y*illumination.y,water->scattering.z*illumination.z,underwater.height};uniform->water_bounds=(af_vec4){origin.x,origin.z,size.x,size.y};uniform->water_axes=(af_vec4){cosf(yaw),sinf(yaw),underwater.bottom,0};
    }
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
        af_vk_shadows *replacement=af_vk_shadows_create(renderer->physical_device,renderer->device,render_settings.local_shadow_resolution,wanted_layers,renderer->frame_layout,af_vk_texture_layout(renderer->white));if(!replacement)return false;
        af_vk_shadows_destroy(renderer->local_shadows);renderer->local_shadows=replacement;renderer->local_shadow_layers=wanted_layers;renderer->local_shadow_resolution=render_settings.local_shadow_resolution;
        update_environment_descriptors(renderer);
        for(unsigned f=0;f<AF_FRAMES_IN_FLIGHT;++f)memset(renderer->frames[f].local_shadow_signatures,0,sizeof(renderer->frames[f].local_shadow_signatures));
    }
    uniform->light_count.y=1.0f/(float)renderer->local_shadow_resolution;uniform->light_count.z=(float)local_layers;
    memcpy(uniform->shadow_matrices,view->matrices,sizeof(view->matrices));return true;
}
static uint64_t shadow_hash(uint64_t h,const void *data,size_t size){const unsigned char *p=data;for(size_t i=0;i<size;++i){h^=p[i];h*=UINT64_C(1099511628211);}return h;}
static bool prepare_ray_scene(af_renderer *r, af_frame *frame, const af_scene *scene,
                              bool shadows, bool reflections)
{
    if(r->rays && r->ray_revision!=r->mesh_revision){
        if(!af_renderer_wait_idle(r))return false;
        af_vk_ray_query_destroy(r->rays);
        r->rays=NULL;
    }
    if(!r->rays){
        r->rays=af_vk_ray_query_create(r->physical_device,r->device,AF_FRAMES_IN_FLIGHT,
                                      r->timestamp_bits,r->timestamp_period);
        r->ray_revision=r->mesh_revision;
        if(!r->rays)return false;
    }
    size_t objects=af_scene_object_count(scene);
    if(objects>r->ray_input_capacity){
        af_ray_instance *inputs=realloc(r->ray_inputs,objects*sizeof(*inputs));
        if(!inputs)return false;
        r->ray_inputs=inputs;
        r->ray_input_capacity=objects;
    }

    size_t count=0,shadow_index=0;
    r->ray_raster_casters=0;
    for(size_t i=0;i<objects;++i){
        af_shadow_draw *caster=NULL;
        if(shadow_index<r->shadow_count && r->shadow_draws[shadow_index].object_index==i)
            caster=&r->shadow_draws[shadow_index++];
        if(!reflections && !caster)continue;

        const af_scene_object *object=af_scene_object_at(scene,i);
        const af_material *material=&object->material;
        uint8_t mask=(shadows && caster?AF_RAY_SHADOW_MASK:0) |
                     (reflections?AF_RAY_REFLECTION_MASK:0);
        if(!mask || material->alpha_mode!=AF_ALPHA_OPAQUE ||
           af_scene_get_water(scene,object->id) ||
           (!caster && !af_scene_object_visible(scene,object->id)))continue;
        size_t mesh=caster?caster->mesh:find_mesh_index(r,object->mesh);
        if(mesh==r->mesh_count)continue;
        const af_mesh_asset *asset=&r->meshes[mesh];
        if(asset->dynamic || asset->gpu.skinned)continue;
        af_mat4 world,inverse;
        if(caster)world=caster->model;
        else if(!object_world(r,scene,i,&world))continue;
        if(!af_mat4_inverse(world,&inverse))continue;
        if(caster && shadows)caster->ray_caster=true;

        r->ray_inputs[count++]=(af_ray_instance){
            .mesh_key=mesh+1,.vertices=asset->gpu.vertices.handle,
            .indices=asset->gpu.indices.handle,.vertex_count=asset->gpu.vertex_count,
            .index_count=asset->gpu.index_count,.world=world,.mask=mask,
            .color=material->base_color,.emission_unlit={
                material->emission.x*material->emission_strength,
                material->emission.y*material->emission_strength,
                material->emission.z*material->emission_strength,material->unlit?1.f:0.f}};
    }
    if(shadows)for(size_t i=0;i<r->shadow_count;++i)
        if(!r->shadow_draws[i].ray_caster)++r->ray_raster_casters;
    return af_vk_ray_query_prepare(r->rays,r->frame_index,r->ray_inputs,count,
                                   reflections,frame->descriptor);
}
static uint64_t shadow_signature(af_renderer *r,af_mat4 matrix,af_shadow_settings settings,bool enabled){
    uint64_t h=shadow_hash(UINT64_C(14695981039346656037),&matrix,sizeof(matrix));
    h=shadow_hash(h,&settings.constant_bias,sizeof(float));h=shadow_hash(h,&settings.slope_bias,sizeof(float));h=shadow_hash(h,&r->mesh_revision,sizeof(r->mesh_revision));h=shadow_hash(h,&r->texture_revision,sizeof(r->texture_revision));h=shadow_hash(h,&enabled,sizeof(enabled));h=shadow_hash(h,&r->skin_revision,sizeof(r->skin_revision));
    if(enabled)for(size_t i=0;i<r->shadow_count;++i){h=shadow_hash(h,&r->shadow_draws[i].model,sizeof(af_mat4));h=shadow_hash(h,&r->shadow_draws[i].mesh,sizeof(size_t));h=shadow_hash(h,&r->shadow_draws[i].alpha,sizeof(float));h=shadow_hash(h,&r->shadow_draws[i].cutoff,sizeof(float));h=shadow_hash(h,&r->shadow_draws[i].alpha_mode,sizeof(uint32_t));h=shadow_hash(h,&r->shadow_draws[i].ray_caster,sizeof(bool));h=shadow_hash(h,&r->shadow_draws[i].texture,sizeof(void*));}
    return h?h:1;
}
/* Previous rendered transforms are independent of fixed-step interpolation. */
af_temporal_test_stats af_renderer_test_temporal(const af_renderer *r) {
  return r ? (af_temporal_test_stats){r->temporal_frame,
                                      r->temporal_resets,
                                      r->swapchain.extent.width,
                                      r->swapchain.extent.height,
                                      r->temporal_objects,
                                      r->temporal_skins,
                                      r->temporal_supported,
                                      r->temporal_valid,
                                      r->temporal_jitter}
           : (af_temporal_test_stats){0};
}
void af_renderer_reset_temporal(af_renderer *r) {
  if (r)
    r->temporal_valid = false;
}
static int motion_compare(const void *a, const void *b) {
  af_object_id x = ((const af_motion_snapshot *)a)->id,
               y = ((const af_motion_snapshot *)b)->id;
  return (x > y) - (x < y);
}
static float halton(unsigned n, unsigned base) {
  float value = 0, scale = 1;
  while (n) {
    scale /= (float)base;
    value += scale * (float)(n % base);
    n /= base;
  }
  return value;
}
static uint64_t temporal_settings_hash(uint64_t h, af_render_settings value) {
  h=shadow_hash(h,&value.ray_traced_shadows,sizeof(value.ray_traced_shadows));
  h=shadow_hash(h,&value.ray_traced_water_reflections,sizeof(value.ray_traced_water_reflections));
  h = shadow_hash(h, &value.anti_aliasing, sizeof(value.anti_aliasing));
  h = shadow_hash(h, &value.aa_sharpness, sizeof(value.aa_sharpness));
  h = shadow_hash(h, &value.taa_history_weight,
                  sizeof(value.taa_history_weight));
  h = shadow_hash(h, &value.exposure, sizeof(value.exposure));
  h = shadow_hash(h, &value.tone_mapping, sizeof(value.tone_mapping));
  h = shadow_hash(h, &value.underwater, sizeof(value.underwater));
  h = shadow_hash(h, &value.refraction, sizeof(value.refraction));
  h = shadow_hash(h, &value.refraction_strength,
                  sizeof(value.refraction_strength));
  h = shadow_hash(h, &value.bloom, sizeof(value.bloom));
  h = shadow_hash(h, &value.bloom_strength, sizeof(value.bloom_strength));
  h = shadow_hash(h, &value.bloom_threshold, sizeof(value.bloom_threshold));
  h = shadow_hash(h, &value.bloom_radius, sizeof(value.bloom_radius));
  h = shadow_hash(h, &value.local_shadow_resolution,
                  sizeof(value.local_shadow_resolution));
  h = shadow_hash(h, &value.environment_enabled,
                  sizeof(value.environment_enabled));
  h = shadow_hash(h, &value.environment_profile,
                  sizeof(value.environment_profile));
  h = shadow_hash(h, &value.skybox, sizeof(value.skybox));
  h = shadow_hash(h, &value.skybox_intensity, sizeof(value.skybox_intensity));
  h = shadow_hash(h, &value.environment_intensity,
                  sizeof(value.environment_intensity));
  h = shadow_hash(h, &value.environment_rotation,
                  sizeof(value.environment_rotation));
  h = shadow_hash(h, &value.motion_blur, sizeof(value.motion_blur));
  h = shadow_hash(h, &value.motion_blur_strength, sizeof(value.motion_blur_strength));
  h = shadow_hash(h, value.environment_asset, strlen(value.environment_asset));
  return h;
}
static uint64_t temporal_material_hash(uint64_t h, af_material value) {
  h = shadow_hash(h, &value.alpha_mode, sizeof(value.alpha_mode));
  h = shadow_hash(h, &value.alpha_cutoff, sizeof(value.alpha_cutoff));
  h = shadow_hash(h, &value.base_color, sizeof(value.base_color));
  h = shadow_hash(h, &value.unlit, sizeof(value.unlit));
  h = shadow_hash(h, &value.cast_shadows, sizeof(value.cast_shadows));
  h = shadow_hash(h, &value.receive_shadows, sizeof(value.receive_shadows));
  h = shadow_hash(h, &value.physically_based, sizeof(value.physically_based));
  h = shadow_hash(h, &value.roughness, sizeof(value.roughness));
  h = shadow_hash(h, &value.metallic, sizeof(value.metallic));
  h = shadow_hash(h, &value.emission, sizeof(value.emission));
  h = shadow_hash(h, &value.emission_strength, sizeof(value.emission_strength));
  h = shadow_hash(h, &value.normal_strength, sizeof(value.normal_strength));
  h = shadow_hash(h, value.texture, strlen(value.texture));
  h = shadow_hash(h, value.normal_map, strlen(value.normal_map));
  h = shadow_hash(h, value.metallic_roughness_map,
                  strlen(value.metallic_roughness_map));
  return h;
}
static uint64_t temporal_light_hash(uint64_t h, af_local_light value) {
  h = shadow_hash(h, &value.type, sizeof(value.type));
  h = shadow_hash(h, &value.enabled, sizeof(value.enabled));
  h = shadow_hash(h, &value.position, sizeof(value.position));
  h = shadow_hash(h, &value.color, sizeof(value.color));
  h = shadow_hash(h, &value.intensity, sizeof(value.intensity));
  h = shadow_hash(h, &value.range, sizeof(value.range));
  h = shadow_hash(h, &value.direction, sizeof(value.direction));
  h = shadow_hash(h, &value.cast_shadows, sizeof(value.cast_shadows));
  h = shadow_hash(h, &value.shadow_bias, sizeof(value.shadow_bias));
  h = shadow_hash(h, &value.inner_angle, sizeof(value.inner_angle));
  h = shadow_hash(h, &value.outer_angle, sizeof(value.outer_angle));
  return h;
}
static bool prepare_temporal(af_renderer *r, af_frame *frame,
                             const af_scene *scene, const af_camera *camera,
                             af_rect area, af_color clear,
                             af_render_settings settings, bool temporal, bool jitter,
                             af_frame_uniform *uniform) {
  uniform->unjittered_view_projection = uniform->view_projection;
  uniform->previous_view_projection = uniform->view_projection;
  r->temporal_jitter = (af_vec2){0};
  if (!temporal) {
    r->temporal_valid = false;
    return true;
  }
  uint64_t signature =
      temporal_settings_hash(UINT64_C(14695981039346656037), settings);
  af_directional_light light = af_scene_get_light(scene);
  signature = shadow_hash(signature, &light, sizeof(light));
  signature = shadow_hash(signature, &clear, sizeof(clear));
  signature =
      shadow_hash(signature, &r->texture_revision, sizeof(r->texture_revision));
  signature =
      shadow_hash(signature, &r->mesh_revision, sizeof(r->mesh_revision));
  for (size_t i = 0; i < af_scene_local_light_count(scene); ++i)
    signature =
        temporal_light_hash(signature, *af_scene_local_light_at(scene, i));
  bool cut = af_vec3_length(af_vec3_sub(camera->position,
                                        r->temporal_camera.position)) > 5 ||
             af_vec3_dot(af_camera_forward(camera),
                         af_camera_forward(&r->temporal_camera)) < .7071f ||
             camera->vertical_fov != r->temporal_camera.vertical_fov ||
             camera->near_clip != r->temporal_camera.near_clip ||
             camera->far_clip != r->temporal_camera.far_clip;
  if (!r->temporal_valid || r->temporal_scene != scene ||
      memcmp(&area, &r->temporal_viewport, sizeof(area)) ||
      signature != r->temporal_signature || cut) {
    r->temporal_valid = false;
    r->temporal_frame = 0;
    ++r->temporal_resets;
    af_vk_post_reset_temporal(r->swapchain.post);
  }
  r->temporal_scene = scene;
  r->temporal_camera = *camera;
  r->temporal_viewport = area;
  r->temporal_signature = signature;
  double ratio = fmax((double)r->swapchain.extent.width / r->swapchain.render_extent.width,
                      (double)r->swapchain.extent.height / r->swapchain.render_extent.height);
  unsigned phases = (unsigned)fmax(16, ceil(8 * ratio * ratio));
  unsigned phase = (unsigned)(r->temporal_frame % phases) + 1;
  int width = 1, height = 1;
  SDL_GetWindowSize(r->window, &width, &height);
  float pixels_x = area.width * (float)r->swapchain.render_extent.width /
                   (float)(width ? width : 1),
        pixels_y = area.height * (float)r->swapchain.render_extent.height /
                   (float)(height ? height : 1);
  float jx = jitter ? halton(phase, 2) - .5f : 0.f;
  float jy = jitter ? halton(phase, 3) - .5f : 0.f;
  r->temporal_jitter = (af_vec2){jx / (float)r->swapchain.render_extent.width,
                                 jy / (float)r->swapchain.render_extent.height};
  for (unsigned c = 0; c < 4; ++c) {
    uniform->view_projection.elements[4 * c] +=
        2 * jx / pixels_x * uniform->view_projection.elements[4 * c + 3];
    uniform->view_projection.elements[4 * c + 1] +=
        2 * jy / pixels_y * uniform->view_projection.elements[4 * c + 3];
  }
  if (!af_mat4_inverse(uniform->view_projection,
                       &uniform->inverse_view_projection))
    return false;
  uniform->previous_view_projection = r->temporal_valid
                                          ? r->previous_view_projection
                                          : uniform->unjittered_view_projection;
  uniform->sky_params.y = 1;
  size_t count = af_scene_object_count(scene);
  if (count > AF_SCENE_FILE_MAX_OBJECTS) {
    af_log("error", "Temporal rendering supports at most %u objects.",
           AF_SCENE_FILE_MAX_OBJECTS);
    return false;
  }
  size_t capacity = 16;
  while (capacity < count)
    capacity *= 2;
  if (r->motion_records_capacity < capacity) {
    af_motion_record *next =
        realloc(r->motion_records, capacity * sizeof(*next));
    if (!next)
      return false;
    r->motion_records = next;
    r->motion_records_capacity = capacity;
  }
  if (r->motion_next_capacity < capacity) {
    af_motion_snapshot *next =
        realloc(r->motion_next, capacity * sizeof(*next));
    if (!next)
      return false;
    r->motion_next = next;
    r->motion_next_capacity = capacity;
  }
  r->motion_next_count = count;
  af_motion_record *records = r->motion_records;
  memset(records, 0, r->motion_records_capacity * sizeof(*records));
  r->temporal_objects = r->temporal_skins = 0;
  for (size_t i = 0; i < count; ++i) {
    const af_scene_object *o = af_scene_object_at(scene, i);
    af_mat4 model = af_mat4_identity();
    bool visible = object_world(r, scene, i, &model) &&
                   af_scene_object_visible(scene, o->id);
    uint64_t hash =
        shadow_hash(UINT64_C(14695981039346656037), o->mesh, strlen(o->mesh));
    hash = temporal_material_hash(hash, o->material);
    r->motion_next[i] = (af_motion_snapshot){o->id, model, hash, visible};
    af_motion_snapshot key = {.id = o->id};
    const af_motion_snapshot *old =
        r->motion_count ? bsearch(&key, r->motion_previous, r->motion_count,
                                  sizeof(key), motion_compare)
                        : NULL;
    bool valid = r->temporal_valid && old && old->visible && visible &&
                 old->signature == hash;
    records[i].model = valid ? old->model : model;
    size_t current_skin = 0;
    while (current_skin < r->skin_count &&
           r->skins[current_skin].object != o->id)
      ++current_skin;
    size_t previous_skin = 0;
    while (previous_skin < r->previous_skin_count &&
           r->previous_skins[previous_skin].object != o->id)
      ++previous_skin;
    if (current_skin < r->skin_count) {
      bool bones = previous_skin < r->previous_skin_count &&
                   r->previous_skins[previous_skin].count ==
                       r->skins[current_skin].count;
      valid &= bones;
      if (bones)
        records[i].info[0] =
            (uint32_t)(AF_SKIN_MATRICES + 1 +
                       previous_skin * AF_ANIMATION_MAX_JOINTS);
    } else if (previous_skin < r->previous_skin_count)
      valid = false;
    records[i].info[1] = valid ? 1u : 0u;
    r->temporal_objects += valid;
    r->temporal_skins += valid && current_skin < r->skin_count;
  }
  if (count > 1)
    qsort(r->motion_next, count, sizeof(*r->motion_next), motion_compare);
  VkDeviceSize bytes = (count ? count : 1) * sizeof(*records);
  if (frame->motion_buffer.capacity < bytes) {
    af_gpu_buffer replacement = {0};
    VkDeviceSize allocation = r->motion_records_capacity * sizeof(*records);
    if (!upload_buffer(r, records, allocation,
                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &replacement)) {
      destroy_buffer(r, &replacement);
      return false;
    }
    destroy_buffer(r, &frame->motion_buffer);
    frame->motion_buffer = replacement;
    frame->motion_buffer.capacity = allocation;
    VkDescriptorBufferInfo info = {frame->motion_buffer.handle, 0,
                                   VK_WHOLE_SIZE};
    VkWriteDescriptorSet write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = frame->descriptor,
        .dstBinding = 9,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .pBufferInfo = &info};
    vkUpdateDescriptorSets(r->device, 1, &write, 0, NULL);
  } else {
    void *mapped = NULL;
    if (!vk_ok(vkMapMemory(r->device, frame->motion_buffer.memory, 0,
                           VK_WHOLE_SIZE, 0, &mapped),
               "map temporal transforms"))
      return false;
    memcpy(mapped, records, (size_t)bytes);
    VkMappedMemoryRange range = {.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
                                 .memory = frame->motion_buffer.memory,
                                 .size = VK_WHOLE_SIZE};
    bool success = vk_ok(vkFlushMappedMemoryRanges(r->device, 1, &range),
                         "flush temporal transforms");
    vkUnmapMemory(r->device, frame->motion_buffer.memory);
    if (!success)
      return false;
  }
  return true;
}
static VkImageMemoryBarrier upscaler_barrier(VkImage image, VkImageAspectFlags aspect,
    VkImageLayout old_layout, VkImageLayout new_layout, VkAccessFlags source, VkAccessFlags destination)
{
    return (VkImageMemoryBarrier){
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = source, .dstAccessMask = destination,
        .oldLayout = old_layout, .newLayout = new_layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image, .subresourceRange = {aspect, 0, 1, 0, 1},
    };
}

static bool record_upscaler(af_renderer *r, VkCommandBuffer command, uint32_t image,
                            af_rect area, float exposure, const af_camera *camera, float frame_time_ms)
{
    af_swapchain *swapchain = &r->swapchain;
    af_depth_attachment depth = swapchain->depths[image];
    af_vulkan_upscaler_frame frame = {
        .struct_size = sizeof(af_vulkan_upscaler_frame), .frame_slot = r->frame_index,
        .reset_history = !r->temporal_valid, .command_buffer = command,
        .depth = {depth.image, depth.view},
        .viewport_uv = {area.x, area.y, area.width, area.height},
        .jitter_pixels = {r->temporal_jitter.x * (float)swapchain->render_extent.width,
                          r->temporal_jitter.y * (float)swapchain->render_extent.height},
        .exposure_multiplier = exp2f(exposure),
        .camera_near = camera->near_clip, .camera_far = camera->far_clip,
        .vertical_fov_radians = camera->vertical_fov, .frame_time_ms = frame_time_ms,
    };
    af_vk_post_upscaler_images(swapchain->post, image, &frame);
    VkImageAspectFlags depth_aspects = VK_IMAGE_ASPECT_DEPTH_BIT;
    if (swapchain->depth_format == VK_FORMAT_D24_UNORM_S8_UINT ||
        swapchain->depth_format == VK_FORMAT_D32_SFLOAT_S8_UINT) depth_aspects |= VK_IMAGE_ASPECT_STENCIL_BIT;
    const VkImageLayout sampled = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkImageMemoryBarrier inputs[] = {
        upscaler_barrier(frame.color.image, VK_IMAGE_ASPECT_COLOR_BIT, sampled, sampled,
                         VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_MEMORY_READ_BIT),
        upscaler_barrier(frame.motion.image, VK_IMAGE_ASPECT_COLOR_BIT, sampled, sampled,
                         VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_MEMORY_READ_BIT),
        upscaler_barrier(frame.depth.image, depth_aspects, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                         VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
                         VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT),
        upscaler_barrier(frame.output.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                         VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_MEMORY_WRITE_BIT),
    };
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         0, 0, NULL, 0, NULL, 4, inputs);
    if (!r->upscaler->record(swapchain->upscaler_context, &frame)) {
        af_log("warning", "Vulkan upscaler declined the frame. Restoring built-in rendering.");
        r->upscaler_failed = true;
        return false;
    }
    VkImageMemoryBarrier output = upscaler_barrier(frame.output.image, VK_IMAGE_ASPECT_COLOR_BIT,
        VK_IMAGE_LAYOUT_GENERAL, sampled, VK_ACCESS_MEMORY_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, NULL, 0, NULL, 1, &output);
    return true;
}

bool af_renderer_draw(af_renderer *renderer, af_color color, const af_scene *scene, const af_camera *camera, af_rect viewport_rect, af_object_id highlighted, const af_overlay_frame *overlay, float frame_time_ms, bool *rendered)
{
    *rendered = false;renderer->missing_textures=0;
    af_render_settings environment_settings=af_scene_get_render_settings(scene);
    bool upscale = renderer->upscaler_enabled && !renderer->upscaler_failed &&
                   renderer->temporal_supported && renderer->debug_view == AF_DEBUG_SHADED &&
                   !(renderer->capture_pending && renderer->capture_motion);
    if (upscale) environment_settings.anti_aliasing = AF_AA_OFF;
    if(renderer->debug_view!=AF_DEBUG_SHADED)environment_settings.anti_aliasing=AF_AA_TAA;
    if(environment_settings.anti_aliasing==AF_AA_TAA&&!renderer->temporal_supported)environment_settings.anti_aliasing=AF_AA_FXAA;
    if(environment_settings.motion_blur&&!renderer->temporal_supported)environment_settings.motion_blur=false;
    if(environment_settings.motion_blur&&!renderer->temporal_supported)environment_settings.motion_blur=false;
    uint32_t resource_config=(uint32_t)environment_settings.anti_aliasing|(environment_settings.bloom?AF_RESOURCE_BLOOM:0u)|((environment_settings.anti_aliasing==AF_AA_TAA||environment_settings.motion_blur||environment_settings.depth_of_field||(renderer->capture_pending&&renderer->capture_motion))?AF_RESOURCE_MOTION:0u)|((environment_settings.refraction&&af_scene_water_count(scene)>0)?AF_RESOURCE_REFRACTION:0u);
    if (upscale) resource_config |= AF_RESOURCE_UPSCALER | AF_RESOURCE_MOTION;
    if(renderer->ray_supported && !renderer->ray_failed){
        bool any_shadows = af_scene_get_shadows(scene).enabled;
        if (!any_shadows) {
            for (size_t i = 0; i < af_scene_local_light_count(scene); ++i) {
                const af_local_light *local = af_scene_local_light_at(scene, i);
                if (local->enabled && local->cast_shadows && local->intensity > 0) {
                    any_shadows = true;
                    break;
                }
            }
        }
        if(environment_settings.ray_traced_shadows && any_shadows)
            resource_config|=AF_RESOURCE_RAY_SHADOWS;
        if(environment_settings.ray_traced_water_reflections){
            for(size_t i=0;i<af_scene_water_count(scene);++i){
                const af_water *water=af_scene_water_at(scene,i);
                if(water->enabled && water->mode!=AF_WATER_PARTICLES &&
                   af_scene_object_visible(scene,water->owner)){
                    resource_config|=AF_RESOURCE_RAY_REFLECTIONS;
                    break;
                }
            }
        }
    }
    if (renderer->resize_pending || !renderer->swapchain.handle || renderer->swapchain.resource_config!=resource_config) {
        if (!create_swapchain(renderer,environment_settings,resource_config)) {
            if (upscale) {
                af_log("warning", "Vulkan upscaler resources unavailable. Restoring built-in rendering.");
                renderer->upscaler_failed = true;
                renderer->resize_pending = renderer->swapchain_invalid = true;
                return true;
            }
            if(resource_config&AF_RESOURCE_RAY_QUERY){af_log("warning","Optional ray-query pipeline unavailable; retrying raster rendering.");renderer->ray_failed=true;renderer->resize_pending=renderer->swapchain_invalid=true;return true;}
            return false;
        }
        if (renderer->resize_pending || !renderer->swapchain.handle ||
            renderer->swapchain.resource_config != resource_config) return true;
    }
    if(!(resource_config&AF_RESOURCE_RAY_QUERY)&&renderer->rays){if(!af_renderer_wait_idle(renderer))return false;af_vk_ray_query_destroy(renderer->rays);renderer->rays=NULL;renderer->ray_raster_casters=0;}
    if((environment_settings.environment_enabled||environment_settings.skybox)&&!environment_settings.environment_asset[0]&&environment_settings.environment_profile!=renderer->environment_profile&&!create_environment(renderer,environment_settings.environment_profile))return false;
    bool environment_found=!environment_settings.environment_asset[0];
    for(size_t i=0;i<renderer->environment_count;++i)if(!strcmp(renderer->environments[i].name,environment_settings.environment_asset))environment_found=true;
    if(!environment_found&&(environment_settings.environment_enabled||environment_settings.skybox))++renderer->missing_textures;
    if(strcmp(renderer->active_environment,environment_settings.environment_asset)){
        if(!af_renderer_wait_idle(renderer))return false;snprintf(renderer->active_environment,sizeof(renderer->active_environment),"%s",environment_settings.environment_asset);update_environment_descriptors(renderer);
    }
    if(environment_settings.anti_aliasing==AF_AA_TAA&&!renderer->temporal_supported)environment_settings.anti_aliasing=AF_AA_FXAA;
    if(environment_settings.motion_blur&&!renderer->temporal_supported)environment_settings.motion_blur=false;
    af_frame_uniform uniform;af_shadow_view shadow_view;
    if(!prepare_shadows(renderer,scene,camera,resolve_viewport(renderer,viewport_rect),&uniform,&shadow_view))return false;
    af_frame *frame = &renderer->frames[renderer->frame_index];
    Uint64 wait_start=SDL_GetPerformanceCounter();
    if (!vk_ok(vkWaitForFences(renderer->device, 1, &frame->fence, VK_TRUE, UINT64_MAX), "vkWaitForFences"))
        return false;
    renderer->cpu_fence_ms=(double)(SDL_GetPerformanceCounter()-wait_start)*1000/(double)SDL_GetPerformanceFrequency();
    if(!stream_prepare(renderer))return false;
    bool ray_shadows=(resource_config&AF_RESOURCE_RAY_SHADOWS)!=0;
    bool ray_reflections=(resource_config&AF_RESOURCE_RAY_REFLECTIONS)!=0;
    uniform.sky_params.z=ray_shadows?1.f:0.f;
    uniform.sky_params.w=ray_reflections?1.f:0.f;
    if((ray_shadows||ray_reflections)&&!prepare_ray_scene(renderer,frame,scene,ray_shadows,ray_reflections)){
        af_log("warning","Ray-query initialization/build preparation failed; using raster and environment fallbacks.");
        renderer->ray_failed=true;renderer->resize_pending=true;return true;
    }
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
    skin_destination[AF_SKIN_MATRICES]=af_mat4_identity();
    for(size_t i=0;i<renderer->previous_skin_count;++i)for(unsigned j=0;j<AF_ANIMATION_MAX_JOINTS;++j)skin_destination[AF_SKIN_MATRICES+1+i*AF_ANIMATION_MAX_JOINTS+j]=j<renderer->previous_skins[i].count?renderer->previous_skins[i].matrices[j]:af_mat4_identity();
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
    bool temporal = renderer->temporal_supported &&
                    (upscale || environment_settings.anti_aliasing == AF_AA_TAA ||
                     environment_settings.motion_blur ||
                     (renderer->capture_pending && renderer->capture_motion));
    bool spatial = upscale && renderer->upscaler->struct_size >= sizeof(*renderer->upscaler) &&
                   (renderer->upscaler->flags & AF_VULKAN_UPSCALER_SPATIAL);
    bool jitter = (upscale && !spatial) || environment_settings.anti_aliasing == AF_AA_TAA;
    if(!prepare_temporal(renderer,frame,scene,camera,resolve_viewport(renderer,viewport_rect),color,environment_settings,temporal,jitter,&uniform))return false;
    VkDescriptorImageInfo refraction_images[]={af_vk_refraction_color(renderer->swapchain.refraction,image_index),af_vk_refraction_depth(renderer->swapchain.refraction,image_index)};VkWriteDescriptorSet water_writes[2];for(unsigned i=0;i<2;++i)water_writes[i]=(VkWriteDescriptorSet){.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=frame->descriptor,.dstBinding=7+i,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.pImageInfo=&refraction_images[i]};vkUpdateDescriptorSets(renderer->device,2,water_writes,0,NULL);
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
    af_vk_refraction_prepare(renderer->swapchain.refraction,frame->command,image_index);
    if(renderer->ui)af_vk_ui_record_uploads(renderer->ui,frame->command,renderer->frame_index);
    if(frame->timestamps){vkCmdResetQueryPool(frame->command,frame->timestamps,0,5);vkCmdWriteTimestamp(frame->command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,frame->timestamps,0);}
    if(resource_config&AF_RESOURCE_RAY_QUERY)af_vk_ray_query_record(renderer->rays,renderer->frame_index,frame->command);
    renderer->shadow_draw_calls=0;renderer->shadow_passes=renderer->shadow_cached_passes=0;
    for(uint32_t layer=0;layer<shadow_view.count;++layer){
        uint64_t signature=shadow_signature(renderer,shadow_view.matrices[layer],af_scene_get_shadows(scene),uniform.shadow_params.x>.5f);
        signature=shadow_hash(signature,&resource_config,sizeof(resource_config));
        if(frame->shadow_signatures[layer]==signature){++renderer->shadow_cached_passes;continue;}frame->shadow_signatures[layer]=signature;++renderer->shadow_passes;
        af_vk_shadows_begin(renderer->shadows,frame->command,renderer->frame_index,layer,af_scene_get_shadows(scene),frame->descriptor);
        for(size_t i=0;uniform.shadow_params.x>.5f&&i<renderer->shadow_count;++i){
            if(!af_shadow_intersects(shadow_view.matrices[layer],&renderer->shadow_bounds[i]))continue;
            af_shadow_draw draw=renderer->shadow_draws[i];if(ray_shadows&&draw.ray_caster)continue;const af_gpu_mesh *mesh=&renderer->meshes[draw.mesh].gpu;
            af_mat4 mvp=af_mat4_mul(shadow_view.matrices[layer],draw.model);af_vk_shadows_push(renderer->shadows,frame->command,mvp.elements,draw.skin_base,draw.alpha_mode,draw.alpha,draw.cutoff,af_vk_texture_descriptor(draw.texture));
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
            af_shadow_draw draw=renderer->shadow_draws[i];if(ray_shadows&&draw.ray_caster)continue;const af_gpu_mesh *mesh=&renderer->meshes[draw.mesh].gpu;
            af_mat4 mvp=af_mat4_mul(uniform.local_shadow_matrices[layer],draw.model);af_vk_shadows_push(renderer->local_shadows,frame->command,mvp.elements,draw.skin_base,draw.alpha_mode,draw.alpha,draw.cutoff,af_vk_texture_descriptor(draw.texture));
            VkDeviceSize offset=0;vkCmdBindVertexBuffers(frame->command,0,1,&mesh->vertices.handle,&offset);vkCmdBindIndexBuffer(frame->command,mesh->indices.handle,0,VK_INDEX_TYPE_UINT32);vkCmdDrawIndexed(frame->command,mesh->index_count,1,0,0,0);++renderer->shadow_draw_calls;
        }vkCmdEndRenderPass(frame->command);
    }
    if(frame->timestamps)vkCmdWriteTimestamp(frame->command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,frame->timestamps,1);
    VkClearValue clears[] = {
        {.color = {.float32 = {color.r, color.g, color.b, color.a}}},
        {.depthStencil = {.depth = 1.0f}},
        {.color = {.float32={0,0,-1,65504}}}
    };
    VkRenderPassBeginInfo pass = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = renderer->swapchain.render_pass,
        .framebuffer = af_vk_post_scene_framebuffer(renderer->swapchain.post,image_index),
        .renderArea = {.extent = renderer->swapchain.render_extent},
        .clearValueCount = 3, .pClearValues = clears
    };
    vkCmdBeginRenderPass(frame->command, &pass, VK_SUBPASS_CONTENTS_INLINE);
    af_mat4 visibility_projection=uniform.unjittered_view_projection;
    float visible_width=uniform.water_screen.x*(uniform.water_viewport.z-uniform.water_viewport.x),visible_height=uniform.water_screen.y*(uniform.water_viewport.w-uniform.water_viewport.y);
    for(unsigned column=0;column<4;++column){visibility_projection.elements[column*4]*=visible_width/(visible_width+4);visibility_projection.elements[column*4+1]*=visible_height/(visible_height+4);}
    size_t draw_calls = 0, missing_meshes = 0;renderer->visible_objects=renderer->culled_objects=0;
    size_t object_count = af_scene_object_count(scene);
    if (object_count > 0||environment_settings.skybox) {
        VkExtent2D extent = renderer->swapchain.render_extent;
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
        af_blend_draw *blended=object_count?malloc(object_count*sizeof(*blended)):NULL;size_t blend_count=0;if(object_count&&!blended)return false;
        for(size_t i=0;i<object_count;++i){const af_scene_object *o=af_scene_object_at(scene,i);if(o->material.alpha_mode!=AF_ALPHA_BLEND&&!af_scene_get_water(scene,o->id))continue;af_mat4 world;if(!object_world(renderer,scene,i,&world))continue;af_vec3 position={world.elements[12],world.elements[13],world.elements[14]};blended[blend_count++]=(af_blend_draw){i,af_vec3_dot(af_vec3_sub(position,camera->position),af_camera_forward(camera))};}if(blend_count>1)qsort(blended,blend_count,sizeof(*blended),blend_compare);
        bool refract=environment_settings.refraction&&environment_settings.refraction_strength>0&&af_scene_water_count(scene)>0;
        for(unsigned alpha_pass=0;alpha_pass<2;++alpha_pass){if(alpha_pass&&refract){vkCmdEndRenderPass(frame->command);af_vk_refraction_copy(renderer->swapchain.refraction,frame->command,image_index,af_vk_post_scene_image(renderer->swapchain.post,image_index),renderer->swapchain.depths[image_index].image);pass.renderPass=renderer->swapchain.water_pass;vkCmdBeginRenderPass(frame->command,&pass,VK_SUBPASS_CONTENTS_INLINE);}
        vkCmdBindPipeline(frame->command,VK_PIPELINE_BIND_POINT_GRAPHICS,alpha_pass?renderer->swapchain.blend_pipeline:renderer->swapchain.pipeline);
        for (size_t order = 0; order < (alpha_pass?blend_count:object_count); ++order) {size_t i=alpha_pass?blended[order].index:order;

            const af_scene_object *object = af_scene_object_at(scene, i);
            const af_water *water=af_scene_get_water(scene,object->id);if(water&&!water->enabled)continue;
            if(!alpha_pass&&(object->material.alpha_mode==AF_ALPHA_BLEND||water))continue;
            if (!af_scene_object_visible(scene,object->id)||(!water&&!strcmp(object->mesh,"__node"))) continue;
            char water_mesh[64];if(water)water_name(object->id,water_mesh);
            size_t index = find_mesh_index(renderer, water?water_mesh:object->mesh);
            if (index == renderer->mesh_count) {
                ++missing_meshes;
                continue;
            }
            const af_mesh_asset *asset=&renderer->meshes[index];
            const af_gpu_mesh *mesh = asset->dynamic?&asset->streams[renderer->frame_index].gpu:&asset->gpu;
            af_mat4 world;if(!object_world(renderer,scene,i,&world))continue;
            if(!renderer->culling_disabled){af_shadow_bounds bounds=object_bounds(renderer,object->id,world,mesh);if(!af_shadow_intersects(visibility_projection,&bounds)){++renderer->culled_objects;continue;}}
            ++renderer->visible_objects;
            af_color base=object->material.base_color;
            if(object->id==highlighted){base.r*=1.25f;base.g*=1.1f;base.b*=0.75f;}
            struct {af_mat4 model;af_color tint;af_vec4 params,surface,emission;} transform={world,base,{(float)((object->material.unlit?1u:0u)|((uint32_t)object->material.alpha_mode<<1u)|((uint32_t)lroundf(object->material.alpha_cutoff*65535)<<3u)),object->material.receive_shadows?1.0f:0.0f,object->material.physically_based?1.0f:0.0f,object->material.normal_map[0]?1.0f:0.0f},{object->material.roughness,object->material.metallic,object->material.normal_strength,(float)skin_base(renderer,object->id,mesh)},{object->material.emission.x,object->material.emission.y,object->material.emission.z,object->material.emission_strength}};
            if(water){transform.tint=(af_color){water->scattering.x,water->scattering.y,water->scattering.z,1};transform.params=(af_vec4){(float)((1u<<19u)|(1u<<21u)|(2u<<1u)),1,1,water->wind};if(water->mode!=AF_WATER_PARTICLES)transform.params.x+=(float)(1u<<18u);if(refract)transform.params.x+=(float)(1u<<22u);if(water->mode!=AF_WATER_PARTICLES&&wa_contains(ph_water_runtime(renderer->water_physics),scene,water->owner,camera->position))transform.params.x+=(float)(1u<<23u);transform.surface=(af_vec4){water->roughness,water->ripple_scale,water->mode==AF_WATER_PARTICLES?water->particle_spacing:water->depth,water->mode==AF_WATER_PARTICLES?0:water->ripple_strength};transform.emission=(af_vec4){water->absorption.x,water->absorption.y,water->absorption.z,water->ior};}
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
            if(water){vkCmdBindPipeline(frame->command,VK_PIPELINE_BIND_POINT_GRAPHICS,renderer->swapchain.water_attenuation);transform.params.x+=(float)(1u<<20u);vkCmdPushConstants(frame->command,renderer->swapchain.pipeline_layout,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(transform),&transform);vkCmdDrawIndexed(frame->command,mesh->index_count,1,0,0,(uint32_t)i);++draw_calls;transform.params.x-=(float)(1u<<20u);vkCmdBindPipeline(frame->command,VK_PIPELINE_BIND_POINT_GRAPHICS,renderer->swapchain.water_scattering);vkCmdPushConstants(frame->command,renderer->swapchain.pipeline_layout,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(transform),&transform);}
            else vkCmdBindPipeline(frame->command,VK_PIPELINE_BIND_POINT_GRAPHICS,alpha_pass?renderer->swapchain.blend_pipeline:renderer->swapchain.pipeline);
            vkCmdDrawIndexed(frame->command, mesh->index_count, 1, 0, 0, (uint32_t)i);
            ++draw_calls;
        }}free(blended);
    }
    vkCmdEndRenderPass(frame->command);
    int logical_w=0,logical_h=0;SDL_GetWindowSize(renderer->window,&logical_w,&logical_h);
    af_rect post_area=resolve_viewport(renderer,viewport_rect);post_area.x/=(float)(logical_w?logical_w:1);post_area.y/=(float)(logical_h?logical_h:1);post_area.width/=(float)(logical_w?logical_w:1);post_area.height/=(float)(logical_h?logical_h:1);
    if(frame->timestamps)vkCmdWriteTimestamp(frame->command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,frame->timestamps,2);
    bool upscaled = upscale && record_upscaler(renderer, frame->command, image_index, post_area,
                                                environment_settings.exposure, camera, frame_time_ms);
    float time_sec = (float)renderer->rendered_frames * (1.0f / 60.0f);
    float delta_sec = frame_time_ms * 0.001f;
    af_vk_post_draw(renderer->swapchain.post,frame->command,image_index,environment_settings,post_area,renderer->temporal_jitter,renderer->debug_view,upscaled,time_sec,delta_sec);
    if(frame->timestamps)vkCmdWriteTimestamp(frame->command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,frame->timestamps,3);
    if (renderer->ui && overlay->index_count)
        af_vk_ui_draw(renderer->ui, frame->command, renderer->frame_index, overlay, renderer->swapchain.extent);
    vkCmdEndRenderPass(frame->command);
    if(frame->timestamps)vkCmdWriteTimestamp(frame->command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,frame->timestamps,4);
    if(renderer->capture_pending){
        int w=0,h=0;SDL_GetWindowSize(renderer->window,&w,&h);
        uint32_t x=(uint32_t)fmaxf(0,fminf((float)renderer->swapchain.extent.width-1,renderer->capture_position.x*(float)renderer->swapchain.extent.width/(float)(w?w:1)));
        uint32_t y=(uint32_t)fmaxf(0,fminf((float)renderer->swapchain.extent.height-1,renderer->capture_position.y*(float)renderer->swapchain.extent.height/(float)(h?h:1)));
        VkImageLayout capture_layout=renderer->capture_motion?VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        VkImageMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.oldLayout=capture_layout,.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
            .image=renderer->capture_motion?af_vk_post_motion_image(renderer->swapchain.post,image_index):renderer->swapchain.images[image_index],.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
        barrier.srcAccessMask|=renderer->capture_motion?VK_ACCESS_SHADER_READ_BIT:0;
        vkCmdPipelineBarrier(frame->command,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&barrier);
        renderer->capture_extent.width=renderer->capture_extent.width<renderer->swapchain.extent.width-x?renderer->capture_extent.width:renderer->swapchain.extent.width-x;renderer->capture_extent.height=renderer->capture_extent.height<renderer->swapchain.extent.height-y?renderer->capture_extent.height:renderer->swapchain.extent.height-y;
        VkBufferImageCopy copy={.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageOffset={(int32_t)x,(int32_t)y,0},.imageExtent={renderer->capture_extent.width,renderer->capture_extent.height,1}};
        vkCmdCopyImageToBuffer(frame->command,barrier.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,renderer->capture_buffer.handle,1,&copy);
        VkBufferMemoryBarrier host={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT,
            .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.buffer=renderer->capture_buffer.handle,.size=VK_WHOLE_SIZE};
        vkCmdPipelineBarrier(frame->command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,NULL,1,&host,0,NULL);
        barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;barrier.newLayout=capture_layout;barrier.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT;barrier.dstAccessMask=renderer->capture_motion?VK_ACCESS_SHADER_READ_BIT:0;
        vkCmdPipelineBarrier(frame->command,VK_PIPELINE_STAGE_TRANSFER_BIT,renderer->capture_motion?VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT:VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,0,0,NULL,0,NULL,1,&barrier);
        renderer->capture_pending=false;renderer->capture_ready=true;renderer->capture_format=renderer->capture_motion?VK_FORMAT_R16G16B16A16_SFLOAT:renderer->swapchain.surface_format.format;
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
    if(temporal){
        af_motion_snapshot *spare=renderer->motion_previous;size_t spare_capacity=renderer->motion_previous_capacity;
        renderer->motion_previous=renderer->motion_next;renderer->motion_previous_capacity=renderer->motion_next_capacity;renderer->motion_count=renderer->motion_next_count;
        renderer->motion_next=spare;renderer->motion_next_capacity=spare_capacity;
        renderer->previous_view_projection=uniform.unjittered_view_projection;renderer->previous_skin_count=renderer->skin_count;memcpy(renderer->previous_skins,renderer->skins,renderer->skin_count*sizeof(renderer->skins[0]));renderer->temporal_valid=true;++renderer->temporal_frame;
    }
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

bool af_renderer_set_debug_view(af_renderer *r,af_debug_view view){if(!r||view<AF_DEBUG_SHADED||view>=AF_DEBUG_COUNT||(view!=AF_DEBUG_SHADED&&!r->temporal_supported))return false;if(r->debug_view==view)return true;r->debug_view=view;r->temporal_valid=false;return true;}
void af_renderer_set_visibility_culling(af_renderer *r,bool enabled){if(r)r->culling_disabled=!enabled;}
bool af_renderer_set_render_budget(af_renderer *r,double ms,uint64_t bytes){if(!r||!isfinite(ms)||ms<=0||ms>1000||bytes<1024u*1024u)return false;r->gpu_budget_ms=ms;r->gpu_budget_bytes=bytes;return true;}
af_engine_stats af_renderer_stats(const af_renderer *renderer)
{
    size_t active_meshes=0;for(size_t i=0;i<renderer->mesh_count;++i)active_meshes+=renderer->meshes[i].name[0]&&!renderer->meshes[i].retired;
    uint64_t texture_bytes=0;uint32_t mip_levels=0;
    for(size_t i=0;i<renderer->texture_count;++i){af_texture_info info=af_vk_texture_info(renderer->textures[i].texture);texture_bytes+=info.allocation_bytes;mip_levels+=info.mip_levels;}
    uint64_t mesh_bytes=0,other=0;
    for(size_t i=0;i<renderer->mesh_count;++i){const af_mesh_asset *m=&renderer->meshes[i];mesh_bytes+=m->gpu.vertices.allocation_bytes+m->gpu.indices.allocation_bytes;for(unsigned f=0;f<AF_FRAMES_IN_FLIGHT;++f)mesh_bytes+=m->streams[f].gpu.vertices.allocation_bytes+m->streams[f].gpu.indices.allocation_bytes;}
    for(unsigned f=0;f<AF_FRAMES_IN_FLIGHT;++f)other+=renderer->frames[f].uniform.allocation_bytes+renderer->frames[f].skin_buffer.allocation_bytes+renderer->frames[f].motion_buffer.allocation_bytes;
    for(uint32_t i=0;i<renderer->swapchain.image_count;++i)other+=renderer->swapchain.depths[i].allocation_bytes;
    af_vk_texture *fixed[]={renderer->white,renderer->flat_normal,renderer->environment,renderer->irradiance,renderer->brdf};for(unsigned i=0;i<5;++i)if(fixed[i])other+=af_vk_texture_info(fixed[i]).allocation_bytes;
    for(size_t i=0;i<renderer->environment_count;++i){other+=af_vk_texture_info(renderer->environments[i].sky).allocation_bytes+af_vk_texture_info(renderer->environments[i].specular).allocation_bytes+af_vk_texture_info(renderer->environments[i].diffuse).allocation_bytes;}
    af_ray_stats ray=af_vk_ray_query_stats(renderer->rays);other+=ray.bytes;
    uint64_t post=af_vk_post_allocation_bytes(renderer->swapchain.post),shadows=af_vk_shadows_allocation_bytes(renderer->shadows)+af_vk_shadows_allocation_bytes(renderer->local_shadows),refraction=af_vk_refraction_allocation_bytes(renderer->swapchain.refraction),tracked=post+shadows+refraction+mesh_bytes+texture_bytes+other;
    return (af_engine_stats){
        .ray_query_supported=renderer->ray_supported,.ray_traced_shadows_active=(renderer->swapchain.resource_config&AF_RESOURCE_RAY_SHADOWS)!=0&&!renderer->ray_failed,
        .ray_traced_water_reflections_active=(renderer->swapchain.resource_config&AF_RESOURCE_RAY_REFLECTIONS)!=0&&!renderer->ray_failed,
        .ray_tracing_failed=renderer->ray_failed,.ray_tracing_bytes=ray.bytes,.ray_blas_builds=ray.blas_builds,.ray_tlas_builds=ray.tlas_builds,.ray_instances=ray.instances,.ray_meshes=ray.meshes,.ray_raster_casters=renderer->ray_raster_casters,.gpu_ray_build_ms=ray.gpu_build_ms,
        .post_bytes=post,.shadow_bytes=shadows,.refraction_bytes=refraction,.mesh_bytes=mesh_bytes,.tracked_gpu_bytes=tracked,.gpu_budget_ms=renderer->gpu_budget_ms,.gpu_budget_bytes=renderer->gpu_budget_bytes,.gpu_time_over_budget=renderer->timing_valid&&renderer->gpu_ms[0]>renderer->gpu_budget_ms,.gpu_memory_over_budget=tracked>renderer->gpu_budget_bytes,.debug_view=renderer->debug_view,.effective_aa=(af_anti_aliasing)(renderer->swapchain.resource_config&AF_RESOURCE_AA_MASK),.visible_objects=renderer->visible_objects,.culled_objects=renderer->culled_objects,
        .gpu_timing_supported=renderer->timestamp_bits!=0,.gpu_timing_valid=renderer->timing_valid,
        .gpu_frame_ms=renderer->gpu_ms[0],.gpu_shadow_ms=renderer->gpu_ms[1],.gpu_scene_ms=renderer->gpu_ms[2],.gpu_post_ms=renderer->gpu_ms[3],.gpu_overlay_ms=renderer->gpu_ms[4],
        .cpu_fence_ms=renderer->cpu_fence_ms,.cpu_acquire_ms=renderer->cpu_acquire_ms,
        .hdr_enabled=renderer->swapchain.post!=NULL,.local_shadow_lights=renderer->local_shadow_lights,.local_shadow_maps=renderer->local_shadow_lights?renderer->local_shadow_layers:0,.local_shadow_resolution=renderer->local_shadow_resolution,.local_shadow_overflow=renderer->local_shadow_overflow,
        .texture_bytes=texture_bytes,.texture_mip_levels=mip_levels,.texture_anisotropy=renderer->anisotropy,
        .rendered_frames = renderer->rendered_frames,
        .swapchain_rebuilds = renderer->swapchain_rebuilds,
        .loaded_meshes = active_meshes,
        .water_upload_bytes=renderer->water_upload_bytes,.water_stream_uploads=renderer->water_stream_uploads,.mesh_idle_waits=renderer->mesh_idle_waits,.water_upload_ms=renderer->water_upload_ms,
        .loaded_textures=renderer->texture_count,.missing_textures=renderer->missing_textures,
        .draw_calls = renderer->draw_calls,
        .shadow_draw_calls=renderer->shadow_draw_calls,.shadow_passes=renderer->shadow_passes,.shadow_cached_passes=renderer->shadow_cached_passes,.shadow_cascades=renderer->shadow_layers,.shadow_resolution=renderer->shadow_resolution,
        .missing_meshes = renderer->missing_meshes,
        .validation_errors = atomic_load(&renderer->validation_errors),
        .validation_enabled = renderer->validation_enabled
    };
}

static bool upload_texture(af_renderer *renderer,const char *name,const unsigned char *rgba,uint32_t width,uint32_t height,bool normal){
    if(renderer)++renderer->texture_revision;
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

bool af_renderer_remove_texture(af_renderer *renderer,const char *name){if(renderer)++renderer->texture_revision;
    for(size_t i=0;i<renderer->environment_count;++i)if(!strcmp(renderer->environments[i].name,name)){
        if(!af_renderer_wait_idle(renderer))return false;af_environment_asset *asset=&renderer->environments[i];af_vk_texture_destroy(asset->specular);af_vk_texture_destroy(asset->diffuse);af_vk_texture_destroy(asset->sky);
        *asset=renderer->environments[--renderer->environment_count];update_environment_descriptors(renderer);return true;}

    if(!name)return false;size_t i=0;while(i<renderer->texture_count&&strcmp(renderer->textures[i].name,name))++i;
    if(i==renderer->texture_count||!af_renderer_wait_idle(renderer))return false;
    af_vk_texture_destroy(renderer->textures[i].texture);memmove(&renderer->textures[i],&renderer->textures[i+1],(renderer->texture_count-i-1)*sizeof(*renderer->textures));--renderer->texture_count;return true;
}

bool af_renderer_test_capture_region(af_renderer *renderer,af_vec2 position,uint32_t width,uint32_t height){
    if(!renderer||!renderer->capture_supported||renderer->capture_pending||!isfinite(position.x)||!isfinite(position.y)||!width||!height||width>1024||height>1024)return false;
    size_t bytes=(size_t)width*height*4;
    if(renderer->capture_buffer.capacity<bytes){
        if(!af_renderer_wait_idle(renderer))return false;destroy_buffer(renderer,&renderer->capture_buffer);
        unsigned char *zero=calloc(bytes,1);if(!zero)return false;
        bool success=upload_buffer(renderer,zero,bytes,VK_BUFFER_USAGE_TRANSFER_DST_BIT,&renderer->capture_buffer);free(zero);if(!success)return false;renderer->capture_buffer.capacity=bytes;
    }
    renderer->capture_motion=false;renderer->capture_extent=(VkExtent2D){width,height};renderer->capture_position=position;renderer->capture_pending=true;renderer->capture_ready=false;return true;
}
bool af_renderer_test_capture_motion(af_renderer *r,af_vec2 p){
    if(!af_renderer_test_capture_region(r,p,2,1))return false;r->capture_extent.width=1;r->capture_motion=true;return true;
}
static float decode_half(uint16_t bits){
    unsigned exponent=(bits>>10u)&31u,mantissa=bits&1023u;float value=exponent==0?ldexpf((float)mantissa,-24):exponent==31?(mantissa?NAN:INFINITY):ldexpf(1+(float)mantissa/1024,(int)exponent-15);return (bits&32768u)?-value:value;
}
bool af_renderer_test_read_motion(af_renderer *r,af_vec4 *out){
    if(!r||!out||!r->capture_ready||r->capture_format!=VK_FORMAT_R16G16B16A16_SFLOAT||!af_renderer_wait_idle(r))return false;
    void *data=NULL;if(!vk_ok(vkMapMemory(r->device,r->capture_buffer.memory,0,VK_WHOLE_SIZE,0,&data),"map motion pixel"))return false;
    VkMappedMemoryRange range={.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,.memory=r->capture_buffer.memory,.size=VK_WHOLE_SIZE};bool success=vk_ok(vkInvalidateMappedMemoryRanges(r->device,1,&range),"invalidate motion pixel");
    if(success){uint16_t values[4];memcpy(values,data,sizeof(values));*out=(af_vec4){decode_half(values[0]),decode_half(values[1]),decode_half(values[2]),decode_half(values[3])};}
    vkUnmapMemory(r->device,r->capture_buffer.memory);r->capture_ready=false;return success;
}
bool af_renderer_test_capture_pixel(af_renderer *renderer,af_vec2 position){return af_renderer_test_capture_region(renderer,position,1,1);}
bool af_renderer_test_read_pixels(af_renderer *renderer,unsigned char *rgba,size_t capacity,uint32_t *width,uint32_t *height){
    if(!renderer||!rgba||!renderer->capture_ready||renderer->capture_format==VK_FORMAT_R16G16B16A16_SFLOAT||capacity<(size_t)renderer->capture_extent.width*renderer->capture_extent.height*4||!af_renderer_wait_idle(renderer))return false;
    void *data=NULL;if(!vk_ok(vkMapMemory(renderer->device,renderer->capture_buffer.memory,0,VK_WHOLE_SIZE,0,&data),"map pixels"))return false;
    VkMappedMemoryRange range={.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,.memory=renderer->capture_buffer.memory,.size=VK_WHOLE_SIZE};
    bool success=vk_ok(vkInvalidateMappedMemoryRanges(renderer->device,1,&range),"invalidate pixels");
    if(success){const unsigned char *p=data;bool bgra=renderer->capture_format==VK_FORMAT_B8G8R8A8_SRGB||renderer->capture_format==VK_FORMAT_B8G8R8A8_UNORM;
        size_t count=(size_t)renderer->capture_extent.width*renderer->capture_extent.height;
        for(size_t i=0;i<count;++i){rgba[4*i]=p[4*i+(bgra?2u:0u)];rgba[4*i+1]=p[4*i+1];rgba[4*i+2]=p[4*i+(bgra?0u:2u)];rgba[4*i+3]=p[4*i+3];}
        if(width)*width=renderer->capture_extent.width;if(height)*height=renderer->capture_extent.height;
    }
    vkUnmapMemory(renderer->device,renderer->capture_buffer.memory);renderer->capture_ready=false;return success;
}
bool af_renderer_test_read_pixel(af_renderer *renderer,unsigned char rgba[4]){return af_renderer_test_read_pixels(renderer,rgba,4,NULL,NULL);}

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
