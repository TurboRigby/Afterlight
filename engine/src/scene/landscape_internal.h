/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef AFTERLIGHT_LANDSCAPE_INTERNAL_H
#define AFTERLIGHT_LANDSCAPE_INTERNAL_H

#include <afterlight/landscape.h>
#include <afterlight/scene.h>
#include "cJSON.h"

bool ls_decode(af_scene *scene, const cJSON *root);
bool ls_encode(const af_scene *scene, cJSON *root);

#endif /* AFTERLIGHT_LANDSCAPE_INTERNAL_H */
