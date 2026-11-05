/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef AFTERLIGHT_SIMULATION_H
#define AFTERLIGHT_SIMULATION_H
#include <stdbool.h>
#include <stdint.h>
#define AF_FIXED_DELTA (1.0 / 60.0)
#define AF_FIXED_MAX_STEPS 8u
typedef struct af_fixed_clock {
    double accumulator,alpha,dropped_seconds;
    uint64_t ticks;
    unsigned steps;
} af_fixed_clock;
/* Drops excess elapsed time rather than accumulating unbounded catch-up work.
   A paused single step advances exactly one tick and presents the current pose. */
unsigned af_fixed_clock_advance(af_fixed_clock *clock,double elapsed,bool paused,bool single_step);
#endif
