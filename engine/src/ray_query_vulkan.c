/* SPDX-License-Identifier: MPL-2.0 */
#include "ray_query_vulkan.h"
#include "log.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define MAX_INSTANCES 65536u
#define MAX_MESHES 4096u
#define MAX_ALLOCATION (UINT64_C(256) * 1024 * 1024)
typedef struct ray_buffer {
  VkBuffer buffer;
  VkDeviceMemory memory;
  VkDeviceSize bytes, capacity;
  VkDeviceAddress address;
  bool coherent;
} ray_buffer;
typedef struct ray_acceleration {
  VkAccelerationStructureKHR handle;
  ray_buffer storage, scratch;
  VkAccelerationStructureGeometryKHR geometry;
  uint32_t primitives;
  VkDeviceAddress address;
  bool dirty;
} ray_acceleration;
typedef struct ray_mesh {
  uint64_t key;
  ray_acceleration as;
  VkDeviceAddress vertices, indices;
} ray_mesh;
typedef struct ray_surface {
  af_color color;
  af_vec4 emission_unlit;
  uint32_t addresses[4];
} ray_surface;
_Static_assert(sizeof(ray_surface) == 48 &&
                   offsetof(ray_surface, addresses) == 32,
               "Reflection hit record matches std430");
_Static_assert(sizeof(af_vertex) == 72 && offsetof(af_vertex, position) == 0 &&
                   offsetof(af_vertex, color) == 12 &&
                   offsetof(af_vertex, normal) == 28,
               "Reflection geometry matches the shader's packed vertex layout");
