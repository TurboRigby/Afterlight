# 0.18 water performance and gameplay

Implementation order:

1. Release profiling for all six water labs and every quality.
2. Frame-owned dynamic mesh streaming without GPU-wide upload waits.
3. Calm water and floating-body sleeping, with event-driven waking.
4. Stable fluid impulses for moving, heavy and thin containers.
5. Swimming, diving, surfacing and river-current interaction.
6. Underwater fog/absorption with a smooth surface transition.
7. Scene-color refraction for submerged geometry.
8. A playable water course and a harder combined course, with exports/regressions.

`afterlight_water_benchmark <scene-directory> [measured-ticks]` is available in
Release without enabling the complete test suite. It warms up 30 ticks, then
reports process-CPU mean/max physics and mesh-generation cost separately as CSV.
All six labs run at low/medium/high quality. These are diagnostics, without
machine-specific pass/fail time thresholds; GPU upload/presentation is excluded.

A second Release executable, `afterlight_water_render_benchmark`, runs the same
18 cases in a 640×480 logical native window, separating wall-clock physics,
render submission, GPU timestamps, frame-fence and acquire waits. It warms up
30 frames per case. Run graphics benchmarks/tests serially.

Initial Apple M4 baseline (90 CPU ticks / 60 rendered frames): the high-quality
pouring lab averages 6.57 ms process CPU in physics and 1.32 ms in mesh generation
(~177 particles). The high-quality river bend is 0.95 / 0.48 ms. Render-loop CPU
submission includes presentation pacing and the old mesh-wide idle wait, so its
~10–16 ms mean must not be interpreted as pure GPU work. Raw CSV logs are written
to build/release/018-water-baseline.csv and 018-render-baseline.csv.
