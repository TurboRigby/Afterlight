# 0.22: Ray tracing, landscape system, post-processing, and bundling

Afterlight 0.22 introduces optional hardware ray-traced sun shadows and water reflections,
an interactive landscape sculpting and painting system, spatially-partitioned GPU-instanced
foliage, a cinematic HDR post-processing pipeline, native extensions (AMD FSR, NVIDIA DLSS/NGX),
single-binary game bundling, and full undo/redo support.

## Try the comparison lab

```sh
cmake --preset debug
cmake --build --preset debug --target run_ray-shadow-lab_editor
```

Press **Play**. **Q** switches the scene's sun-shadow preference between maps and
hardware rays, **Space** freezes the moving arm, and **R** resets its motion.
Use the editor's usual right-mouse camera controls to inspect thin bars and contact
shadows. `run_ray-shadow-lab_player` runs the same lab without the editor. The HUD
reports the actual path. On unsupported hardware it reports raster maps.

For any scene, open **Environment → Ray-traced sun shadows (experimental)**.
The setting supports Undo/Save and is retained in exported scenes. **Performance**
reports actual activation, ray instance/mesh counts, retained acceleration-structure
bytes, build GPU time and the number of casters kept in directional shadow maps.
The engine defaults to raster shadows. The comparison lab opts into rays.

The tested Linux RTX 5060 exposes the required Vulkan features. The tested macOS
MoltenVK path does not expose them and uses maps. Windows remains unverified.

## What is traced

Visible, opaque, non-skinned rigid mesh instances enter a top-level acceleration
structure (TLAS), including off-screen casters and current parent/world transforms.
Each distinct uploaded mesh has a cached bottom-level structure (BLAS), shared by
its instances. Mirrored transforms work with rays. Non-invertible transforms stay
on the map path. A geometric-normal offset and minimum ray distance limit
self-intersection. Rays currently extend 10,000 world units toward the sun.

Skinned meshes and alpha-cutout casters retain their existing directional
shadow maps. The receiver shader combines ray visibility with the maps for
casters outside the TLAS.
Local point/spot shadows retain their full raster caster lists. Blended surfaces
follow the existing blended-material policy. Water can receive sun shadows.
The TLAS uses rigid geometry only.

`cast_shadows`, `receive_shadows`, unlit materials, inherited visibility and the
scene's master shadow toggle control participation. The traced contribution
uses hard edges and deterministic sampling. Ordinary TAA filters the result.

## Try water reflections

```sh
cmake --build --preset debug --target run_water-expedition_editor
```

The expedition requests hardware reflections for its pools and river. Use
**Environment → Ray-traced water reflections** to compare with the environment
map. Both ray options can run independently or together. **Performance** shows
which effects are active. The standalone player's pause/settings menu has a
**Ray tracing** switch that enables or disables both ray-traced effects together
and saves the choice in the player's preferences.

The water shader traces a single reflection ray from each eligible visible
surface sample. Ripple normals bend the ray, and Fresnel and roughness control
how the result blends into the water. Rays can hit off-screen opaque rigid
geometry up to 150 world units away. A miss samples the environment map. Rough
water blends toward that map. Unsupported devices also use the environment map.

Hit shading uses vertex and material base colors, emission, and an approximation
of sun and environment lighting. A reflection can therefore look simpler than
the directly rendered object. Skinned, cutout and translucent objects are
outside this first reflection path. The feature traces the upper surfaces of
pools and rivers. Droplets use environment lighting. Refraction remains a
separate optical pass.

Shadow and reflection rays share acceleration structures and use separate
instance masks. A rigid object can appear in reflections even when its material
disables shadow casting. Per-frame hit records contain material data and
addresses for the existing mesh buffers, so material edits can update the
reflected color without rebuilding the geometry.

## Resources and synchronization

The optional path requires Vulkan 1.2 plus `VK_KHR_acceleration_structure`,
`VK_KHR_deferred_host_operations`, `VK_KHR_ray_query`, and their required feature
bits. Raster rendering retains its Vulkan 1.1 floor. Devices without the optional
features load the ordinary SPIR-V shader, which has no RayQuery capability.

