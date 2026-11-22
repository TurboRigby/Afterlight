# 0.25.0: Native Metal 3 backend, Apple Silicon ray tracing and MetalFX

Afterlight 0.25.0 introduces a full native Metal 3 rendering backend on macOS
with feature parity against the Vulkan backend, including Apple Silicon hardware
ray tracing, GPU compute cluster culling, runtime custom SPIR-V material
shaders, and native MetalFX upscaling, while keeping Linux and Windows pure C17
Vulkan builds unaffected.

## Dual-backend dispatch and Metal-only builds

Renderer calls now dispatch through a backend vtable (`af_renderer_ops` in
`engine/src/render/renderer_dispatch.c`) with shared backend-agnostic math,
tile binning, temporal hashes, and LOD helpers extracted into
`renderer_common.{h,c}`.

Applications can select a backend via `af_engine_config.backend`
(`AF_RENDER_BACKEND_AUTO`, `AF_RENDER_BACKEND_VULKAN`,
`AF_RENDER_BACKEND_METAL`) or override it at runtime with
`AFTERLIGHT_BACKEND=metal|vulkan`. On macOS, `AUTO` defaults to native Metal
unless Vulkan startup or upscaler extensions are loaded, and `af_engine_stats.active_backend`
reports the active backend.

CMake adds `AFTERLIGHT_ENABLE_METAL` (default `ON` on Apple platforms) and
`AFTERLIGHT_ENABLE_VULKAN` (default `ON`). Building with
`-DAFTERLIGHT_ENABLE_VULKAN=OFF -DAFTERLIGHT_ENABLE_METAL=ON` produces macOS
binaries and exports with zero Vulkan or MoltenVK runtime dependencies.

## Native Metal 3 renderer and shader pipeline

All engine shaders compile from the single GLSL source tree at build time via
`glslangValidator` -> `afterlight_spv2msl` (SPIRV-Cross C API with fixed slot
bindings and `--flip-vert-y`) -> `xcrun metal -std=metal3.0` -> `xcrun metallib`,
embedded into `generated/metal_shaders_lib.h`. Runtime custom SPIR-V material
shaders (`af_engine_upload_material_shader`) use the same `spv_to_msl.c`
translator and compile through `newLibraryWithSource:`, surviving window resizes
and swapchain rebuilds.

The Metal backend (`engine/src/render/metal/`) implements full parity:
PBR/Lambert/Unlit materials, normal and metallic-roughness maps, IBL and
skyboxes, cascaded directional shadows and 24-layer cached local shadows,
128-light 32×32 screen-tiled forward shading, alpha cutout/blend/split-blend,
two-pass water optics with refraction snapshots and underwater fog, skeletal
skinning, parallel morph blending, dynamic water/landscape chunk streaming,
foliage, prop instancing, frustum culling, HDR/bloom/FXAA/SMAA/TAA/color
grading/DoF/motion blur/debug views, Nuklear UI overlay, and pixel/region/motion
test readback.

## Hardware ray tracing, GPU cluster culling and MetalFX

- **Metal 3 Hardware Ray Tracing (`ray_query_metal.m`)**: Builds native
  `MTLAccelerationStructure` BLAS/TLAS structures using
  `MTLAccelerationStructureUserIDInstanceDescriptor`, `[MTLBuffer gpuAddress]`
  surface records for hit shading, and explicit `useResource:` residency. On
  Apple Silicon this unlocks hardware ray-traced shadows, ray water reflections,
  glossy/transmission effects, RTAO, and multi-bounce RTGI with spatiotemporal
  denoising.
- **GPU Cluster Culling (`cluster_icb.metal`, `renderer_cluster_metal.m`)**:
  Compute-driven frustum culling writes `MTLDrawIndexedPrimitivesIndirectArguments`
  indirect draw buffers alongside region LOD and byte-budgeted LRU streaming
  residency.
- **Native MetalFX (`upscaler_metal.m`)**: Integrates `MTLFXTemporalScaler`
  (with `MTLFXSpatialScaler` fallback) directly behind the existing
  `af_engine_set_upscaler_enabled` / `af_engine_set_upscaler_quality` API (or
  `AFTERLIGHT_METALFX=1`).

## Packaging, shutdown fix and compatibility

- Fixed a shutdown ordering race in `af_engine_destroy` by shutting down the
  global `af_jobs` thread pool before `SDL_Quit()`.
- Updated `scripts/package_source.py`, `tests/tooling/licensing.py`, and
  `scripts/afterlight.py` for `.m`/`.metal` MPL-2.0 source archiving, universal
  Mach-O `otool -L` handling, `@rpath` inheritance, and optional MoltenVK
  bundling for Metal-only exports.
- The engine release is **0.25.0**, the scene format remains **30**, the C game
  module ABI remains **30**, and the engine extension ABI remains **1**.

## Verification

Verified on macOS (`arm64` / Apple M4) across Debug, Release, and Metal-only
(`AFTERLIGHT_ENABLE_VULKAN=OFF`) builds with zero compiler warnings:

- 110/110 non-graphics CPU tests (including `afterlight.dco` and
  `afterlight.licensing`).
- 79/79 non-packaging graphics tests on native Metal (including the new
  `afterlight.metal_backend` test covering 100 tiled local lights, runtime
  SPIR-V -> MSL custom shaders, MetalFX upscaling, and Metal 3 hardware
  ray-traced shadows), plus Metal API Validation (`MTL_DEBUG_LAYER=1`) checks
  across smoke, foundations, ray shadows/reflections/RTGI, clusters, and
  `afterlight.metal_backend`.
- 3/3 packaging/export checks (`afterlight.licensing`, `afterlight.ray_export`,
  `afterlight.bundle_export`).
- 10/10 Vulkan parity tests with `AFTERLIGHT_BACKEND=vulkan`.
