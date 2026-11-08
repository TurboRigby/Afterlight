/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef AFTERLIGHT_ASSET_WATCH_H
#define AFTERLIGHT_ASSET_WATCH_H
#include <afterlight/afterlight.h>
typedef struct af_asset_watch af_asset_watch;
typedef struct af_asset_watch_stats {
    unsigned reloads, rejections;
    size_t sources;
} af_asset_watch_stats;
af_asset_watch *af_asset_watch_create(void);
void af_asset_watch_destroy(af_asset_watch *watch);
/* Poll stat fingerprints each 250 ms; require 350 ms stable input. Tracks glTF
   external buffers/images and groups selectors by source. CPU scene is untouched. */
void af_asset_watch_update(af_asset_watch *watch, af_engine *engine, const af_scene *scene, double delta,
                           af_asset_error *message);
af_asset_watch_stats af_asset_watch_get_stats(const af_asset_watch *watch);
#endif
