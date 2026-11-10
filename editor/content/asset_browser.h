/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef ED_ASSET_BROWSER_H
#define ED_ASSET_BROWSER_H
#include "document/document.h"
/* kind=-1 matches every kind; ASCII case-insensitive name/path search. */
bool ed_asset_matches(const af_scene_asset *asset, const char *query, int kind);
bool ed_document_assign_asset(ed_document *document, const char *name);
#endif
