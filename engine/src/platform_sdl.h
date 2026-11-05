/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef AF_PLATFORM_SDL_H
#define AF_PLATFORM_SDL_H
#include <afterlight/afterlight.h>
#include <SDL3/SDL.h>
/* Private bridge for the editor's platform dialogs; absent from public API. */
SDL_Window *af_engine_sdl_window(af_engine *engine);
#endif
