/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <SDL3/SDL_loadso.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#if __has_feature(address_sanitizer)
#include <sanitizer/asan_interface.h>
#endif
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            fprintf(stderr, "Module unload %d: %s (%s)\n", __LINE__, #c, SDL_GetError());                              \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
int main(int argc, char **argv) {
    CHECK(argc == 2);
    for (unsigned iteration = 0; iteration < 16; ++iteration) {
        SDL_SharedObject *library = SDL_LoadObject(argv[1]);
        CHECK(library);
        SDL_FunctionPointer symbol = SDL_LoadFunction(library, "af_test_module_name");
        const char *(*name_fn)(void) = NULL;
        memcpy(&name_fn, &symbol, sizeof(name_fn));
        CHECK(name_fn);
        symbol = SDL_LoadFunction(library, "af_test_module_report");
        void (*report)(int *) = NULL;
        memcpy(&report, &symbol, sizeof(report));
        CHECK(report);
        int destructor_protected = 0;
        report(&destructor_protected);
#if __has_feature(address_sanitizer)
        const char *name = name_fn();
        uintptr_t redzone = (uintptr_t)(name + strlen(name) + 1);
        CHECK(__asan_address_is_poisoned((const void *)redzone));
#else
        CHECK(!strcmp(name_fn(), "Afterlight unload regression"));
#endif
        SDL_UnloadObject(library);
        CHECK(destructor_protected == 1);
#if __has_feature(address_sanitizer)
        CHECK(!__asan_address_is_poisoned((const void *)redzone));
#endif
    }
    puts("Module unload: sixteen load/unload cycles, protected user destructors and cleared sanitizer global metadata "
         "passed.");
    return 0;
}
