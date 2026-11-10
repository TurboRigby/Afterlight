/* SPDX-License-Identifier: MPL-2.0 */
#ifndef AF_BLUEPRINT_IO_H
#define AF_BLUEPRINT_IO_H
#include "cJSON.h"
#include <afterlight/blueprint.h>

bool af_scene_encode_blueprints(const af_scene *, cJSON *);
bool af_scene_decode_blueprints(af_scene *, const cJSON *);

#endif