BLAS resources are created lazily when rays are requested. Each frame in flight
owns its TLAS, instances, scratch and timing queries. Its fence retires prior use
before CPU writes or reallocation. Changed instance lists/transforms rebuild that
frame's TLAS, while unchanged lists reuse it. Shared BLAS builds precede TLAS
builds, and build writes are made visible to fragment ray queries. Geometry
replacement/reimport invalidates the cache after GPU retirement. Disabling rays
releases retained ray resources. Mode switches can briefly wait while rebuilding
swapchain-dependent pipelines, like the existing AA configuration switches.

Ray AS storage, scratch and instance allocations are included in tracked GPU
memory and budget warnings. Limits are 65,536 instances, 4,096 cached meshes and
256 MiB per ray buffer, further constrained by queried device limits. This is a
first implementation caches full BLAS builds and uses the existing graphics
queue. A failed optional pipeline or ray resource allocation falls back to maps
for that engine session. Restarting retries the optional path.

## Compatibility

Scene format is **24** and requires Boolean `render.ray_traced_shadows`. Formats
1–23 still load with rays disabled. The optional Boolean
`render.ray_traced_water_reflections` defaults to off in existing scenes. Game module ABI is **25**. Rebuild modules and
restart old editor/player processes. The C host adds `get_render_settings` and
`get_stats` so game code can select an effect and report its actual availability.
Older progress files remain subject to the existing ABI compatibility check.

## Verification and timing

`ray_shadows` compares rendered shadow/lit pixels, verifies occlusion beyond a
one-unit raster cascade range on hardware, and checks rigid motion, hidden
casters, cutout/skinned map fallback, cast/receive flags, mesh replacement, parent
and mirrored transforms, resize, an empty TLAS, TAA, scene detach and resource
release. `ray_fallback` forces the capability off; `ray_allocation_failure` fails
a partial AS allocation; `ray_pipeline_fallback` rejects the optional pipeline.
All retain the raster scene. `water_ray_shadows` exercises water optics with
ray queries and TAA. `ray_lab` runs the C lab module. `ray_export`
removes source files, relocates the packaged game, then checks and runs it.

Use `AFTERLIGHT_REQUIRE_RAY_QUERY=1` for hardware-path checks on a capable native
desktop. Forced-capability fallback checks must run without that requirement.
The Linux hardware checks use the same GNOME Wayland / RTX 5060 desktop as 0.21,
with Vulkan synchronization validation. macOS covers the unsupported-device path.
Sanitizer GPU checks use ASan/UBSan with leak detection disabled.

Run the Release comparison with

```sh
build/release/afterlight_ray_shadow_benchmark --benchmark
```

This runs without validation, averages 69 delayed GPU samples after 20 warm-up
frames per initial mode, then exercises the remaining regression phases. Its
`RT_BENCH` records separate frame/shadow/scene/build times and retained bytes.
The controlled fixture is one cube and one receiver at 640x480 on the tested Linux
scale-1 desktop, with a 512-pixel single shadow map. Shadow timing includes the
AS build interval. These are GPU timestamps for the warmed-up fixture, excluding
initial AS construction and FIFO presentation.

| Mode | GPU frame (ms) | GPU shadow (ms) | GPU scene (ms) | Ray AS bytes |
| --- | ---: | ---: | ---: | ---: |
| Raster | 0.019961 | 0.000108 | 0.014753 | 0 |
| Ray query | 0.025239 | 0.001011 | 0.019234 | 19,200 |

[Recorded Linux Release benchmark](benchmarks/022-linux-ray-shadows.csv)

The initial shadow-only Debug sweeps covered 174 macOS and 173 Linux tests. The source archive
changed during those runs, so the licensing check was rerun against the rebuilt
archive. The final ray suite covered hardware queries on Linux and raster
fallback on both platforms. The relocated Release lab passed on both platforms.
The initial macOS ASan/UBSan selection passed 23 checks. The Linux selection passed after
aligning the exporter with the sanitizer policy and rerunning the failed export
check. The Linux exporter now rejects ASan runtime dependencies in the player
or game module.

