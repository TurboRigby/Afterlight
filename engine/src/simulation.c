/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <afterlight/simulation.h>
#include <math.h>
unsigned af_fixed_clock_advance(af_fixed_clock *clock, double elapsed, bool paused, bool single_step) {
    if (!clock)
        return 0;
    clock->steps = 0;
    if (!isfinite(elapsed) || elapsed < 0)
        elapsed = 0;
    if (paused) {
        clock->accumulator = 0;
        clock->alpha = 1;
        clock->steps = single_step ? 1u : 0u;
    } else {
        double accepted = fmin(elapsed, 0.25);
        clock->dropped_seconds += elapsed - accepted;
        clock->accumulator += accepted;
        while (clock->accumulator + 1e-12 >= AF_FIXED_DELTA && clock->steps < AF_FIXED_MAX_STEPS) {
            clock->accumulator -= AF_FIXED_DELTA;
            ++clock->steps;
        }
        if (clock->accumulator >= AF_FIXED_DELTA) {
            double dropped = floor(clock->accumulator / AF_FIXED_DELTA) * AF_FIXED_DELTA;
            clock->accumulator -= dropped;
            clock->dropped_seconds += dropped;
        }
        clock->accumulator = fmax(0, clock->accumulator);
        clock->alpha = fmin(clock->accumulator / AF_FIXED_DELTA, 1);
    }
    clock->ticks += clock->steps;
    return clock->steps;
}
