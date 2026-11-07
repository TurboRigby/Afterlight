# Foundations before ray tracing

Afterlight already has raster PBR, image-based lighting, shadow maps, animation,
physics, water, profiling and playable exports. The next milestone should make
these dependable together in one small, polished game built with the engine.
This is a proposed engineering order, not a claim that these areas are absent.

1. **Finish one representative playable demo.** A complete start-to-finish loop,
   clean restart/checkpoints, camera/input behavior, pause/settings, understandable
   feedback and a working exported build. Include moving/skinned props, water and
   light changes so the engine gets exercised as a whole.
2. **Make failures reproducible.** Grow region/image regressions from the recent
   water artifacts, motion and edge cases. Test scene switches, undo, save/load,
   import/reload and missing assets repeatedly. Keep native validation and
   sanitizers clean. Add depth, motion and temporal-history debug views for
   diagnosing future rendering defects.
3. **Set and measure budgets.** Choose a target scene size, resolution, frame time
   and memory limit. Report CPU update/physics/render, GPU passes, waits, uploads
   and resource bytes. Stress resource lifetime, resize and allocation failure;
   make pass dependencies and frame ownership explicit. Reduce allocations for
   disabled effects, including the new AA scratch/motion targets.
4. **Scale the existing systems.** Measure culling and batching, then add/improve
   instancing, LOD and asynchronous asset streaming where profiles justify them.
   Keep physics/animation work bounded and ensure larger scenes remain responsive
   in the editor. Prefer evidence from frame timings to paced FPS alone.
5. **Polish the raster and asset workflow.** Verify color-space consistency, PBR
   material authoring, roughness/normal detail, HDR/exposure, IBL and shadow
   stability across representative scenes. Improve import/reimport diagnostics,
   asset dependencies, hierarchy/prefabs and scene transitions as the demo needs.
6. **Verify actual shipping targets.** Test the exported game, input, audio,
   packaging and graphics on native Linux/Windows GPUs as well as macOS. Existing
   Linux CPU tests do not establish native GPU correctness. Query capabilities
   and provide understandable fallbacks for optional effects.
7. **Add one optional ray-traced effect.** Start with a constrained reflection or
   shadow experiment, keep a raster fallback, and compare image quality, memory
   and GPU time against the baseline. Acceleration-structure builds/updates and
   synchronization add resource-management work; denoising and history reuse add
   their own motion/disocclusion requirements. TAA is useful groundwork but is
   not itself a ray-tracing denoiser.

A useful completion gate is: the demo survives repeated play/reload/resize and
scene switches, its saved state round-trips, its exported build runs on the
chosen targets, and it stays within measured budgets. Then a ray-traced effect
can be judged against a stable baseline.

Vulkan requirements and integration details: [Khronos ray tracing overview](https://docs.vulkan.org/guide/latest/extensions/ray_tracing.html)
and [acceleration structures](https://docs.vulkan.org/tutorial/latest/courses/18_Ray_tracing/02_Acceleration_structures.html).
