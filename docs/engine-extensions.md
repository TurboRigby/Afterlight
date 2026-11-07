# Engine extensions

Afterlight and its game modules build as C17. An engine extension is a separate
native library that can use C, C++ or a vendor SDK and expose the C ABI in
[`extension.h`](../engine/include/afterlight/extension.h). Extensions and game
modules have independent ABI versions.

## Enable extensions in the editor

Open **Project → Extensions → Load extension** and choose a trusted native
library. The panel lists loaded extensions and lets you disable or re-enable
each one during the current session. The **Upscaler active** label confirms the
selected provider is rendering. Disabling it restores the scene's built-in
anti-aliasing and native resolution. You can also load libraries at launch with
`--extension` in the editor or standalone player.

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
  Loading is explicit through the editor, CLI or `af_engine_load_extension`
  at startup or during a game update. Relative paths use the process working
  directory.
- Export `af_extension_get_api` with C linkage and `AF_EXTENSION_EXPORT`.
  Define `AF_EXTENSION_BUILD` when compiling the module. Return a descriptor
  for the requested ABI or return null. The loader checks the version,
  descriptor size, required callbacks and unique extension ID.
- `start` runs when the library loads. `on_present` is optional and runs once
  per successful presentation while the extension is enabled.
  The host calls `stop` in reverse load order before renderer destruction.
  All callbacks run on the main thread.
- The library owns its state and releases it in `stop`. A failed `start`
  cleans up any partial state before returning. Keep the descriptor and ID
  valid through unload. Keep C++ exceptions and C++ types inside the module.

## Vulkan upscaler hook

An extension can provide an upscaler through the optional descriptor
field in [`extension_vulkan.h`](../engine/include/afterlight/extension_vulkan.h).
ABI 1 extensions with the original descriptor size still load. Only one
upscaler is enabled at a time. Enabling another switches providers.

Build the reference extension separately. It renders the scene at 75% of the
output width and height and uses a linear Vulkan blit to expand HDR color.
Tone mapping, bloom and UI composition run at their usual output sizes.

```sh
cmake -S engine/extensions/vulkan-blit -B build/vulkan-blit -G Ninja
cmake --build build/vulkan-blit
build/debug/afterlight_player --project build/debug/playground.afterlight.project \
  --extension build/vulkan-blit/afterlight_vulkan_blit.so
```

The hook runs after transparent geometry and water, before post-processing.
The provider records on the engine's graphics command buffer, outside a render
pass. Afterlight allocates the input and output images, synchronizes their use
and keeps their layouts as documented in the header. The provider can use the
completed frame slot for per-slot descriptors and temporary resources.

- HDR color is linear RGBA16F. The depth attachment uses standard Vulkan depth
  with far at 1. Transparent passes may leave opaque depth beneath their color.
- Motion RG contains previous UV minus current UV in full attachment
  coordinates. Multiply by the render extent for pixel units. Motion B is
  previous view-space depth or negative for invalid motion. Motion A contains
  current view-space depth.
- The frame also supplies the normalized viewport, projection jitter in render
  pixels, exposure multiplier and the history-reset flag. Resizing recreates
  the provider context after device idle. Camera cuts and scene changes reset
  temporal history. Spatial providers can request zero projection jitter with
  `AF_VULKAN_UPSCALER_SPATIAL`.
- A loaded provider starts enabled and takes the place of built-in AA.
  `af_engine_set_upscaler_enabled` switches back to native resolution and
  the scene's AA setting. If the provider declines a frame, the renderer
  composites its current scene color and restores native rendering on the
  next frame. A failed resource setup also restores native rendering.

The hook configures providers after the Vulkan device exists. SDKs that need
instance or device extensions require a separate pre-device negotiation step.

## AMD FidelityFX Super Resolution 1.0

The optional C++ adapter uses AMD's FSR 1.0.2 EASU and RCAS shaders at a fixed
Quality ratio of 1.5×. It encodes the HDR source into the range the spatial
filters expect and converts the result back to linear HDR before Afterlight's
bloom and tone mapping. The adapter uses the existing Vulkan device and
requires compute on the graphics queue.
[AMD FidelityFX Super Resolution source](https://github.com/GPUOpen-Effects/FidelityFX-FSR/tree/a21ffb8f6c13233ba336352bdff293894c706575)

```sh
git clone https://github.com/GPUOpen-Effects/FidelityFX-FSR.git
git -C FidelityFX-FSR checkout a21ffb8f6c13233ba336352bdff293894c706575
cmake -S engine/extensions/amd-fsr1 -B build/amd-fsr1 -G Ninja \
  -DAFTERLIGHT_FSR1_ROOT="$PWD/FidelityFX-FSR"
cmake --build build/amd-fsr1
```

Load `afterlight_fsr1.so` through the editor or pass its path to
`--extension`. Keep `AMD-FSR1-LICENSE.txt` with the built module when sharing it.

## Other SDKs

NVIDIA documents Streamline for Windows. NGX supplies a Vulkan API and Linux
libraries, including calls to query the Vulkan extensions a DLSS feature
requires. [Streamline programming guide](https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuide.md),
[NGX Vulkan header](https://github.com/NVIDIA/DLSS/blob/main/include/nvsdk_ngx_vk.h),
[NGX Linux libraries](https://github.com/NVIDIA/DLSS/tree/main/lib/Linux_x86_64).

AMD documents a C-facing FidelityFX API and C++ convenience wrappers.
Available effects and backends depend on the SDK version and platform.
[AMD FidelityFX API](https://gpuopen.com/manuals/fidelityfx_sdk/getting-started/ffx-api/),
[AMD FSR SDK](https://gpuopen.com/amd-fsr-sdk/).
