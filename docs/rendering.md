# Materials, lighting and profiling

Afterlight **0.8** adds floating-point HDR rendering, environment lighting, local
shadows and bloom. Start with [the Lighting Lab and current controls](lighting-08.md).

Afterlight 0.4 adds the [fifteen completed steps](development-15.md). Engine and
game code remain C17; shaders are GLSL compiled to embedded SPIR-V.

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
Selection tint remains transient editor state. Geometry is opaque.

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
