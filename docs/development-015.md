# Afterlight 0.15: physics performance and powered mechanisms

Eight milestones cover Release profiling, collision/cache optimization, sleeping
constraint islands, joint warm starting, diagnostics, torque-limited hinge
motors, viewport joint editing, and simple-to-complex machinery/export tests.

## Profiling

`afterlight_physics_benchmark` is available in Release without enabling the full
test suite. It runs 64/128/256 bodies in four-box stacks with 8:1 mass ratios.
Forty warmup ticks precede 60 CPU samples. Mean/p95 cover the whole tick; phase
columns describe the last sample. Optional process-CPU timings separate scene
synchronization, broadphase, narrowphase, CCD and solving. Profiling is enabled
for engine diagnostics and disabled by default in standalone physics worlds.
Counters remain available without timing. Game ABI is now 14; rebuild modules.

## Character landing correction

The rounded player capsule now supplies actual capsule contacts and CCD distances.
Its controller pose is a passive obstacle for rigid bodies; horizontal pushing
uses the existing force-limited API. Pose deltas are no longer interpreted as
unbounded kinematic impact velocity. Center, edge and corner landings followed
by another jump retain bounded crate motion; falling bodies still collide with
the player and the real C demo interaction tests pass.
