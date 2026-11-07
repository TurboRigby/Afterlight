# Engine extensions

Afterlight and its game modules build as C17. An engine extension is a separate
native library that can use C, C++ or a vendor SDK and expose the C ABI in
[`extension.h`](../engine/include/afterlight/extension.h). Extensions and game
modules have independent ABI versions.

## Try the C++ telemetry extension

Build the extension separately with a C++17 compiler. The engine's regular
build does not need that compiler.

```sh
cmake -S engine/extensions/telemetry -B build/telemetry -G Ninja
cmake --build build/telemetry
```

On Linux, run the player from the engine checkout.

```sh
build/debug/afterlight_player \
  --project build/debug/playground.afterlight.project \
  --extension build/telemetry/afterlight_telemetry.so --frames 120
```

On macOS, the player executable is inside
`build/debug/afterlight_player.app/Contents/MacOS/`. The sample is also a
`.so` with CMake's macOS module defaults. On Windows, use the built `.dll`.
The editor accepts the same `--extension` option. Repeat it to load up to
eight libraries, in argument order.

## ABI and lifetime

- Load libraries you trust. They execute native code in the engine process.
  Loading is explicit through the CLI or `af_engine_load_extension` before
  the run loop. Relative paths use the process working directory.
- Export `af_extension_get_api` with C linkage and `AF_EXTENSION_EXPORT`.
  Define `AF_EXTENSION_BUILD` when compiling the module. Return a descriptor
  for the requested ABI or return null. The loader checks the version,
  descriptor size, required callbacks and unique extension ID.
- `start` runs after engine creation and before game initialization.
  `on_present` is optional and runs once per successful presentation.
  The host calls `stop` in reverse load order before renderer destruction.
  All callbacks run on the main thread.
- The library owns its state and releases it in `stop`. A failed `start`
  cleans up any partial state before returning. Keep the descriptor and ID
  valid through unload. Keep C++ exceptions and C++ types inside the module.

## Rendering integrations

The current ABI exposes lifecycle callbacks, logging and frame counters.
An upscaler needs Vulkan instance and device capability negotiation before
device creation and a render pass with scene color, depth, motion vectors,
jitter, exposure and history reset information. That pass needs explicit
image layout, synchronization and resource lifetime rules.

NVIDIA documents Streamline for Windows. NGX supplies a Vulkan API and Linux
libraries, including calls to query the Vulkan extensions a DLSS feature
requires. [Streamline programming guide](https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuide.md),
[NGX Vulkan header](https://github.com/NVIDIA/DLSS/blob/main/include/nvsdk_ngx_vk.h),
[NGX Linux libraries](https://github.com/NVIDIA/DLSS/tree/main/lib/Linux_x86_64).

AMD documents a C-facing FidelityFX API and C++ convenience wrappers.
Available effects and backends depend on the SDK version and platform.
[AMD FidelityFX API](https://gpuopen.com/manuals/fidelityfx_sdk/getting-started/ffx-api/),
[AMD FSR SDK](https://gpuopen.com/amd-fsr-sdk/).
