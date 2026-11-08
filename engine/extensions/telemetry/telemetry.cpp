/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <afterlight/extension.h>
#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <new>

namespace {
using clock_type = std::chrono::steady_clock;

struct telemetry {
    const af_extension_host *host;
    clock_type::time_point started;
    uint64_t frames = 0;
};
} // namespace

extern "C" {
static int32_t start(const af_extension_host *host, void **state) {
    if (host->abi_version != AF_EXTENSION_ABI_VERSION || host->struct_size < sizeof(*host) || !host->log)
        return 1;
    *state = new (std::nothrow) telemetry{host, clock_type::now()};
    if (!*state)
        return 2;
    host->log("Telemetry started.");
    return 0;
}

static void present(void *state, const af_extension_frame *) {
    auto *sample = static_cast<telemetry *>(state);
    if (++sample->frames == 1)
        sample->host->log("Telemetry observed the first presentation.");
}

static void stop(void *state) {
    auto *sample = static_cast<telemetry *>(state);
    const std::chrono::duration<double> duration = clock_type::now() - sample->started;
    char message[128];
    std::snprintf(message, sizeof(message), "Telemetry stopped after %" PRIu64 " presentations in %.2f seconds.",
                  sample->frames, duration.count());
    sample->host->log(message);
    delete sample;
}
}

AF_EXTENSION_EXPORT const af_extension_api *af_extension_get_api(uint32_t abi_version) {
    static const af_extension_api api = {
        AF_EXTENSION_ABI_VERSION, sizeof(af_extension_api), "afterlight.telemetry", start, present, stop,
    };
    return abi_version == AF_EXTENSION_ABI_VERSION ? &api : nullptr;
}
