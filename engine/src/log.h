/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef AF_LOG_H
#define AF_LOG_H
#include <stdarg.h>
#include <stdio.h>
static inline void af_log(const char *level, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    fprintf(stderr, "[afterlight][%s] ", level);
    vfprintf(stderr, format, arguments);
    fputc('\n', stderr);
    va_end(arguments);
}
#endif
