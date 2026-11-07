/* SPDX-License-Identifier: MPL-2.0 */
#ifndef AF_RAY_QUERY_VULKAN_H
#define AF_RAY_QUERY_VULKAN_H
#include <afterlight/mesh.h>
#include <vulkan/vulkan.h>
typedef struct af_vk_ray_query af_vk_ray_query;
typedef struct af_ray_instance {
  uint64_t mesh_key;
  VkBuffer vertices, indices;
  uint32_t vertex_count, index_count;
  af_mat4 world;
} af_ray_instance;
typedef struct af_ray_stats {
  uint64_t bytes, blas_builds, tlas_builds;
  size_t instances, meshes;
  double gpu_build_ms;
} af_ray_stats;
af_vk_ray_query *af_vk_ray_query_create(VkPhysicalDevice physical,
                                        VkDevice device, unsigned frames,
                                        uint32_t timestamp_bits,
                                        float timestamp_period);
/* The caller must retire GPU work before destruction. */
void af_vk_ray_query_destroy(af_vk_ray_query *r);
/* Called after this frame's fence, before acquiring a presentation image. */
bool af_vk_ray_query_prepare(af_vk_ray_query *r, unsigned frame,
                             const af_ray_instance *instances, size_t count,
                             VkDescriptorSet descriptor);
void af_vk_ray_query_record(af_vk_ray_query *r, unsigned frame,
                            VkCommandBuffer command);
af_ray_stats af_vk_ray_query_stats(const af_vk_ray_query *r);
#endif
