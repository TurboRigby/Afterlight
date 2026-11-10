# Materials, lighting and profiling

Afterlight **0.9** adds [imported HDR environments and skyboxes](assets-09.md).
Afterlight **0.8** added floating-point HDR rendering, environment lighting, local
shadows and bloom. Start with [the Lighting Lab and current controls](lighting-08.md).

Engine and game code remain C17; shaders are GLSL compiled to embedded SPIR-V.

## Texture quality

Scene color images are sRGB. Upload builds a complete RGBA8 mip chain on the CPU,
averaging RGB in linear space and alpha linearly. Area weights include NPOT edge
texels and handle one-dimensional images. All levels upload together, avoiding a
dependency on optional depth/color blit capabilities. Scene samplers use trilinear
repeat filtering and up to 8x anisotropy, capped by hardware support. Devices that
lack samplerAnisotropy use ordinary trilinear filtering. The UI font atlas remains
single-level, linear/clamped. Importing/replacing images waits for GPU work.

Normal maps are separate `normal_map` assets sampled as linear UNORM; importing
the same file as color and normal gives distinct references. RGB encodes a tangent
normal in [0,1]. Strength scales XY before normalization. UV derivatives build
the tangent frame, including mirrored UV handedness; degenerate UVs retain the
mesh normal. Missing normal maps use a flat fallback and increment missing-texture
stats. Mips average encoded normals and the shader renormalizes them.

`af_engine_texture_info` reports dimensions, mip count, allocated GPU bytes,
anisotropy and color space. Texture allocation totals exclude shadows, fallback
images and the UI atlas. Adding mip levels costs roughly one third more pixel
storage for large square images, plus driver allocation overhead.

## Material model

Existing scenes retain Lambert lighting. Enable **Physically based** to use an
isotropic Cook–Torrance microfacet path: GGX normal distribution, Schlick Fresnel,
Smith visibility approximation and energy-split Lambert diffuse. Roughness and
metallic values are scalar [0,1]; the shader clamps roughness to 0.04 for numerical
stability. Metallic materials tint specular using base color and remove diffuse.
Packed metallic/roughness maps and optional diffuse/specular environment lighting
are supported. Clearcoat, transmission and area lights remain future work. The
Ambient scalar is separate from the convolved environment contribution.

Emission adds linear RGB times strength independently of lights, including for
unlit materials. It does not illuminate neighboring geometry. When enabled, bloom filters bright
HDR emission before presentation.
Color textures, vertex colors and material base color multiply before lighting.
Selection tint remains transient editor state. Geometry supports opaque, cutout and sorted alpha blending; see [0.12](development-012.md).

Exposure multiplies scene geometry by 2^stops (range -10 to +10). Optional tone
mapping uses the Reinhard curve c/(1+c). Scene color first goes to an RGBA16F target, then bloom, exposure and tone mapping
are applied before the swapchain; an sRGB swapchain performs output encoding.
The editor UI is drawn afterwards and is unaffected. Clear scene/background
color follows scene exposure. Automatic exposure and HDR monitor output are not
implemented. GPU scene timing includes post processing.

## Local lights

A scene holds up to 16 named point/spot lights, separate from the directional
light. Positions are world-space. Linear RGB is [0,100], intensity [0,10000], and
range [0.01,10000]. Intensity is an engine multiplier rather than a calibrated
photometric unit. Distance attenuation is inverse-square with a smooth range
window and a small denominator floor near the source.

