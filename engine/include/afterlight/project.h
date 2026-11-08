/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef AFTERLIGHT_PROJECT_H
#define AFTERLIGHT_PROJECT_H
#include <afterlight/asset.h>
#define AF_PROJECT_VERSION 1
/* In-memory references are absolute. Files store references relative to the
   project when possible; moving a self-contained project preserves its paths. */
typedef struct af_project {
    char name[128];
    char startup_scene[AF_ASSET_PATH_CAPACITY];
    char game_module[AF_ASSET_PATH_CAPACITY];
} af_project;
bool af_project_load(const char *path, af_project *out, af_asset_error *error);
bool af_project_save(const char *path, const af_project *project, af_asset_error *error);
#endif
