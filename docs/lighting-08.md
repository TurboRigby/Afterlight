# Afterlight 0.8: builds, HDR lighting and previews

Engine, editor and game modules remain C17. The renderer targets Vulkan 1.1;
shaders are GLSL. This milestone implements all six planned items.

## Try the Lighting Lab

```sh
cmake --preset debug
cmake --build --preset debug --target run_lighting_editor
```

`sandbox/scenes/lighting.afterlight.json` combines smooth polished/rough PBR spheres,
a warm point light, a cool spot light and an emissive strip. The original sphere
is regenerated with `python3 scripts/generate_lighting_assets.py`. Scroll the Inspector
to **Environment lighting**, **Bloom** and **Point and spot lights**. The Assets
toolbar button shows texture and mesh thumbnails; a material sphere appears
under an object's Material heading. Changes use the normal undo/redo and save
paths. Existing demo scenes keep their previous lighting until the new effects
are enabled.

## 1. Build and CPU-test CI

`.github/workflows/build.yml` builds Debug, Release and Address/UndefinedBehavior
Sanitizer configurations on `macos-15`, then runs tests excluding the `graphics`
label. It runs on pushes to main, pull requests and manual dispatch once pushed
to GitHub. Checkout is pinned to a full commit and does not persist credentials;
workflow permissions are read-only. There are no external secrets or automatic
publishing steps. The separate GitHub CodeQL setup remains enabled.

Interactive Vulkan/editor pixel tests run locally on a GPU. Hosted CI does not
pretend those checks passed. Apple AddressSanitizer does not support leak
detection, so CI disables LeakSanitizer while retaining address/UB checks.

## 2. HDR forward rendering

Scene geometry renders into sampled `R16G16B16A16_SFLOAT` color with a depth
attachment per swapchain image. Exposure and optional Reinhard tone mapping
run in a fullscreen presentation pass, followed by the UI in display space.
This preserves bright scene values for post processing. The display output is
still SDR; this does not negotiate an HDR monitor color space.

Devices must support RGBA16F color attachments with linear sampling. Missing
support produces a startup error. Extent-dependent images, framebuffers and
pipelines are rebuilt on resize; mesh, environment and shadow resources survive.
Render-pass dependencies order color writes and subsequent fragment reads,
including ping-pong reuse. UI images and text bypass exposure, tone mapping and
bloom. Clear scene/background color follows scene exposure and tone mapping.

## 3. Environment lighting / IBL

Enable **Image-based lighting**, then choose Studio softboxes, Sunset sky or
Overcast sky. Each original environment is baked into floating-point images:
cosine-weighted diffuse convolution, GGX roughness-prefiltered radiance levels,
and an integrated split-sum BRDF lookup. PBR reflection strength/color depends
on Fresnel, metallic and roughness, including the packed surface texture. Legacy
Lambert materials receive diffuse environment light. Unlit surfaces bypass it.

Intensity and rotation are saved scene settings. Rotation only updates a
uniform; changing the environment profile rebakes/replaces its images after
draining GPU work. Images use longitude wrapping and latitude clamping.
The initial environments are procedural HDR sources; external HDRI import,
visible skyboxes, reflection probes and occlusion of environment light are not
implemented. This is global environment illumination, not indirect light bounced
from scene geometry.

## 4. Point and spot shadows

Each local light has **Cast local shadows** and a depth-bias control. Point
lights use six perspective depth faces; spots use one fitted perspective cone.
The shader performs 3x3 manual PCF, selecting/reprojecting point samples across
face boundaries. Material cast/receive flags and inherited visibility apply.
Local shadows work independently of the directional-shadow switch.

The first four enabled, positive-intensity shadow lights in scene order receive
maps, up to 24 faces. Remaining lights still illuminate without shadows; the
Performance panel reports this overflow. Resolution is 256, 512 or 1024 pixels
per face. Per-frame arrays protect resources in flight. Changing the required
face count/resolution waits for GPU work and replaces the array transactionally.
This is a fixed budget, without adaptive shadow caching or distance-based light
selection. At 1024/D32, 24 faces in two frames can use about 192 MiB for depth.

## 5. Bloom

The HDR image is downsampled to half resolution with a soft brightness threshold,
then filtered horizontally and vertically into separate images. The presentation
pass adds the result before exposure/tone mapping. Strength, threshold and blur
radius are saved/undoable. Sampling is bounded by the scene viewport so light
does not bleed across editor panels. The current blur is a bounded separable
Gaussian, rather than a multiscale lens simulation. Intermediate resources and
filter passes currently remain allocated/executed when strength is zero.

## 6. Editor thumbnails

The Assets list shows image previews with aspect/alpha preserved and mesh
previews using actual bounded geometry, vertex colors and a depth buffer.
Click a thumbnail or its name to select the asset. The Inspector's material
sphere previews color/maps, roughness, metallic, normal strength and emission.
It uses fixed preview lighting and approximates the scene shader.

A 64-entry cache uploads at most one new preview per UI frame and only requests
visible widgets. File signatures, manual reload, successful watched dependency
reloads and material edits invalidate entries. Invalid replacement files retain
the last good preview. Large mesh previews sample at most 65,536 triangles;
they show geometry rather than full textured/animated model rendering. Audio
entries use an icon; it is not an audio waveform visualization.

Overlay commands now carry an image ID. IDs 0/1 select the font atlas; callers
may upload/replace/remove IDs >=2 through the public C overlay functions.
At most 128 images can be resident. Upload/removal drains GPU work, and all image
resources are released with the UI. This is a simple bounded upload path, not
asynchronous thumbnail streaming.

## Persistence and validation

Scene format **8** adds HDR/environment/bloom settings and local shadow fields;
versions 1–7 remain readable with environment/bloom/local shadows disabled.
New C layouts require game ABI **7**. Restart an older editor and rebuild game
modules; generated projects must refresh their copied public headers.

The suite covers CPU BRDF bounds/prefiltering and all six point face matrices;
pixel checks cover local cast/receive/visibility, resizing and budget overflow,
environment rotation/profile changes, HDR highlight recovery, bloom threshold
and halo, and overlay image lifetime/color unaffected by post effects. CPU
thumbnail checks and the actual Nuklear editor thumbnail path are exercised too.
`afterlight.render_data` covers persistence/undo and invalid values. Existing
material, directional-shadow, editor, resize, reload and packaged player tests
remain in place. See [verification](testing.md).

Implementation references: [Vulkan render passes and dependencies](https://docs.vulkan.org/spec/latest/chapters/renderpass.html),
[Filament's lighting model](https://google.github.io/filament/Filament.html),
and [GitHub workflow syntax](https://docs.github.com/en/actions/reference/workflows-and-actions/workflow-syntax).
