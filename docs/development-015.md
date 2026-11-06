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

## Hinge motors

Scene 15 adds `motor_enabled`, `motor_speed` (relative B-A radians/second) and
`max_motor_torque` (newton-metres) to every joint. Older scenes default motors off.
Only hinges accept enabled motors. Accumulated motor impulse is clamped to torque
times substep duration, including warm starts; angular limits take precedence.
Zero target speed acts as a brake. Running motors keep their island awake.
Editor controls and C-host joint views/setters expose these settings. Tests cover
acceleration budgets, speed tracking, braking, limit stalls and persistence.

## Viewport joint tools

In a joint's inspector, choose Drag anchor A/B or Rotate axis A/B. Translation
handles edit world anchor positions and convert back to unscaled body axes;
rotation rings update the axis and perpendicular zero reference together. The
existing Snap toggle uses 0.5 units or 15 degrees. A drag is one undo operation;
Escape cancels it. Finish joint edit returns to object tools. Tests cover rotated
and nonuniformly scaled bodies, world endpoints, actual snapped drag results and
grouped undo/redo, alongside native editor smoke/animation tests.

## Simple-to-complex machinery

`run_motor-room_editor` adds one powered door to the mesh room. Then use
`run_machinery-playground_editor` for the door, swing, seesaw and rotating platform
with passive chains, ramp balls and continuous projectiles. Matching `_player`
targets run independently. Limited mechanisms reverse near their endpoints.

Z brakes/resumes all motors, X reverses their targets, R restores authored bodies
and motor settings, and N switches the two scenes. Movement, Q crate spawning, F
grab/release and V throw remain. C state callbacks preserve motor control state.

Limits remain 1024 physics bodies, 8192 contacts and 128 joints. Motors are velocity
targets, without servo-angle controllers or breakable joints. CCD retains bounded
approximate impacts. The chain stress test is bounded and does not establish
stability for arbitrary sizes or mass ratios. Timings must be measured in Release.

## Verification and local measurements

All 114 native Debug tests and all 114 macOS ASan/UBSan tests pass with Vulkan
validation. All 75 Linux Clang sanitizer CPU tests pass with leak detection.
The powered scenes pass relocated Debug and optimized Release exports after
source removal. Static analysis of the physics and joint-gizmo paths is clean.

The landing regression observed zero horizontal speed/spin in its three cases.
The 12-link chain's maximum measured length errors were 0.00096 / 0.00113 /
0.00106 units at 30 / 60 / 144 Hz. Powered scene anchor/length error stayed below
0.00050 units in the local native run. These are bounded regression scenarios.

Local Apple M4 Release benchmark samples, in process-CPU milliseconds:

| Bodies | Initial mean / p95 | Final mean / p95 |
| --- | --- | --- |
| 64 | 2.59 / 2.67 | 1.71 / 3.52 |
| 128 | 5.68 / 5.90 | 4.20 / 6.43 |
| 256 | 10.82 / 11.32 | 6.53 / 10.19 |

Sleeping remains enabled, so samples include groups becoming quiet during the
measurement window. Background build load and CPU scheduling also vary; these
are diagnostic observations, not a controlled speedup claim or frame-rate budget.
The cache-probe reduction provides a separate measure of avoided lookup work.
