/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef AF_EDITOR_HIERARCHY_H
#define AF_EDITOR_HIERARCHY_H

#include "editor_state.h"

#ifdef __cplusplus
extern "C" {
#endif

void object_rows(editor *app, af_object_id parent, unsigned depth);
void object_list(editor *app, float height);

#ifdef __cplusplus
}
#endif

#endif /* AF_EDITOR_HIERARCHY_H */
