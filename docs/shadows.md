# Directional shadows

Afterlight first implemented and tested a single directional shadow map, then
added four cascaded maps. Both modes remain available. All engine code is C;
the depth and lighting shaders are GLSL compiled to embedded SPIR-V.

## Editor workflow

Scroll the inspector below **Directional light** to **Shadows**. These controls
are available without an object selected. Shadows default to enabled with four
2048×2048 maps, distance 40, 3×3 PCF, split lambda 0.65 and blend fraction 0.1.
Scene changes participate in undo/redo, save/load and Play scene cloning.

| Control | Effect |
| --- | --- |
| Single map / 4 cascades | One map covers the range, or four maps concentrate detail near the camera. |
| Resolution per map | 512, 1024, 2048 or 4096. Higher values cost memory and depth-rendering work. |
| Distance | Maximum camera-forward shadow distance, limited by the camera far plane. Reduce it to concentrate detail. |
| Constant / slope bias | Offset caster depth to reduce self-shadow speckles; excessive values detach shadows. |
| Normal offset | Move the receiver sample along its world normal; excessive offset loses contact detail. |
| Hard / Soft / Softer | One depth comparison, 3×3 PCF or 5×5 PCF. Filtering smooths map edges. |
| Near detail | Mix uniform and logarithmic split spacing. Higher values allocate more detail nearby. |
| Cascade blend | Overlap fraction used to blend into the next cascade; the last range fades to lit. |
| Show cascade regions | Tint cascades red, green, blue and yellow from near to far. |

Each object's Material section has **Cast shadows** and **Receive shadows**.
Both default to enabled. Hidden objects and children of hidden parents do not
cast. Unlit materials ignore lighting and received shadows but can still cast.
Ambient light remains visible in shadow; only directional diffuse is occluded.

## Implementation and costs

Each frame in flight owns an independent sampled depth array. Each layer gets
a depth-only pass before the main scene. The shader performs manual PCF with a
nearest sampler and explicit LOD. Cascades use camera-frustum slices, practical
uniform/logarithmic splits, overlapping ranges, rotation-stable sphere fits and
texel-snapped light-space centers. Relevant caster bounds extend depth coverage,
including geometry outside the camera view. Per-cascade light-frustum culling
reduces unnecessary draws. Stabilization reduces shimmer; it cannot remove all
aliasing from moving geometry or changing light direction.

At D32, four 2048 maps across two frames use approximately **128 MiB** of depth
storage, or **512 MiB** at 4096, excluding driver overhead. Single-map mode uses
one quarter of that storage. A D16 fallback uses half. Changing resolution or
mode waits for outstanding GPU work and replaces the arrays; ordinary camera
and object motion does not. Shadow resources survive window resizing. Disabling
shadows skips caster draws but retains and clears the allocated maps.

`af_scene_get_shadows` / `af_scene_set_shadows` expose validated CPU settings.
`af_material` contains cast/receive flags. Stats expose shadow draw calls,
allocated layer count and map resolution. Scene format 4 saves these fields;
versions 1 and 2 receive defaults and version 3 retains its shadow fields. See [scene format](scene-format.md) and
[renderer ownership/synchronization](architecture.md).

Shadow maps support the directional light and opaque geometry. Local point/spot
lights are unshadowed. There are no point/spot shadow maps, alpha-tested casters, contact-hardening penumbras, PCSS or ray-traced
shadows. PCF smooths a fixed map-space kernel rather than simulating area lights.
Distant detail depends on resolution, range and split distribution.

## Verification

```sh
AFTERLIGHT_REQUIRE_VALIDATION=1 ctest --preset debug
ctest --preset debug -R 'afterlight.shadow_math|afterlight.shadows'
```

CPU tests cover splits, fitting, vertical lights, world bounds, culling, texel
snapping and invalid settings. GPU tests compare actual rendered pixels for
single-map shadow versus disabled output, cast/receive switches, unlit materials,
visibility and moving casters. The harder variant checks all four cascade
selections, filtered boundary pixels, a camera crossing a cascade boundary,
parented/offscreen/tall casters and a distant caster on a large floor. Tests also
change quality/mode, resize the window and detach/reattach the scene with Vulkan
synchronization validation enabled. Document and scene IO tests cover undo/redo,
material flags, shadow settings and legacy version 2 loading.

## Optional hardware sun shadows (0.22)

The Environment panel can enable ray queries for opaque rigid casters. Skinned,
cutout and streamed casters retain directional maps; local-light shadows keep
their full map path. Traced shadows have hard edges, independent of map PCF.
Unsupported GPUs use raster shadows. The [comparison lab and limits](development-022.md)
cover activation, lifecycle, diagnostics and validation.
