/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "spv_to_msl.h"
#include "metal_bindings.h"
#include <spirv_cross_c.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_error(char *buf, size_t cap, const char *msg) {
    if (buf && cap > 0)
        snprintf(buf, cap, "%s", msg ? msg : "unknown error");
}

static bool add_binding(spvc_compiler compiler, SpvExecutionModel model, unsigned set, unsigned binding,
                        unsigned msl_buf, unsigned msl_tex, unsigned msl_smp) {
    spvc_msl_resource_binding_2 b;
    spvc_msl_resource_binding_init_2(&b);
    b.stage = model;
    b.desc_set = set;
    b.binding = binding;
    b.count = 1;
    b.msl_buffer = msl_buf;
    b.msl_texture = msl_tex;
    b.msl_sampler = msl_smp;
    return spvc_compiler_msl_add_resource_binding_2(compiler, &b) == SPVC_SUCCESS;
}

bool af_spv_to_msl(const uint32_t *spirv, size_t word_count, af_msl_stage stage, af_msl_profile profile,
                   const char *entry_name, bool flip_vert_y, char **out_msl, char *error_buf, size_t error_capacity) {
    if (out_msl)
        *out_msl = NULL;
    if (!spirv || word_count < 5 || spirv[0] != 0x07230203u || !out_msl) {
        set_error(error_buf, error_capacity, "Invalid SPIR-V input.");
        return false;
    }

    spvc_context context = NULL;
    if (spvc_context_create(&context) != SPVC_SUCCESS || !context) {
        set_error(error_buf, error_capacity, "spvc_context_create failed.");
        return false;
    }

    spvc_parsed_ir ir = NULL;
    spvc_compiler compiler = NULL;
    spvc_compiler_options options = NULL;
    bool ok = false;

    if (spvc_context_parse_spirv(context, spirv, word_count, &ir) != SPVC_SUCCESS) {
        set_error(error_buf, error_capacity, spvc_context_get_last_error_string(context));
        goto cleanup;
    }
    if (spvc_context_create_compiler(context, SPVC_BACKEND_MSL, ir, SPVC_CAPTURE_MODE_TAKE_OWNERSHIP, &compiler) !=
        SPVC_SUCCESS) {
        set_error(error_buf, error_capacity, spvc_context_get_last_error_string(context));
        goto cleanup;
    }

    SpvExecutionModel model = stage == AF_MSL_STAGE_VERTEX    ? SpvExecutionModelVertex
                              : stage == AF_MSL_STAGE_COMPUTE ? SpvExecutionModelGLCompute
                                                              : SpvExecutionModelFragment;

    if (spvc_compiler_create_compiler_options(compiler, &options) != SPVC_SUCCESS) {
        set_error(error_buf, error_capacity, spvc_context_get_last_error_string(context));
        goto cleanup;
    }
    spvc_compiler_options_set_uint(options, SPVC_COMPILER_OPTION_MSL_PLATFORM, SPVC_MSL_PLATFORM_MACOS);
    spvc_compiler_options_set_uint(options, SPVC_COMPILER_OPTION_MSL_VERSION, SPVC_MAKE_MSL_VERSION(3, 0, 0));
    spvc_compiler_options_set_bool(options, SPVC_COMPILER_OPTION_FLIP_VERTEX_Y,
                                   (stage == AF_MSL_STAGE_VERTEX && flip_vert_y) ? SPVC_TRUE : SPVC_FALSE);
    if (spvc_compiler_install_compiler_options(compiler, options) != SPVC_SUCCESS) {
        set_error(error_buf, error_capacity, spvc_context_get_last_error_string(context));
        goto cleanup;
    }

    /* Push constants always live at buffer(AF_MTL_BUF_PUSH). */
    if (!add_binding(compiler, model, SPVC_MSL_PUSH_CONSTANT_DESC_SET, SPVC_MSL_PUSH_CONSTANT_BINDING, AF_MTL_BUF_PUSH,
                     0, 0)) {
        set_error(error_buf, error_capacity, "Failed to map push constant binding.");
        goto cleanup;
    }

    if (profile == AF_MSL_PROFILE_SCENE) {
        /* Set 0 buffers & acceleration structure. */
        bool mapped = add_binding(compiler, model, 0, 0, AF_MTL_BUF_FRAME, 0, 0) &&
                      add_binding(compiler, model, 0, 6, AF_MTL_BUF_SKIN, 0, 0) &&
                      add_binding(compiler, model, 0, 9, AF_MTL_BUF_MOTION, 0, 0) &&
                      add_binding(compiler, model, 0, 10, AF_MTL_BUF_ACCEL, 0, 0) &&
                      add_binding(compiler, model, 0, 11, AF_MTL_BUF_REFLECTION, 0, 0) &&
                      add_binding(compiler, model, 0, 12, AF_MTL_BUF_LIGHTS, 0, 0) &&
                      add_binding(compiler, model, 0, 13, AF_MTL_BUF_LIGHT_TILES, 0, 0) &&
                      /* Set 0..4 combined image samplers. */
                      add_binding(compiler, model, 0, 1, 0, AF_MTL_TEX_LOCAL_SHADOW, AF_MTL_TEX_LOCAL_SHADOW) &&
                      add_binding(compiler, model, 0, 2, 0, AF_MTL_TEX_ENV, AF_MTL_TEX_ENV) &&
                      add_binding(compiler, model, 0, 3, 0, AF_MTL_TEX_IRRADIANCE, AF_MTL_TEX_IRRADIANCE) &&
                      add_binding(compiler, model, 0, 4, 0, AF_MTL_TEX_BRDF, AF_MTL_TEX_BRDF) &&
                      add_binding(compiler, model, 0, 5, 0, AF_MTL_TEX_SKY, AF_MTL_TEX_SKY) &&
                      add_binding(compiler, model, 0, 7, 0, AF_MTL_TEX_REFRACT, AF_MTL_TEX_REFRACT) &&
                      add_binding(compiler, model, 0, 8, 0, AF_MTL_TEX_OPAQUE_DEPTH, AF_MTL_TEX_OPAQUE_DEPTH) &&
                      add_binding(compiler, model, 1, 0, 0, AF_MTL_TEX_BASE, AF_MTL_TEX_BASE) &&
                      add_binding(compiler, model, 2, 0, 0, AF_MTL_TEX_SHADOW, AF_MTL_TEX_SHADOW) &&
                      add_binding(compiler, model, 3, 0, 0, AF_MTL_TEX_NORMAL, AF_MTL_TEX_NORMAL) &&
                      add_binding(compiler, model, 4, 0, 0, AF_MTL_TEX_MR, AF_MTL_TEX_MR);
        if (!mapped) {
            set_error(error_buf, error_capacity, "Failed to map scene Metal bindings.");
            goto cleanup;
        }
    } else {
        bool mapped = add_binding(compiler, model, 0, 0, 0, AF_MTL_POST_TEX_0, AF_MTL_POST_TEX_0) &&
                      add_binding(compiler, model, 0, 1, 0, AF_MTL_POST_TEX_1, AF_MTL_POST_TEX_1) &&
                      add_binding(compiler, model, 0, 2, 0, AF_MTL_POST_TEX_2, AF_MTL_POST_TEX_2);
        if (!mapped) {
            set_error(error_buf, error_capacity, "Failed to map post/UI Metal bindings.");
            goto cleanup;
        }
    }

    if (entry_name && entry_name[0]) {
        if (spvc_compiler_rename_entry_point(compiler, "main", entry_name, model) != SPVC_SUCCESS) {
            set_error(error_buf, error_capacity, spvc_context_get_last_error_string(context));
            goto cleanup;
        }
    }

    const char *msl = NULL;
    if (spvc_compiler_compile(compiler, &msl) != SPVC_SUCCESS || !msl) {
        set_error(error_buf, error_capacity, spvc_context_get_last_error_string(context));
        goto cleanup;
    }

    size_t len = strlen(msl);
    char *copy = malloc(len + 1);
    if (!copy) {
        set_error(error_buf, error_capacity, "Out of memory.");
        goto cleanup;
    }
    memcpy(copy, msl, len + 1);
    *out_msl = copy;
    ok = true;

cleanup:
    spvc_context_destroy(context);
    return ok;
}
