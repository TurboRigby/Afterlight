/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef AF_VULKAN_DEVICE_POLICY_H
#define AF_VULKAN_DEVICE_POLICY_H
#include <stdbool.h>
#include <vulkan/vulkan.h>

/* Several ICDs can expose the same Mac GPU. Prefer the macOS backend we
   support, rather than letting loader enumeration order choose the driver. */
static inline int af_vulkan_device_score(VkPhysicalDeviceType type, VkDriverId driver, bool macos)
{
    int score = 10;
    if (type == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) score = 200;
    else if (type == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) score = 100;
    else if (type == VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU) score = 50;
    else if (type == VK_PHYSICAL_DEVICE_TYPE_CPU) score = 1;
    if (macos && driver == VK_DRIVER_ID_MOLTENVK) score += 1000;
    return score;
}
#endif
