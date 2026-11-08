/* SPDX-License-Identifier: MPL-2.0 */
#include <SDL3/SDL.h>
#include <afterlight/afterlight.h>
#include <stdio.h>
int main(void) {
    af_engine_config c = af_engine_config_default();
    c.width = 400;
    c.height = 300;
    c.enable_validation = true;
    c.title = "Afterlight allocation cleanup";
    af_engine *e = af_engine_create(&c);
    if (e) {
        af_engine_destroy(e);
        fputs("Injected allocation unexpectedly succeeded.\n", stderr);
        return 1;
    }
    SDL_unsetenv_unsafe("AFTERLIGHT_TEST_POST_FAIL_AT");
    e = af_engine_create(&c);
    if (!e)
        return 1;
    af_engine_stats stats = af_engine_get_stats(e);
    af_engine_destroy(e);
    if (stats.validation_errors)
        return 1;
    puts("Post allocation failure cleans partial resources and allows a fresh "
         "engine in the same process.");
    return 0;
}
