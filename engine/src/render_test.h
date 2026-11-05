/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef AF_RENDER_TEST_H
#define AF_RENDER_TEST_H
#include <afterlight/afterlight.h>
/* Private regression hook: copies one rendered pixel before presentation.
   Requests use logical window coordinates; read after a subsequent draw. */
bool af_engine_test_capture_pixel(af_engine *engine,af_vec2 position);
bool af_engine_test_read_pixel(af_engine *engine,unsigned char rgba[4]);
/* Native focus may be lost and regained within one SDL event batch. */
uint64_t af_engine_test_focus_losses(const af_engine *engine);
#endif
