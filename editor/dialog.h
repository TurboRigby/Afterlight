/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef AF_EDITOR_DIALOG_H
#define AF_EDITOR_DIALOG_H
#include <afterlight/afterlight.h>

typedef struct ed_file_dialog ed_file_dialog;
/* A request owns its callback state independently of the editor's lifetime. */
ed_file_dialog *ed_dialog_begin(af_engine *engine, bool save, const char *suggested_path);
ed_file_dialog *ed_dialog_begin_asset(af_engine *engine,af_asset_kind kind);
ed_file_dialog *ed_dialog_begin_module(af_engine *engine);
ed_file_dialog *ed_dialog_begin_extension(af_engine *engine);
ed_file_dialog *ed_dialog_begin_project(af_engine *engine,bool save,const char *path);
/* 0 pending, 1 selected, 2 canceled, -1 error. Result strings are copied. */
int ed_dialog_poll(ed_file_dialog *dialog, char *path, size_t path_capacity, char *error, size_t error_capacity);
void ed_dialog_release(ed_file_dialog *dialog);
#endif
