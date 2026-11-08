/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "vulkan_device_policy.h"
#include <stdio.h>

#define CHECK(condition)                                                                                               \
    do {                                                                                                               \
        if (!(condition)) {                                                                                            \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #condition);                            \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
int main(void) {
    VkDriverId other = (VkDriverId)0;
    VkPhysicalDeviceType integrated = VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
    VkPhysicalDeviceType discrete = VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
    VkPhysicalDeviceType cpu = VK_PHYSICAL_DEVICE_TYPE_CPU;
    CHECK(af_vulkan_device_score(integrated, VK_DRIVER_ID_MOLTENVK, true) >
          af_vulkan_device_score(integrated, other, true));
    CHECK(af_vulkan_device_score(integrated, VK_DRIVER_ID_MOLTENVK, true) >
          af_vulkan_device_score(discrete, other, true));
    CHECK(af_vulkan_device_score(integrated, VK_DRIVER_ID_MOLTENVK, false) ==
          af_vulkan_device_score(integrated, other, false));
    CHECK(af_vulkan_device_score(discrete, other, false) > af_vulkan_device_score(integrated, other, false));
    CHECK(af_vulkan_device_score(integrated, other, true) > af_vulkan_device_score(cpu, other, true));
    puts("Vulkan driver selection: macOS MoltenVK preference and hardware fallback passed.");
    return 0;
}