Spot direction points along emitted light. Inner and outer angles are half-cone
angles in radians in the C API/scene file, displayed in degrees in the editor:
0 <= inner <= outer <= 89 degrees. The transition between the cones is smooth;
equal angles produce a narrow hard transition. Disabled lights consume scene
slots but are excluded from the GPU light loop. Local lights can opt into point/spot shadows using a four-light budget; see
[local shadow limits and filtering](lighting-08.md#4-point-and-spot-shadows). `af_scene_*local_light*` edits CPU data; clone, undo and Play retain it.
Indices identify entries in the current array and change when a preceding light
is removed; callers should re-query after structural edits.

## Profiling

The Performance toolbar button exposes last-iteration CPU event/update/render
submission times, paced FPS, delayed GPU frame/shadow/scene/UI timings, draws and
texture diagnostics. CPU submit includes fence/acquisition/presentation waits;
it is not GPU execution time. GPU timestamps exclude actual presentation and
can include queue-stage idle time inside the measured intervals. Frames in flight
mean GPU results arrive later than the current CPU sample. A device/queue without
timestamp support reports that explicitly. No extra GPU idle wait is introduced
to collect query results.

## Verification and references

The `afterlight.mipmap`, `afterlight.render_features` and `afterlight.render_data`
tests cover CPU color filtering, real rendered-pixel responses, light range/cone
falloff, minified checker sampling, timing availability, scene v2/v3 compatibility,
strict v4 validation, clone/undo and browser filtering/assignment. The full suite
also retains directional-shadow and editor graphics regressions. See
[testing](testing.md) for commands and the macOS sanitizer module-unload fix.

Design references: [Khronos texture sampling](https://docs.vulkan.org/tutorial/latest/06_Texture_mapping/01_Image_view_and_sampler.html),
[Vulkan timestamp queries](https://docs.vulkan.org/spec/latest/chapters/queries.html),
and [Filament's material-model discussion](https://google.github.io/filament/Filament.html).
Afterlight implements a small direct-light model and does not include Filament.

## Water optics (0.18)

Water uses dielectric reflection, lit scattering and RGB transmission. The
optional underwater effect clips the camera-to-fragment path to the active
region and fades across a 20 cm surface band. River fog approximates the local
tangent region. Opaque HDR color/depth snapshots support screen-space refraction;
IOR and strength bend the lookup, viewport bounds clamp it, and foreground depth
prevents dragging nearby opaque objects into the water. Refraction covers visible
opaque geometry; off-screen objects and layered transparent refraction are not
reconstructed. Both effects can be disabled in scene render settings.

Pools/rivers now have visible side and bottom boundaries as well as their top.
A filtered world-space ripple normal texture responds to wind and simulation time.
It changes optical shading without an external image dependency; see the
[0.19 guide](development-019.md) for controls and approximations.

The editor's **Environment** panel groups sun, sky, environment lighting and
exposure. Night presets lower all global sources together; sky brightness and
image-based lighting remain separately adjustable. The sun is a directional
light, and HDR image rotation is independent of its heading.

## Anti-aliasing (0.20)

The Environment panel selects Off, FXAA, SMAA or TAA per scene. Spatial modes
filter perceptual color; TAA resolves HDR history using camera/object/bone motion,
depth rejection and neighborhood clipping. Water and blended surfaces use a
reactive spatial finish. UI follows AA. See the [AA guide](development-020.md) for
controls, fallback requirements, resource costs and known limits.

## Resource and visibility diagnostics (0.21)

The Performance panel offers shaded, logarithmic depth, motion, previous-history
and history-rejection views. Diagnostic views temporarily use TAA resources
without changing the scene's saved AA selection; unsupported independent blending
rejects that selection. Invalid motion is magenta; rejection is red and accepted
history is green. UI remains outside scene post processing.

Post targets follow enabled effects: motion/history only for TAA, SMAA scratch
and lookup tables only for SMAA, bloom scratch only when bloom is enabled.
Refraction retains 1x1 placeholder snapshots until required by water. Switching
resource configuration waits for the device once and can briefly pause rendering.

`af_engine_set_render_budget` sets diagnostic GPU-time and retained-memory targets
(default 16.67 ms / 512 MiB). Stats include actual Vulkan allocation requirements
for tracked resources, with post/shadow/refraction/mesh breakdowns. They exclude
UI buffers/fonts and driver/presentation allocations. Warnings do not impose hard
allocation limits. GPU timestamps exclude presentation; CPU render time includes
waits and presentation work.

Conservative frustum culling uses unjittered camera bounds expanded by two
framebuffer pixels, including skinned and water mesh bounds. The original
[visibility benchmark](development-021.md#visibility-baseline) measures
culling separately from draw batching.

## Instanced props

The opaque scene pass batches consecutive visible rigid props that share mesh,
material and sidedness flags. The order of submitted objects is preserved for
depth ties. Each instance keeps its own transform and motion history. Water,
transparent materials, skinned or morphed meshes, and editor highlights use
individual draws. Shadow maps use their existing submission path.

```sh
cmake --build build --target afterlight_renderer_benchmark
build/afterlight_renderer_benchmark 4095 --props --no-validation
ctest --test-dir build -R '^afterlight.prop_instancing$' --output-on-failure
```

The benchmark compares 4,096 visible railings, pipes and fixtures across three
material runs. Median of three Release runs on Apple M4 with MoltenVK 1.4.2,
an 800 × 600 framebuffer, shadows disabled, and 31 sampled frames per mode:

| Mode | Scene draws | GPU scene | CPU command recording | CPU render less fence and acquire | Tracked GPU memory |
| --- | ---: | ---: | ---: | ---: | ---: |
| Individual | 4,096 | 2.56 ms | 1.66 ms | 16.12 ms | 36.04 MiB |
| Instanced | 3 | 0.86 ms | 1.68 ms | 16.31 ms | 36.67 MiB |

CPU render time includes presentation. The validation test compares a captured
region byte for byte and verifies that all 1,024 test props remain visible.

## Ray-query sun shadows (0.22)

Optional ray queries replace opaque rigid casters in the sun's map pass, while
other supported casters retain raster maps. Ray resources are lazy, per-frame
TLAS ownership follows fences, and tracked GPU bytes include AS storage/scratch
and instances. Disabling the effect releases them. The ordinary raster shader is
kept for unsupported devices. See the [0.22 guide](development-022.md).

## Clustered geometry

The offline cooker packs static indexed geometry into spatial clusters of up
to 128 triangles. Load the resulting `.afgeom` asset with
`af_engine_load_cluster_mesh` and reference its mesh name on a scene object.
The opaque pass culls clusters against the view. Shadows and other passes use
the mesh as a whole.

```sh
cmake --build build --target afterlight_cluster_demo
build/afterlight_cluster_demo build/cluster-wall.afgeom
build/afterlight_cluster_cook input.glb output.afgeom
ctest --test-dir build -R '^afterlight.cluster_geometry$' --output-on-failure
```

In the demo, WASD and Q/E move the camera, right click toggles mouse look,
R resets the camera, C toggles cluster colors, V switches to the regular mesh
path, and Space prints the cluster counters. The built-in wall is procedural.
The Vulkan test compares a capture from the regular path with the clustered
path and checks that the debug view changes the colors.

With Vulkan compute and indexed multi-draw support, the shaded pass generates
its draw commands on the GPU. Each candidate has a fixed command slot, and
culled clusters receive a zero index count. The X key switches between GPU
and CPU culling. The color view uses the CPU path for per-cluster colors.
GPU visibility and triangle counts are read back after a frame fence signals.

The cooker groups nearby clusters into regions and builds a coarser
replacement for each region it can simplify. It preserves boundary vertices
and uses the largest displacement from a source vertex to its surviving
representative as the geometric error. The renderer selects the coarse
region when that error projects to two pixels or less. N toggles detail
selection in the demo. The boundary test compares the exact edge sets of
all simplified wall regions.

Cook with `--stream` to separate the coarse mesh from the fine pages on disk.
The same loader accepts both formats. The streamed mesh keeps the coarse
vertices and indices available from the first frame. Each mesh can retain up
to 24 fine pages, and the renderer supports up to eight streamed meshes at
once. A worker reads requested pages, and the renderer uploads them and
reuses cache slots after their frame fences signal. If a fine page is missing
or unreadable, the region uses its coarse geometry. Shadow and ray geometry
for streamed meshes use the resident coarse mesh.

```sh
build/afterlight_cluster_demo build/cluster-wall.afpages
build/afterlight_cluster_cook --stream input.glb output.afpages
ctest --test-dir build -R '^afterlight.cluster_' --output-on-failure
```

On the Apple M4 test, the near view submitted 27,648 of the wall's 147,456
triangles. The far view submitted 82,036 with LOD enabled. The streaming test
loaded 32 pages while moving the camera, with eight evictions and roughly
1.6 MB of resident fine-page buffers at its final stop. These checks measure
triangle counts, image agreement and residency. GPU timings vary by path
and driver.

Each streamed mesh queues up to 32 fine-page requests. Requests for the
same page share one entry. The worker prefers regions seen most recently,
then regions closest to the camera. Requests that leave the view expire
after two frames. The queue keeps a single completed read ready for upload.
