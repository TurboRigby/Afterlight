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

## Collision and cache work

CCD now uses conservative swept bounds sorted along the widest scene axis,
rebuilding after impacts. Worlds without continuous bodies skip CCD pair work.
Discrete collision shares the same adaptive sweep. Contact warm starts use a
stable-ID pair hash instead of scanning the whole cache. The 256-body sample
reduced final-tick cache probes from 1717403 to 9662; timing remains machine- and
scheduling-dependent. Sparse 256-body continuous scenes avoid narrowphase pairs.
Thin-wall, moving-pair, angular CCD and original stack/controller tests remain.

## Constraint islands

Dynamic bodies joined by contacts or enabled constraints sleep and wake as a
group. Static ground does not merge unrelated groups. World/static anchors and
supporting contacts allow quiet groups to sleep; removed supports, moved
kinematic anchors, impulses and held bodies wake the affected group. Counters
report island count, awake count and largest group. Tests cover a sleeping chain,
impulse propagation and removal of its world support, alongside stack sleeping.

## Joint warm starting

Distance, point-anchor, hinge alignment and unilateral limit rows retain bounded
impulses between substeps. Warm starts scale with the substep duration and reset
when endpoints or settings change. Position correction remains separate from
velocity impulses. A twelve-link chain with a 16:1 bottom mass ratio stays below
0.14 world units of length error at 30/60/144 Hz, including a later impulse.
Sleeping groups skip joint rows until woken.

## Diagnostics

Engine and C-host joint-state queries expose anchor/angle errors, relative hinge
speed, sleeping state and reaction estimates in newtons/newton-metres. Reactions
are cached solver impulses divided by substep duration, retaining the last solved
value during sleep. The suspended 2 kg regression measures approximately 19.62 N.
The editor shows per-joint state, phase timings, island counts, cache probes and
CCD clamps; sleeping and high-error joints have distinct overlay colors.