typedef struct ray_frame {
  ray_acceleration as;
  ray_buffer instances, surfaces;
  size_t capacity;
  uint64_t hash;
  VkQueryPool query;
  bool submitted;
} ray_frame;
struct af_vk_ray_query {
  VkDevice device;
  VkPhysicalDeviceMemoryProperties memory;
  VkPhysicalDeviceAccelerationStructurePropertiesKHR limits;
  PFN_vkCreateAccelerationStructureKHR create;
  PFN_vkDestroyAccelerationStructureKHR destroy;
  PFN_vkGetAccelerationStructureBuildSizesKHR sizes;
  PFN_vkCmdBuildAccelerationStructuresKHR build;
  PFN_vkGetAccelerationStructureDeviceAddressKHR address;
  PFN_vkGetBufferDeviceAddress buffer_address;
  ray_mesh *meshes;
  size_t mesh_count, mesh_capacity;
  ray_frame *frames;
  unsigned frame_count, allocations;
  uint32_t timestamp_bits;
  float timestamp_period;
  af_ray_stats stats;
};
static bool result(VkResult code) {
  if (code == VK_SUCCESS)
    return true;
  af_log("warning",
         "Ray-query resource operation failed (%d); using renderer fallbacks.",
         (int)code);
  return false;
}
static void buffer_free(af_vk_ray_query *r, ray_buffer *b) {
  if (b->buffer)
    vkDestroyBuffer(r->device, b->buffer, NULL);
  if (b->memory)
    vkFreeMemory(r->device, b->memory, NULL);
  r->stats.bytes -= b->bytes;
  *b = (ray_buffer){0};
}
static bool buffer_create(af_vk_ray_query *r, ray_buffer *b, VkDeviceSize size,
                          VkBufferUsageFlags usage, bool host) {
  if (!size || size > MAX_ALLOCATION)
    return false;
  const char *fault = getenv("AFTERLIGHT_TEST_RAY_FAIL_ALLOC");
  if (fault && strtoul(fault, NULL, 10) == ++r->allocations)
    return false;
  VkBufferCreateInfo info = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                             .size = size,
                             .usage = usage |
                                      VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                             .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
  if (!result(vkCreateBuffer(r->device, &info, NULL, &b->buffer)))
    return false;
  VkMemoryRequirements need;
  vkGetBufferMemoryRequirements(r->device, b->buffer, &need);
  uint32_t type = UINT32_MAX;
  for (uint32_t i = 0; i < r->memory.memoryTypeCount; ++i) {
    VkMemoryPropertyFlags flags = r->memory.memoryTypes[i].propertyFlags;
    if (!(need.memoryTypeBits & (1u << i)) ||
        (host && !(flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)))
      continue;
    type = i;
    if (flags & (host ? VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                      : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
      break;
  }
  if (type == UINT32_MAX)
    return false;
  VkMemoryAllocateFlagsInfo flags = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
      .flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT};
  VkMemoryAllocateInfo allocation = {.sType =
                                         VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                                     .pNext = &flags,
                                     .allocationSize = need.size,
                                     .memoryTypeIndex = type};
  if (!result(vkAllocateMemory(r->device, &allocation, NULL, &b->memory)))
    return false;
  b->bytes = need.size;
  r->stats.bytes += need.size;
  b->capacity = size;
  if (!result(vkBindBufferMemory(r->device, b->buffer, b->memory, 0)))
    return false;
  VkBufferDeviceAddressInfo address = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
      .buffer = b->buffer};
  b->address = r->buffer_address(r->device, &address);
  b->coherent = (r->memory.memoryTypes[type].propertyFlags &
                 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
  return b->address != 0;
}
static void acceleration_free(af_vk_ray_query *r, ray_acceleration *a) {
  if (a->handle)
    r->destroy(r->device, a->handle, NULL);
  buffer_free(r, &a->scratch);
  buffer_free(r, &a->storage);
  *a = (ray_acceleration){0};
}
static bool acceleration_create(af_vk_ray_query *r, ray_acceleration *a,
                                VkAccelerationStructureTypeKHR type,
                                VkAccelerationStructureGeometryKHR geometry,
                                uint32_t count) {
  a->geometry = geometry;
  a->primitives = count;
  VkAccelerationStructureBuildGeometryInfoKHR build = {
      .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
      .type = type,
      .flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
      .mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
      .geometryCount = 1,
      .pGeometries = &a->geometry};
  VkAccelerationStructureBuildSizesInfoKHR sizes = {
      .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
  r->sizes(r->device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build,
           &count, &sizes);
  VkDeviceSize alignment =
      r->limits.minAccelerationStructureScratchOffsetAlignment;
  if (!buffer_create(r, &a->storage, sizes.accelerationStructureSize,
                     VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
                     false) ||
      !buffer_create(r, &a->scratch, sizes.buildScratchSize + alignment,
                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false))
    return false;
  VkAccelerationStructureCreateInfoKHR info = {
      .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
      .buffer = a->storage.buffer,
      .size = sizes.accelerationStructureSize,
      .type = type};
  if (!result(r->create(r->device, &info, NULL, &a->handle)))
    return false;
  VkAccelerationStructureDeviceAddressInfoKHR address = {
      .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
      .accelerationStructure = a->handle};
  a->address = r->address(r->device, &address);
  a->dirty = true;
  return a->address != 0;
}
af_vk_ray_query *af_vk_ray_query_create(VkPhysicalDevice physical,
                                        VkDevice device, unsigned frames,
                                        uint32_t bits, float period) {
  af_vk_ray_query *r = calloc(1, sizeof(*r));
  if (!r)
    return NULL;
  r->device = device;
  r->frame_count = frames;
  r->timestamp_bits = bits;
  r->timestamp_period = period;
#define LOAD(field, name)                                                      \
  r->field = (PFN_##name)vkGetDeviceProcAddr(device, #name);                   \
  if (!r->field)                                                               \
  goto fail
  LOAD(create, vkCreateAccelerationStructureKHR);
  LOAD(destroy, vkDestroyAccelerationStructureKHR);
  LOAD(sizes, vkGetAccelerationStructureBuildSizesKHR);
  LOAD(build, vkCmdBuildAccelerationStructuresKHR);
  LOAD(address, vkGetAccelerationStructureDeviceAddressKHR);
  LOAD(buffer_address, vkGetBufferDeviceAddress);
#undef LOAD
  vkGetPhysicalDeviceMemoryProperties(physical, &r->memory);
  r->limits.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;
  VkPhysicalDeviceProperties2 properties = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
      .pNext = &r->limits};
  vkGetPhysicalDeviceProperties2(physical, &properties);
  r->frames = calloc(frames, sizeof(*r->frames));
  if (!r->frames)
    goto fail;
  if (bits)
    for (unsigned i = 0; i < frames; ++i) {
      VkQueryPoolCreateInfo query = {
          .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
          .queryType = VK_QUERY_TYPE_TIMESTAMP,
          .queryCount = 2};
      if (!result(vkCreateQueryPool(device, &query, NULL, &r->frames[i].query)))
        goto fail;
    }
  return r;
fail:
  af_vk_ray_query_destroy(r);
  return NULL;
}
void af_vk_ray_query_destroy(af_vk_ray_query *r) {
  if (!r)
    return;
  if (r->frames)
    for (unsigned i = 0; i < r->frame_count; ++i) {
      acceleration_free(r, &r->frames[i].as);
      buffer_free(r, &r->frames[i].instances);
      buffer_free(r, &r->frames[i].surfaces);
      if (r->frames[i].query)
        vkDestroyQueryPool(r->device, r->frames[i].query, NULL);
    }
  for (size_t i = 0; i < r->mesh_count; ++i)
    acceleration_free(r, &r->meshes[i].as);
  free(r->frames);
  free(r->meshes);
  free(r);
}
static ray_mesh *mesh_get(af_vk_ray_query *r, const af_ray_instance *in) {
  for (size_t i = 0; i < r->mesh_count; ++i)
    if (r->meshes[i].key == in->mesh_key)
      return &r->meshes[i];
  if (r->mesh_count >= MAX_MESHES ||
      in->index_count / 3 > r->limits.maxPrimitiveCount)
    return NULL;
  if (r->mesh_count == r->mesh_capacity) {
    size_t cap = r->mesh_capacity ? r->mesh_capacity * 2 : 16;
    ray_mesh *m = realloc(r->meshes, cap * sizeof(*m));
    if (!m)
      return NULL;
    r->meshes = m;
    r->mesh_capacity = cap;
  }
  ray_mesh *m = &r->meshes[r->mesh_count++];
  *m = (ray_mesh){.key = in->mesh_key};
  VkBufferDeviceAddressInfo
      vi = {.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
            .buffer = in->vertices},
      ii = vi;
  ii.buffer = in->indices;
  m->vertices = r->buffer_address(r->device, &vi);
  m->indices = r->buffer_address(r->device, &ii);
  if (!m->vertices || !m->indices)
    return NULL;
  VkAccelerationStructureGeometryKHR geometry = {
      .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
      .geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
      .flags = VK_GEOMETRY_OPAQUE_BIT_KHR,
      .geometry.triangles = {
          .sType =
              VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
          .vertexFormat = VK_FORMAT_R32G32B32_SFLOAT,
          .vertexData.deviceAddress =
              m->vertices + offsetof(af_vertex, position),
          .vertexStride = sizeof(af_vertex),
          .maxVertex = in->vertex_count - 1,
          .indexType = VK_INDEX_TYPE_UINT32,
          .indexData.deviceAddress = m->indices}};
  return acceleration_create(r, &m->as,
                             VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
                             geometry, in->index_count / 3)
             ? m
             : NULL;
}
static uint64_t hash_bytes(uint64_t h, const void *data, size_t bytes) {
  const unsigned char *p = data;
  for (size_t i = 0; i < bytes; ++i) {
    h ^= p[i];
    h *= UINT64_C(1099511628211);
  }
  return h;
}
bool af_vk_ray_query_prepare(af_vk_ray_query *r, unsigned frame,
                             const af_ray_instance *in, size_t count,
                             bool reflections, VkDescriptorSet descriptor) {
  if (!r || frame >= r->frame_count || count > MAX_INSTANCES ||
      count > r->limits.maxInstanceCount)
    return false;
  ray_frame *f = &r->frames[frame];
  if (f->submitted) {
    uint64_t ticks[2];
    VkResult code =
        vkGetQueryPoolResults(r->device, f->query, 0, 2, sizeof(ticks), ticks,
                              sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);
    if (code == VK_SUCCESS) {
      uint64_t mask = r->timestamp_bits == 64
                          ? UINT64_MAX
                          : ((UINT64_C(1) << r->timestamp_bits) - 1);
      r->stats.gpu_build_ms = (double)((ticks[1] - ticks[0]) & mask) *
                              r->timestamp_period / 1000000;
    } else if (code != VK_NOT_READY)
      return false;
  }
  VkAccelerationStructureInstanceKHR *instances =
      calloc(count ? count : 1, sizeof(*instances));
  if (!instances)
    return false;
  size_t surface_count = reflections && count ? count : 1;
  size_t surface_bytes = surface_count * sizeof(ray_surface);
  if (f->surfaces.capacity < surface_bytes ||
      (!reflections && f->surfaces.capacity > surface_bytes)) {
    buffer_free(r, &f->surfaces);
    if (!buffer_create(r, &f->surfaces, surface_bytes,
                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true)) {
      free(instances);
      return false;
    }
  }
  ray_surface *surfaces = NULL;
  if (!result(vkMapMemory(r->device, f->surfaces.memory, 0, VK_WHOLE_SIZE, 0,
                          (void **)&surfaces))) {
    free(instances);
    return false;
  }
  surfaces[0] = (ray_surface){0};
  bool ok = true;
  uint64_t hash =
      hash_bytes(UINT64_C(14695981039346656037), &count, sizeof(count));
  for (size_t i = 0; i < count; ++i) {
    ray_mesh *m = mesh_get(r, &in[i]);
    if (!m) {
      ok = false;
      break;
    }
    for (unsigned row = 0; row < 3; ++row)
      for (unsigned col = 0; col < 4; ++col)
        instances[i].transform.matrix[row][col] =
            in[i].world.elements[col * 4 + row];
    instances[i].mask = in[i].mask;
    instances[i].instanceCustomIndex = reflections ? (uint32_t)i : 0;
    if (reflections) {
      surfaces[i] = (ray_surface){
          .color = in[i].color,
          .emission_unlit = in[i].emission_unlit,
          .addresses = {(uint32_t)m->vertices, (uint32_t)(m->vertices >> 32),
                        (uint32_t)m->indices, (uint32_t)(m->indices >> 32)}};
    }
    instances[i].flags =
        VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
    instances[i].accelerationStructureReference = m->as.address;
    hash = hash_bytes(hash, &instances[i], sizeof(instances[i]));
  }
  if (ok && !f->surfaces.coherent) {
    VkMappedMemoryRange range = {.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
                                 .memory = f->surfaces.memory,
                                 .size = VK_WHOLE_SIZE};
    ok = result(vkFlushMappedMemoryRanges(r->device, 1, &range));
  }
  vkUnmapMemory(r->device, f->surfaces.memory);
  if (!ok) {
    free(instances);
    return false;
  }
  size_t capacity = count ? count : 1;
  if (capacity > f->capacity || !f->as.handle) {
    acceleration_free(r, &f->as);
    buffer_free(r, &f->instances);
    f->capacity = 0;
    f->hash = 0;
    if (!buffer_create(
            r, &f->instances, capacity * sizeof(*instances),
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
            true)) {
      free(instances);
      return false;
    }
    VkAccelerationStructureGeometryKHR geometry = {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
        .geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR,
        .geometry.instances = {
            .sType =
                VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
            .data.deviceAddress = f->instances.address}};
    if (!acceleration_create(r, &f->as,
                             VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
                             geometry, (uint32_t)capacity)) {
      free(instances);
      return false;
    }
    f->capacity = capacity;
  }
  f->as.primitives = (uint32_t)count;
  f->as.dirty = f->hash != hash || f->as.dirty;
  if (f->as.dirty) {
    void *mapped = NULL;
    ok = result(vkMapMemory(r->device, f->instances.memory, 0, VK_WHOLE_SIZE, 0,
                            &mapped));
    if (ok) {
      memcpy(mapped, instances, capacity * sizeof(*instances));
      if (!f->instances.coherent) {
        VkMappedMemoryRange range = {.sType =
                                         VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
                                     .memory = f->instances.memory,
                                     .size = VK_WHOLE_SIZE};
        ok = result(vkFlushMappedMemoryRanges(r->device, 1, &range));
      }
      vkUnmapMemory(r->device, f->instances.memory);
    }
  }
  free(instances);
  if (!ok)
    return false;
  f->hash = hash;
  VkWriteDescriptorSetAccelerationStructureKHR acceleration = {
      .sType =
          VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
      .accelerationStructureCount = 1,
      .pAccelerationStructures = &f->as.handle};
  VkWriteDescriptorSet write = {
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .pNext = &acceleration,
      .dstSet = descriptor,
      .dstBinding = 10,
      .descriptorCount = 1,
      .descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR};
  VkDescriptorBufferInfo surface_buffer = {f->surfaces.buffer, 0,
                                           surface_bytes};
  VkWriteDescriptorSet surface_write = {
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = descriptor,
      .dstBinding = 11,
      .descriptorCount = 1,
      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
      .pBufferInfo = &surface_buffer};
  VkWriteDescriptorSet writes[] = {write, surface_write};
  vkUpdateDescriptorSets(r->device, 2, writes, 0, NULL);
  r->stats.instances = count;
  r->stats.meshes = r->mesh_count;
  return true;
}
static void build_one(af_vk_ray_query *r, ray_acceleration *a,
                      VkAccelerationStructureTypeKHR type,
                      VkCommandBuffer command) {
  VkDeviceAddress alignment =
      r->limits.minAccelerationStructureScratchOffsetAlignment;
  VkDeviceAddress scratch =
      (a->scratch.address + alignment - 1) & ~(alignment - 1);
  VkAccelerationStructureBuildGeometryInfoKHR build = {
      .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
      .type = type,
      .flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
      .mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
      .dstAccelerationStructure = a->handle,
      .geometryCount = 1,
      .pGeometries = &a->geometry,
      .scratchData.deviceAddress = scratch};
  VkAccelerationStructureBuildRangeInfoKHR range = {.primitiveCount =
                                                        a->primitives};
  const VkAccelerationStructureBuildRangeInfoKHR *ranges = &range;
  r->build(command, 1, &build, &ranges);
  a->dirty = false;
}
void af_vk_ray_query_record(af_vk_ray_query *r, unsigned frame,
                            VkCommandBuffer command) {
  ray_frame *f = &r->frames[frame];
  if (f->query) {
    vkCmdResetQueryPool(command, f->query, 0, 2);
    vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, f->query,
                        0);
  }
  /* Host uploads and any earlier queue submission's AS writes precede builds.
   */
  VkMemoryBarrier before = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
      .srcAccessMask = VK_ACCESS_HOST_WRITE_BIT |
                       VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
      .dstAccessMask = VK_ACCESS_SHADER_READ_BIT |
                       VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR |
                       VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR};
  vkCmdPipelineBarrier(
      command,
      VK_PIPELINE_STAGE_HOST_BIT |
          VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
      VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, 0, 1, &before, 0,
      NULL, 0, NULL);
  for (size_t i = 0; i < r->mesh_count; ++i)
    if (r->meshes[i].as.dirty) {
      build_one(r, &r->meshes[i].as,
                VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, command);
      ++r->stats.blas_builds;
    }
  VkMemoryBarrier between = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
      .srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
      .dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR};
  vkCmdPipelineBarrier(command,
                       VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                       VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                       0, 1, &between, 0, NULL, 0, NULL);
  if (f->as.dirty) {
    build_one(r, &f->as, VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, command);
    ++r->stats.tlas_builds;
  }
  vkCmdPipelineBarrier(
      command, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1, &between, 0, NULL, 0, NULL);
  if (f->query) {
    vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, f->query,
                        1);
    f->submitted = true;
  }
}
af_ray_stats af_vk_ray_query_stats(const af_vk_ray_query *r) {
  return r ? r->stats : (af_ray_stats){0};
}
