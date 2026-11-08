/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef AFTERLIGHT_SHADOW_H
#define AFTERLIGHT_SHADOW_H
#include <stdbool.h>
#include <stdint.h>
typedef enum af_shadow_mode { AF_SHADOW_SINGLE = 1, AF_SHADOW_CASCADED = 4 } af_shadow_mode;
typedef struct af_shadow_settings {
    bool enabled;
    af_shadow_mode mode;
    uint32_t resolution; /* 512, 1024, 2048 or 4096 per map. */
    float distance;
    float constant_bias, slope_bias, normal_bias;
    uint32_t filter_radius; /* 0=hard, 1=3x3 PCF, 2=5x5 PCF. */
    float split_lambda, blend_fraction;
    bool debug_cascades;
} af_shadow_settings;
af_shadow_settings af_shadow_settings_default(void);
bool af_shadow_settings_valid(af_shadow_settings settings);
#endif
