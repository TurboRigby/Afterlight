/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef AF_PLAYER_BUNDLE_H
#define AF_PLAYER_BUNDLE_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Magic markers for bundle format */
#define AF_BUNDLE_HEADER_MAGIC 0x454C444E55424641ULL /* "AFBUNDLE" */
#define AF_BUNDLE_FOOTER_MAGIC1 0x5F544C5245544641ULL /* "AFTERLT_" */
#define AF_BUNDLE_FOOTER_MAGIC2 0x5F454C444E55425FULL /* "_BUNDLE_" */

/* If the executable contains an embedded Mach-O/ELF section, linked memory
   buffer, or trailing appended bundle, extracts it to a temporary directory
   and copies the path to out_dir. Returns true on success, false if not bundled. */
bool af_bundle_extract(char *out_dir, size_t out_dir_capacity);

/* Removes the extracted bundle temporary directory. Also registered with atexit. */
void af_bundle_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif
