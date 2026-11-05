/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <stddef.h>
#include <string.h>
#if __has_feature(address_sanitizer)
#include <sanitizer/asan_interface.h>
#endif
static int *destructor_result;
__attribute__((visibility("default"))) const char *af_test_module_name(void){return "Afterlight unload regression";}
__attribute__((visibility("default"))) void af_test_module_report(int *result){destructor_result=result;}
__attribute__((destructor)) static void report_destructor(void){
    if(!destructor_result)return;
#if __has_feature(address_sanitizer)
    const char *name=af_test_module_name();*destructor_result=__asan_address_is_poisoned(name+strlen(name)+1);
#else
    *destructor_result=1;
#endif
}
