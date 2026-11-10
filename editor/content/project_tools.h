/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef AF_EDITOR_PROJECT_TOOLS_H
#define AF_EDITOR_PROJECT_TOOLS_H

#include "core/editor_state.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { ED_TOOL_NEW = 1, ED_TOOL_EXPORT = 2, ED_TOOL_BUNDLE = 3 };

void read_startup_extension(editor *app);
bool is_startup_extension_scheduled(const editor *app, const char *path);
void schedule_startup_extension(editor *app, const char *path, bool enable);
void schedule_startup_upscaler(editor *app, const char *path, bool enable);
void add_editor_extension(ext_discovery *d, const char *path);
void add_scheduled_startup_extensions(ext_discovery *d, const char *startup_extension);
void discover_editor_ext_in_dir(const char *dir, ext_discovery *d);

void poll_project_tool(editor *app);
void start_project_tool(editor *app, int mode);
void project_panel(editor *app, float width, float height);
void extensions_panel(editor *app, float width, float height);

#ifdef __cplusplus
}
#endif

#endif /* AF_EDITOR_PROJECT_TOOLS_H */
