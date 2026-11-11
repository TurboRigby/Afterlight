/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef AFTERLIGHT_VERSION_H
#define AFTERLIGHT_VERSION_H

/* Release version; scene formats and module ABIs are versioned separately. */
#define AF_VERSION_MAJOR 0
#define AF_VERSION_MINOR 23
#define AF_VERSION_PATCH 0

#define AF_VERSION_TEXT_(value) #value
#define AF_VERSION_TEXT(value) AF_VERSION_TEXT_(value)
#define AF_VERSION_STRING                                                                                              \
    AF_VERSION_TEXT(AF_VERSION_MAJOR) "." AF_VERSION_TEXT(AF_VERSION_MINOR) "." AF_VERSION_TEXT(AF_VERSION_PATCH)

#endif
