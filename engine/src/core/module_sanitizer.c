/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/* This object must be linked first in a macOS C game module. Its C destructor
   is then registered before user destructors and runs after them at dlclose.
   Apple Clang 21's generated __mod_term_func ASan destructor is not called on
   the tested dyld, leaving global redzones poisoned when VM addresses are reused.
   CMake replaces that destructor (not the instrumentation) with this equivalent
   compiler-rt image-unregistration call, registered through C's working atexit path. */
typedef int af_module_sanitizer_translation_unit;
#if defined(__APPLE__) && defined(__clang__)
#if __has_feature(address_sanitizer)
#include <stdint.h>
/* Compiler-rt ABI used by Clang's own module destructor. */
extern void __asan_unregister_image_globals(uintptr_t *flag);
static uintptr_t af_image_registered = 1;
__attribute__((destructor)) static void af_unregister_image(void) {
    __asan_unregister_image_globals(&af_image_registered);
}
#endif
#endif
