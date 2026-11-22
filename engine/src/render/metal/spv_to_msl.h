/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef AF_SPV_TO_MSL_H
#define AF_SPV_TO_MSL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum af_msl_stage {
    AF_MSL_STAGE_VERTEX = 0,
    AF_MSL_STAGE_FRAGMENT = 1,
    AF_MSL_STAGE_COMPUTE = 2
} af_msl_stage;

typedef enum af_msl_profile {
    AF_MSL_PROFILE_SCENE = 0,
    AF_MSL_PROFILE_POST_UI = 1
} af_msl_profile;

/* Translates a SPIR-V shader binary to macOS Metal Shading Language (MSL 3.0)
 * using Afterlight's fixed resource binding table (metal_bindings.h).
 * On success, allocates a NUL-terminated MSL source string in *out_msl
 * (caller frees with free()). Returns false on error and optionally writes
 * a message into error_buf. */
bool af_spv_to_msl(const uint32_t *spirv, size_t word_count, af_msl_stage stage, af_msl_profile profile,
                   const char *entry_name, bool flip_vert_y, char **out_msl, char *error_buf, size_t error_capacity);

#endif
