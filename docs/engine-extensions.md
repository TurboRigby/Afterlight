# Engine extensions

Afterlight and its game modules build as C17. An engine extension is a separate
native library that can use C, C++ or a vendor SDK and expose the C ABI in
[`extension.h`](../engine/include/afterlight/extension.h). Extensions and game
modules have independent ABI versions.

## Enable extensions in the editor

Open **Extensions → Load extension** from the top toolbar and choose a trusted
native library. The Linux installation opens its bundled extensions directory.
The panel lists loaded extensions and lets you disable or re-enable
each one during the current session. The **Upscaler active** label confirms the
selected provider is rendering. Disabling it restores the scene's built-in
anti-aliasing and native resolution. You can also load libraries at launch with
`--extension` in the editor or standalone player.

Libraries that need Vulkan startup negotiation show **Restart required** when
loaded in a running editor. Close and reopen the editor to activate the queued
library. The editor remembers one startup extension per user. Use **Forget
startup extension** to clear it for future launches. Explicit `--extension`
arguments take precedence over the saved selection.

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
  pixels, exposure multiplier and the history-reset flag. An optional tail
  supplies camera clip planes, vertical field of view and frame time. Resizing recreates
  the provider context after device idle. Camera cuts and scene changes reset
  temporal history. Spatial providers can request zero projection jitter with
  `AF_VULKAN_UPSCALER_SPATIAL`.
- A loaded provider starts enabled and takes the place of built-in AA.
  `af_engine_set_upscaler_enabled` switches back to native resolution and
  the scene's AA setting. If the provider declines a frame, the renderer
  composites its current scene color and restores native rendering on the
  next frame. A failed resource setup also restores native rendering.
- A provider can publish quality presets through the optional tail of
  `af_vulkan_upscaler_api`: names, render scales and a count. The first preset
  is the default and `af_engine_set_upscaler_quality` selects another one.
  The engine passes the selected index in `af_vulkan_upscaler_config.quality`
  and rebuilds its render targets when the render scale changes. Providers
  without presets keep their fixed `render_scale`; query the count first.

An extension can optionally declare a `vulkan_startup` descriptor to request
Vulkan instance and device extensions and the supported feature bits documented
in the header. Pass those libraries to `af_engine_create_with_extensions` or
`--extension` so the callbacks can run before Vulkan creation. If the selected
device cannot meet the requirements, Afterlight leaves the extension disabled
and starts with its built-in renderer.

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

## AMD FidelityFX Super Resolution 2 and 3

The optional Linux Vulkan adapters use the FSR 2.3.3 and FSR 3.1.4 temporal
upscalers from AMD's FidelityFX SDK 1.1.4. Both use Quality mode at a 1.5×
ratio, with HDR scene color, depth, motion vectors and automatic exposure.
Tone mapping, bloom and UI composition run at output resolution. The FSR 3
module provides Super Resolution. Frame generation requires separate
swapchain and presentation integration.
[AMD FidelityFX SDK 1.1.4](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/tree/v1.1.4)

The standalone build needs a C++20 compiler, Python 3, Vulkan 1.2 headers,
`glslangValidator` and `spirv-dis`. It compiles AMD's GLSL passes for HDR and
render-resolution motion vectors. It uses FP32 shaders on the device's
native subgroup size. Keep the SDK checkout external to Afterlight.

```sh
git clone --depth 1 --branch v1.1.4 --filter=blob:none --sparse \
  https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK.git ../FidelityFX-SDK
git -C ../FidelityFX-SDK sparse-checkout set sdk/include sdk/src
cmake -S engine/extensions/amd-fsr -B build/amd-fsr -G Ninja \
  -DAFTERLIGHT_FIDELITYFX_SDK="$PWD/../FidelityFX-SDK"
cmake --build build/amd-fsr
```

Choose `afterlight_fsr2.so` or `afterlight_fsr3.so` under **Extensions →
Load extension**, then restart the editor. Both request
storage-image features at Vulkan startup. The standalone player accepts
the same libraries with `--extension`. If you pass both, the last upscaler
starts active and the editor panel can switch between them. Keep
`AMD-FIDELITYFX-LICENSE.txt` with the built modules when sharing them.

## NVIDIA NGX DLSS Super Resolution

The optional C adapter uses NVIDIA's native NGX Vulkan API on Linux x86-64
with a supported NVIDIA RTX GPU and driver. It runs DLSS Super Resolution
in Quality mode, rendering at roughly two thirds of the output width and
height. It uses HDR scene color, depth, motion vectors and temporal jitter.
Tone mapping and editor UI composition run at output resolution. If NGX
cannot initialize or evaluate, Afterlight restores the scene's built-in AA
and native resolution. [NVIDIA DLSS SDK v310.9.16](https://github.com/NVIDIA/DLSS/tree/v310.9.16)

Keep the SDK in a separate directory. Its headers and binaries are provided
under [NVIDIA's SDK license](https://github.com/NVIDIA/DLSS/blob/v310.9.16/LICENSE.txt)
and remain external to the Afterlight repository and standard exports.

```sh
git clone --depth 1 --branch v310.9.16 https://github.com/NVIDIA/DLSS.git ../NVIDIA-DLSS
cmake -S engine/extensions/nvidia-ngx -B build/nvidia-ngx -G Ninja \
  -DAFTERLIGHT_NGX_SDK="$PWD/../NVIDIA-DLSS"
cmake --build build/nvidia-ngx
```

Select `afterlight_ngx.so` in **Extensions → Load extension**,
then restart the editor. For the standalone player, supply the module with
`--extension build/nvidia-ngx/afterlight_ngx.so`. The module looks for NVIDIA's
DLSS runtime in the SDK's `lib/Linux_x86_64/rel` directory. Set
`AFTERLIGHT_NGX_RUNTIME_DIR` to use a different runtime directory.

The module publishes **Quality**, **Balanced**, **Performance** and
**Ultra Performance** presets. Quality is the default. The standalone player's
pause/settings menu shows a **DLSS quality** selector when the module is
loaded, and the same menu has a **Ray tracing** switch that enables or disables
ray-traced sun shadows and water reflections together. The choices persist in
the player's preferences alongside anti-aliasing and exposure.

## Other SDKs

AMD documents a C-facing FidelityFX API and C++ convenience wrappers.
Available effects and backends depend on the SDK version and platform.
[AMD FidelityFX API](https://gpuopen.com/manuals/fidelityfx_sdk/getting-started/ffx-api/),
[AMD FSR SDK](https://gpuopen.com/amd-fsr-sdk/).
