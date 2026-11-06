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
