# 0.24.0: Scalability, Vulkan suballocation, multithreading and streaming audio

Afterlight 0.24.0 removes the core scalability bottlenecks across rendering,
physics, scene cloning, CPU threading, audio and build layout while keeping
strict C17 cleanliness and zero new third-party dependencies.

## Vulkan memory suballocator and modular renderer

All Vulkan buffer and image allocations now route through a single pooled block
suballocator (`vk_memory.c`) with separate buffer/image heaps respecting
`bufferImageGranularity` and `nonCoherentAtomSize`, persistent host mappings,
and coalescing free spans. Static geometry uses `DEVICE_LOCAL` allocations with
zero-copy writes on unified memory and staged transfer copies on discrete VRAM
(exercisable via `AFTERLIGHT_FORCE_STAGING_UPLOAD=1`).

The monolithic `renderer_vulkan.c` and its `#include`d implementation headers
have been split into focused translation units (`renderer_device_vulkan.c`,
`renderer_mesh_vulkan.c`, `renderer_cluster_vulkan.c`,
`renderer_assets_vulkan.c`, and `renderer_vulkan.c`), and the root
`CMakeLists.txt` is modularized under `cmake/`. The concurrent streamed cluster
asset limit (`AF_CLUSTER_STREAM_ASSETS`) increases from 8 to 256.

## 128-light tiled forward shading and custom material shaders

Local point/spot light capacity increases from 16 to 128
(`AF_SCENE_MAX_LOCAL_LIGHTS`). Lights are uploaded in a per-frame storage buffer
(set 0 binding 12) and culled on the CPU into 32×32 screen-space `uvec4` tile
bitmasks (set 0 binding 13).

Materials can reference named custom SPIR-V fragment shaders via
`af_material.shader` (persisted in scene v30 JSON) and
`af_engine_upload_material_shader` / `af_engine_remove_material_shader`. Custom
opaque and alpha-blended pipelines share the standard 5-set / 128-byte
push-constant interface and rebuild automatically across swapchain resizes.

## Dynamic physics pools, COW landscapes and deterministic jobs

`af_physics` replaces fixed inline arrays (1,024 bodies / 8,192 contacts) with
dynamically growable pools (up to 16,384 bodies / 131,072 contacts) and an O(1)
body-ID hash index. Heavy `af_landscape` terrain and foliage chunk storage uses
atomic reference-counted copy-on-write during `af_scene_clone` and editor undo,
detaching lazily on `af_scene_get_landscape_mut`.

A new SDL3 worker thread pool (`engine/src/core/jobs.c`, configurable via
`AFTERLIGHT_JOB_THREADS`) deterministically parallelizes independent physics
constraint islands, PBF water particle passes, multi-instance animation pose
sampling, large morph-target vertex blending, and dirty terrain chunk mesh
builds.

## Compressed and disk-streaming audio with mix buses

Zero-dependency MPL-2.0 codecs (`engine/src/audio/audio_codec.c`) add QOA
(5× compression) and IMA-ADPCM WAV (4:1 compression) alongside 8/16/24/32-bit
PCM and 32-bit float WAV. `af_audio` now dynamically grows clip (up to 512 /
128 MiB), voice (up to 256), and stream (up to 32) pools, adds disk-streaming
music/ambience playback (`af_audio_stream_start` with no 30 s / 8 MiB ceiling),
exposes Master/SFX/Music/Ambience mix buses, and applies smooth soft-clipping.

## Compatibility and version reporting

The engine release is **0.24.0**, the scene format remains **30** (with an
optional backward-compatible `"shader"` field on materials), the C game module
ABI remains **30**, and the engine extension ABI remains **1**.

## Verification

Verified on macOS (`arm64` / Apple M4) across Debug (110/110 non-graphics tests
including `afterlight.dco`, `afterlight.licensing`, `afterlight.jobs`,
`afterlight.dynamic_pools_and_cow`, and `afterlight.audio`), Release (all 353
targets), ASan+UBSan (110/110 non-graphics tests with zero sanitizer errors),
and 15/15 live Vulkan/MoltenVK graphics tests with `VK_LAYER_VALIDATE_SYNC=1`
(including `afterlight.vk_memory_and_materials` and `afterlight.vk_memory_staged`).
Windows verification remains deferred.
