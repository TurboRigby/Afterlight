/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef AF_EDITOR_INSPECTOR_H
#define AF_EDITOR_INSPECTOR_H

#include "core/editor_state.h"

#ifdef __cplusplus
extern "C" {
#endif

void local_light_controls(editor *app);
void collider_controls(editor *app, af_scene_object object);
void water_controls(editor *app, af_scene_object object);
void part_controls(editor *app, af_scene_object object);
void joint_controls(editor *app, af_scene_object object);
void body_controls(editor *app, af_scene_object object);
void gameplay_controls(editor *app, af_scene_object object);
void thumbnail_widget(editor *app, const af_scene_asset *asset, const af_material *material);
void asset_entry(editor *app, const af_scene_asset *asset);
void inspector(editor *app, float width, float height);

#ifdef __cplusplus
}
#endif

#endif /* AF_EDITOR_INSPECTOR_H */
