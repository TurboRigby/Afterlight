/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef AF_NUKLEAR_CONFIG_H
#define AF_NUKLEAR_CONFIG_H
#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_KEYSTATE_BASED_INPUT
#include <stdio.h>
#include <string.h>
static inline char *ed_nuklear_number(char *buffer, double value);
#define NK_DTOA ed_nuklear_number
#define NK_MAX_FLOAT_PRECISION 3
#define NK_UINT_DRAW_INDEX
#include "nuklear.h"
/* Round numeric controls instead of truncating bias values such as 0.015. */
static inline char *ed_nuklear_number(char *buffer, double value) {
    int n = snprintf(buffer, NK_MAX_NUMBER_BUFFER, "%.3f", value);
    if (n < 0 || n >= NK_MAX_NUMBER_BUFFER) {
        snprintf(buffer, NK_MAX_NUMBER_BUFFER, "%.3g", value);
        return buffer;
    }
    char *decimal = strchr(buffer, '.');
    if (decimal) {
        while (n > 0 && buffer[n - 1] == '0')
            buffer[--n] = 0;
        if (n > 0 && buffer[n - 1] == '.')
            buffer[--n] = 0;
    }
    return buffer;
}
#endif
