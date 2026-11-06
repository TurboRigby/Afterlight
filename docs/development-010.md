# Afterlight 0.10 development

This work is split into eight commits, in the requested order:

1. Frame profiling and measured rendering improvements.
2. Background HDR decoding/baking with progress and cancellation.
3. Visibility-aware local shadow budgeting and reduced redundant work.
4. glTF node transform animation.
5. Clip playback, pause, looping, seeking and speed.
6. Skeleton import and Vulkan skinning in scene and shadow passes.
7. Editor animation controls and C gameplay/blending functions.
8. Simple and complex demos, persistence/reload/export and regression checks.

## 1. Profiling

The Performance panel separates GPU shadows, opaque scene, post processing and
overlay work. CPU frame-fence and swapchain-image acquisition waits are reported
separately; a long render callback is not necessarily expensive CPU recording.
GPU results come from completed frame timestamps and exclude presentation.

Build Release and run a fixed camera/viewport benchmark:

```sh
cmake --preset release
cmake --build --preset release
build/release/afterlight_profile sandbox/scenes/lighting.afterlight.json
```

This executable uses a 1280x800 logical window (2560x1600 pixels on the tested
Retina screen), warms each variant for 30 frames and averages 120 samples.
Variants successively disable bloom, local shadows, then directional shadows.
CSV is written to stdout; diagnostics go to stderr. This is a measurement tool,
not an FPS assertion; resolution, other applications and thermal state matter.

The baseline revealed that switching bloom off still performed extraction and
both blur passes. These are now skipped, with a dedicated composite descriptor
using initialized HDR resources even when bloom has never run. The Gaussian
blur combines adjacent taps through linear sampling (five reads instead of
nine). Exposure/tone mapping still run and the UI remains after composition.
The existing GPU pixel checks cover bloom on/off, threshold, viewport clipping,
resize and exposure-independent overlay images.

## 2. HDR import jobs

The import dialog starts one owned SDL worker for RGBE decoding and convolution.
The editor shows the current stage and percentage with a Cancel button. Bake
cancellation is checked once per output row; decoding finishes its bounded input
before observing cancellation. No worker accesses the live document or Vulkan.
An acquire-published immutable result is uploaded between main-thread updates.
GPU resource creation/transfer still uses the synchronous asset upload path;
`upload_ms` reports that remaining cost. Background baking is the expensive CPU
portion removed from editor frames, not a promise of zero upload latency.

Failure/cancellation preserves the previous named resource. Assignment is an
atomic undoable document edit only after successful publication; if the document
changed or entered Play meanwhile, automatic assignment is withheld. Existing
startup and transactional watched-reload paths remain synchronous. Engine
destruction cancels/joins the worker and releases every result. C callers use
`af_engine_start_environment`, `af_engine_environment_status` and
`af_engine_cancel_environment`. CPU tests cover successful completion, invalid
input, early/late cancellation and shutdown; GPU sky/IBL checks start through
the asynchronous engine API.

## 3. Shadow budget and map reuse

The four-light shadow budget selects enabled, nonzero lights whose range boxes
intersect the camera frustum. It conservatively retains lights whose centers
are offscreen but whose influence enters the view. Priority is range divided
by distance plus one, with scene order breaking ties. Overflow counts eligible
lights that exceed the budget; all enabled lights still contribute lighting.
This is a conservative range heuristic, not per-pixel occlusion or spot-cone
visibility testing.

Each frame owns its shadow maps and signatures. Unchanged caster world matrices,
mesh registry generation, light matrices and depth biases reuse completed maps.
Animated/moved/deleted casters, cast/visibility changes, hot reload and shadow
resource replacement invalidate them. Reuse preserves sampled depth layouts;
new maps receive their first clear/render before being sampled. Counters in
Performance distinguish recorded layers from reused layers. Receiver filters
and material colors need no depth-map regeneration.
On the tested Apple M4, warmed static-scene shadow timing fell to about
0.17–0.25 ms in the benchmark; total GPU time still includes lighting and post
work and varies with other running applications.

## 4. glTF node animation

`af_animation_file_load` retains source node IDs, parents, quaternion bind poses
and up to 128 named clips. `af_animation_sample` evaluates translation, rotation
and scale, including STEP, linear/shortest-path SLERP, and time-scaled Hermite
CUBICSPLINE interpolation. Channel times clamp independently at either end.
Quaternion spline results normalize after interpolation. World matrices work
with parents appearing after children in the source array and reject cycles or
nonfinite accumulated transforms. Negative TRS scales retain their signs.

Input limits are 16,384 nodes, 4,096 channels across a file, 65,536 keys per
channel and 64 MiB of decoded key data, in addition to the existing glTF parser
limits. Morph animation, sheared matrices and singular bind transforms are not
supported. Animated nodes must use TRS rather than a matrix. Model nodes expose
`source_node` so import preorder does not change animation targets.

The original `animated.gltf` fixture exercises linear TRS, stepped translation
and a cubic translation curve; `scripts/generate_animation_assets.py` rebuilds
it. Tests verify quaternion/Euler convention agreement, parent accumulation,
clamping, invalid buffers, nonfinite inputs and duplicate tracks. Sampling
follows [glTF animation interpolation](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#appendix-c-animation-sampler-interpolation-modes).

## 5. Playback

`af_animation_player` owns its reusable blend buffers and borrows animation data
per call. Initialize with `af_animation_player_default`, then play, pause, seek,
update and sample; destroy once when finished. Looping wraps in either direction,
negative speed starts a clip at its end, and non-looping clips hold their endpoint.
Speed zero holds the current pose. Speed is bounded to [-100,100], transition
time to [0,10] seconds and a single update to one day of finite nonnegative time.

Crossfades advance both clips and use quaternion SLERP for rotation. Interrupting
a fade captures its current mixed pose to avoid snapping back to a source clip.
A short destination clip holds its endpoint while the remaining fade finishes.
Seeking/stopping clears transitions. Runtime time/temporary buffers are separate
from saved animation configuration and must not be copied by value.