[Khronos ray tracing overview](https://docs.vulkan.org/guide/latest/extensions/ray_tracing.html),
[ray-query feature requirements](https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceRayQueryFeaturesKHR.html)
and [acceleration-structure lifetime/build rules](https://docs.vulkan.org/spec/latest/chapters/accelstructures.html).

## Landscape system

The landscape system provides multi-chunk editable terrain with real-time GPU mesh updates,
multi-layer texture blending, and sculpting tools.

### Terrain architecture

Terrain is divided into a grid of independent chunks (default $4 \times 4$, 16 chunks total)
with $64 \times 64$ heightmap vertices per chunk ($65{,}536$ vertices total):

- **Chunked updates**: Sculpting or painting dirty chunks only re-uploads modified chunk vertex and normal buffers to the GPU.
- **Normal recalculation**: Surface normals are recalculated on the fly across chunk seams to maintain smooth shading.
- **Multi-layer texturing**: Supports up to 4 layers (Grass, Dirt, Rock, Sand, Snow) with custom diffuse maps, normal maps, roughness, and UV tiling scales.
- **Procedural generation**: Gradient Perlin noise generation with frequency, amplitude, and seed controls.
- **Heightmap persistence**: Landscape configurations and height arrays are serialized within `.afterlight.json` scene files under the `"landscape"` object.

### Sculpting and painting tools

The editor sidebar provides a dedicated **[ Landscape ]** mode with sub-tabs for:

- **Sculpt**: Raise, Lower, Smooth, and Flatten brushes with interactive viewport ring overlays, adjustable brush radius ($1$ to $50$ meters), strength, and falloff profiles (Smooth, Linear, Sphere, Sharp). Holding **Shift** reverses the active sculpt tool.
- **Layers**: Multi-layer weight painting with visual color swatches, opacity controls, custom texture file selection, normal mapping, and per-layer UV tiling scales.
- **Foliage**: Mesh painting onto the landscape with spatial partitioning into chunk instance batches.
- **Manage**: Global landscape operations including procedural generation, flattening, clearing, and layer definitions.

## Instanced foliage system

Foliage is rendered via hardware GPU instancing with spatially partitioned instance batches:

```
Terrain
└── Terrain Chunks (e.g. 4x4)
    ├── Foliage Layer 0 (Grass) -> Shared mesh reference + instance transforms
    ├── Foliage Layer 1 (Trees) -> Shared mesh reference + instance transforms
    └── Foliage Layer 2 (Rocks) -> Shared mesh reference + instance transforms
```

### Key capabilities:
- **GPU Instancing**: Instanced draw calls eliminate CPU overhead for thousands of rendered instances.
- **Spatial Partitioning**: Each chunk maintains its own foliage instances, enabling distance and frustum culling per chunk.
- **Placement Rules**:
  - Random yaw rotation ($0$ to $360^\circ$).
  - Random scale variation within configurable min/max bounds.
  - Slope constraints (min/max angle in degrees) to prevent placement on steep cliffs or flat surfaces.
  - Height thresholds (min/max world elevation).
  - Surface normal alignment with adjustable blend factor.
- **Viewport Painting**: Paint and erase foliage instances directly using the viewport brush with density control.

## Undo and Redo

All landscape actions are integrated into the engine's transaction and command history:
- Sculpt strokes capture delta heightmap states for affected chunks on mouse release.
- Layer weight painting captures weight buffer modifications.
- Foliage painting and erasing record instance addition and removal batches.
- Procedural generation snapshots and restores previous landscape state.
- **Undo** (`Cmd+Z` / `Ctrl+Z`) and **Redo** (`Cmd+Shift+Z` / `Ctrl+Shift+Z`) seamlessly revert and reapply all landscape modifications.

## Post-processing pipeline

Afterlight 0.22 implements a complete HDR post-processing stack in Vulkan:

- **Tone Mapping**: Selectable operators:
  - ACES Filmic
  - AgX
  - Reinhard
  - Neutral / Linear
- **Exposure**: Manual exposure compensation and automatic luminance-based auto-exposure.
- **Bloom**: Dual-pass downsampling/upsampling Gaussian pyramid with thresholding and knee control.
- **Color Grading**: Saturation, contrast, color filter RGB multipliers, and white balance tinting.
- **Depth of Field**: Physically-based bokeh depth of field with focus distance, focal range, and blur radius.
- **Motion Blur**: Camera and velocity-vector based motion blur with shutter angle and sample controls.
- **Cinematic Artifacts**:
  - Vignette with adjustable roundness and softness.
  - Chromatic Aberration with radial spectral displacement.
  - Film Grain with procedural animated noise and luminance masking.

All post-processing parameters are exposed under the **Post-process** editor panel and persisted within the scene file.

## Single-binary game bundling

The standalone player and editor support packaging games into single-file self-contained executables:
- Embeds scene descriptions, meshes, textures, audio, fonts, and game modules into a single binary.
- Requires zero external file dependencies at runtime.
