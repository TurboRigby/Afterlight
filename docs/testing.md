# Verification

```sh
cmake --preset debug
cmake --build --preset debug
AFTERLIGHT_REQUIRE_VALIDATION=1 ctest --preset debug
cmake --preset release
cmake --build --preset release
```

The Debug suite covers the Apple M4/MoltenVK path, including
Vulkan synchronization validation, actual pixel comparisons, fixed simulation,
collisions, the controller and obstacle-course gameplay. Graphics tests require
a desktop and Vulkan driver. The macOS-only module-unload regression is excluded
on other platforms. Linux CPU coverage
is described below; native Linux graphics and Windows still need verification.

`afterlight.editor_playground` opens the actual obstacle course and its C module.
It also injects a real jump and verifies its audio cue/timer, checks loaded audio/GLB resources and F6 maximize/restore.
`afterlight.editor_project` runs the same regression using `--project`, verifying
that the correct module is selected without a playground launch flag.
The scripted tests wait for native window focus and restart their sequence if
the OS takes focus away; they keep a bounded frame budget. The engine still
releases capture/input on focus loss. The test checks that Play starts in the player camera with mouse capture, right mouse
temporarily gives the editor camera control, Escape releases capture, clicking
recaptures, paused stepping works, and Stop restores the clean edit scene/camera.

## Address/undefined sanitizers

```sh
cmake -S . -B build/asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON \
  -DCMAKE_C_FLAGS='-fsanitize=address,undefined' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined' \
  -DCMAKE_SHARED_LINKER_FLAGS='-fsanitize=address,undefined' \
  -DCMAKE_MODULE_LINKER_FLAGS='-fsanitize=address,undefined'
cmake --build build/asan
ctest --test-dir build/asan --output-on-failure
```

## macOS module-unload fix

The previous sanitizer failure is fixed in 0.5. On the tested Apple Clang 21 / macOS
27 system, unloading a sanitized C game module did not execute Clang's generated
Mach-O ASan destructor. Global redzones stayed poisoned after the image unmapped.
When macOS reused that address for a window backing store or metadata, ASan
reported a global-buffer-overflow in CoreGraphics/CoreSpotlight and aborted. The
crash report from editor PID 48575 came from that CTest sanitizer run.

A standalone load/unload probe reproduced this without Afterlight, SDL or Vulkan.
The fix uses `engine/src/module_sanitizer.c` as the **first linked object** in each
macOS Clang module. Its ordinary C destructor registers via the working atexit
path and calls compiler-rt's image-global unregistration entry point. Linking it
first registers this cleanup before user destructors, making cleanup execute
last. The original compiler-generated ASan destructor is replaced with the
[Clang destructor-kind option](https://clang.llvm.org/docs/ClangCommandLineReference.html#cmdoption-clang-fsanitize-address-destructor)
to prevent duplicate unregistration. CMake passes that option to the frontend;
it has no effect in builds without AddressSanitizer.

Heap, stack, global and undefined-behavior instrumentation remain enabled. There
are no ASan suppressions, manual shadow-memory unpoisoning, retained module images,
or skipped editor tests. Normal Debug/Release behavior is unchanged.

`afterlight.module_unload` performs sixteen load/unload cycles. It verifies that
global redzones are still protected while module code and user destructors run,
then confirms the former redzone is unpoisoned after unload. The formerly failing
editor smoke test also passes repeatedly. Run the targeted checks with:

```sh
ctest --test-dir build/asan -R 'afterlight.module_unload|afterlight.editor_smoke|afterlight.hot_reload' --output-on-failure
```

Custom module targets need the same first-object cleanup and frontend option as
the game targets in CMakeLists.txt. Keep the cleanup object first when adding
translation units. A compiler without the required option cannot use this
workaround; CMake detects support before applying it.

## Project, component, course, audio and glTF coverage

`afterlight.project` covers project-relative references, atomic save preservation,
strict JSON/version/NUL rejection, player/camera edits and undo, component
serialization and spawn-ID remapping within duplicated subtrees. The course test
renames all objects to prove component-based discovery, then exercises walking,
jumping, platforms, ordered checkpoints, respawn, locked finish, frozen finish
time and a clean new run. Audio tests use SDL's dummy driver to decode the actual
bundled WAVs, open playback streams, mix clips, set gain, stop/replay and preserve
old clips after failed replacement. They do not assess speaker output by ear.

`afterlight.gltf` loads analytic fixtures as external-buffer glTF, embedded data
URI glTF and GLB. It checks parent transforms, mirrored winding, normals, UV0,
normalized colors and base-color factors, then loads the real beacon. Malformed
buffers, oversized declarations, singular transforms and truncated GLB are
rejected. Playground graphics tests upload/render the beacon and show the shared
HUD with Vulkan synchronization validation. The model importer is static-only;
see assets.md for its explicit material/animation limits.

## 0.7 engine/tooling checks

`afterlight.player` launches a project through the generic C player. Model tests
cover a two-material hierarchy, mirrored/local transforms, external and embedded
base/normal/surface maps, selector reload, scene format 7 persistence and undo.
The rendering regression samples real pixels to verify packed metallic/roughness
maps in linear space. `afterlight.asset_reload` watches a glTF image dependency,
observes a GPU pixel change, corrupts another map and checks last-good retention,
then repairs it and verifies recovery with unchanged resource counts.

`afterlight.project_tools` creates a starter using the actual wizard CLI,
compiles/runs its C module, adds a rich model, exports, checks overwrite/failure
isolation, deletes source data, relocates the export and launches it. On macOS it
also checks bundled loader/driver paths. In sanitizer builds it exercises starter
build/play/import and confirms sanitizer runtimes are rejected for distribution;
the normal build covers runnable export. No graphics test is suppressed.
The finished-game milestone is not part of these changes.

`afterlight.editor_wizard` invokes the bundled tool through the real editor UI
process-launch path and validates the generated project while preserving the
open document/history state. It uses a unique test destination per run.


## 0.8 lighting, thumbnails and build CI

See [the six-step milestone](lighting-08.md) for implementation and limits.
`afterlight.lighting_math`, `afterlight.lighting`, `afterlight.thumbnails` and
`afterlight.editor_thumbnails` add CPU integration/matrix checks, rendered-pixel
comparisons, resource changes and real editor image commands. Updated render-data
checks cover v8 settings and local shadow persistence/rejection. Overlay image
replacement/removal and independence from exposure/bloom are checked on the GPU.

GitHub's build workflow runs macOS Debug/Release/ASan+UBSan builds and all tests
without the `graphics` label. Run the interactive suite locally with Vulkan
synchronization validation. Apple ASan requires `ASAN_OPTIONS=detect_leaks=0`.
Validate workflow changes with `actionlint .github/workflows/build.yml`.


## 0.9 environment, job and light-tool checks

The additional `afterlight.hdr`, `afterlight.environments`,
`afterlight.light_gizmos` and `afterlight.editor_light_tools` tests verify decoder
bounds, imported radiance/sky pixels, light wire geometry and actual editor
mouse picking/drag/cancel behavior. Updated thumbnail tests cover worker
completion, invalidation and teardown with queued work; exports include HDR.
Updated data tests cover v9 persistence and v7/v8 compatibility. See
[0.9 workflow and limits](assets-09.md).

The first pushed macOS Debug/Release/Sanitizer CI jobs and C/Python CodeQL scans
passed. New Linux jobs build SDL3 from a pinned commit and run CPU tests in
Ubuntu 24.04, independently of macOS. Run the same build on Linux with
`bash .github/scripts/linux-build.sh` after installing the workflow's packages.
Use `AF_BUILD_CONFIGURATION=Release` or `Sanitizers` to select a variant.
GPU tests are excluded from hosted CI; a software/container build does not prove
native Linux desktop behavior. The local Linux environment is ARM64/GCC 13
(Clang 18 for sanitizers); GitHub's Ubuntu runner is x86-64 and gets its first
run after the workflow is pushed.


## 0.10 performance and animation checks

CPU tests cover HDR job cancellation, shadow selection, glTF interpolation and
malformed tracks, clip control/blend continuity, skeletons/normalized weights,
malformed influences, independent scene instances, v10 persistence and demo
regeneration. GPU tests compare skin silhouettes/normals and moving shadows,
posed picking, fixed-step animation, reload, the actual editor preview controls,
and both simple/complex C demo modules. The animation export test removes the
source project and relocates both runnable packages. Sanitized players are
explicitly rejected for distribution. The suite has 38 CPU/source checks on
macOS, 37 on Linux; hosted jobs exclude its 23 desktop graphics checks.

`afterlight_profile` produces fixed-camera CSV measurements rather than asserting
an FPS threshold. See [profiling and demo commands](development-010.md).


## 0.11 animated character checks

See [the development notes](development-011.md) for the two playable scenes,
64-rig CPU benchmark and limits. `afterlight.third_person`, `locomotion`,
`locomotion_player`, `animation_events`, `root_motion`, `locomotion_runtime`,
`character_game` and `animation_stress` cover CPU controls, blended poses,
marker boundaries/overflow, collision-resolved motion, diagnostics and scaling.
The animation-data regression also rejects malformed marker metadata.
`character_demo_play`, `character_simple`, `character_course`, `character_crowd`
and `character_export` cover the real C module, camera, renderer and relocated
exports. `afterlight.dco` runs the seven sign-off-checker regressions.

## 0.12 reusable tool checks

New CPU checks cover named input/deadzones/persistence, prefab ID/spawn remapping,
state serialization/rejection and deferred scene changes. Alpha pixel tests cover
opaque-before-blend ordering, cutoff discard, sorted glass and resize; shadow
pixels also verify texture/material-alpha cutouts and cache invalidation. The
`afterlight.toolbox` real-module test combines the systems. `toolbox_export`
verifies portable input maps, scene aliases and prefab dependencies after source
removal. See [the workflow and known limits](development-012.md).

## 0.13 physics checks

CPU tests cover body validation/persistence, fixed-tick gravity, force/impulse
motion and angular response, primitive contact manifolds and broadphase pruning,
four-box stacking/friction, restitution and render-rate independence, sleep/wake,
lost support, thin-wall sweeps, pushing/grabbing/throwing, player proxies and
editor undo/redo. Dynamic rotation interpolation crosses an Euler gimbal boundary
without turning the long way. `afterlight.physics_demo` exercises a real C module
with Vulkan validation, pause/single-step, reload and both physics rooms.
`physics_export` runs the same sequence from relocated exports after deleting
the source project. See [the supported physics scope](development-013.md).

## 0.14 mesh and constraint checks

CPU regressions cover BVH bounds/ownership, transformed mesh rays and capsule
sweeps, rigid mesh stacks, box/pair/angular CCD, distance pendulums, hinge limits,
joint save/load/remapping and editor history. `mechanical_scene` runs the simple
mesh room before the 10-joint mechanical playground and checks finite bodies,
anchor lengths and hinge alignment/limits. Native `mesh_reload` checks successful
and rejected CPU/GPU geometry replacement. `mechanical_demo` uses the real C
module for pushing/grabbing/throwing, pause/step, reload and edit isolation.
`mechanical_export` checks relocated play of both scenes after source removal.

Run the full native Debug and ASan/UBSan suites serially for window focus, and
Linux ASan/UBSan CPU tests with `ctest --test-dir BUILD -LE graphics`. Physics
timing samples are machine-specific diagnostics, not performance gates.

## 0.15 physics and machinery checks

`physics_landing` reproduces the old launch bug with real character stepping,
center/edge/corner landings and a second jump. `physics_broadphase` checks sparse
continuous scenes. `physics_islands` checks group sleep/wake and lost supports;
`joint_stress` exercises 12 links / 16:1 mass ratios at 30/60/144 Hz.
`joint_diagnostics` compares reaction to a suspended weight; `hinge_motor` checks
acceleration, torque budgets, target speeds, braking, stops and persistence.
`joint_gizmo` checks world/body conversion, snapped drags and grouped history.

`motor_scene` runs the powered-door room before the machinery playground, checking
finite bodies, joint errors and motor torque budgets. `machinery_demo` runs their
actual C module through brake/reverse/reset, pause/single-step and hot reload.
`machinery_export` removes sources, relocates both exports and repeats Play.
The standalone Release `afterlight_physics_benchmark` reports 64/128/256 bodies
without enabling the whole test suite; timings are diagnostics, not CI gates.

## 0.16 compound checks

CPU regressions cover bounded part tables/IDs, transformed per-part queries and
empty space, density/COM/rotated inertia, shared-body contacts, pivot publication,
angular and offset-tip CCD, editor fitting/history, prefab owner remapping and
transactional deletion. Prop scenes run simple-first and exercise an imported
L-prop, compound steps and an open five-part cup with falling payloads. Native
C Play checks compound prefab spawning, COM consistency, interaction, motor
controls, reset and hot reload. Exports remove their source, relocate and repeat
Play. Vulkan validation and sanitizer suites retain all earlier regressions.

## 0.17 water checks

`water_pool` checks equilibrium, sinking and interactive waves at all qualities.
`water_river` checks slopes, boundaries and downstream drift; `water_scenes` and
`water_channel` run the simple and harder authored scenes. `water_particles`
checks cup retention, spilling, conservation and reset; `water_particle_limits`
checks emission controls, blocked-nozzle stop, capacity, bounded meshes and cleanup.
`water_coupling` checks bounded momentum transfer to a dynamic thin receiver. `water_data`
checks saved settings, history, duplication, prefab owner remapping and deletion.
`water_render` checks actual Vulkan pixels for sun/local-light color and intensity,
absorption, quality changes and particle lighting. `water_demo` exercises all six
C demos with pause/single-step and Play isolation. `water_export` removes sources,
relocates exports and repeats C Play and standalone launches.

## 0.18 water checks

`water_sleep` verifies frozen fields/particles and wake events. `water_boundaries`
checks light/heavy thin receivers, moving/rotating panels and downhill sliding.
Cup-retention tests initialize quiescent contained fluid; impact tests and labs
allow splashes and account for escaped volume. `swimming` compares 30/60/144 Hz
entry, surface balance, diving, jumping, walls and currents. `underwater` and
`refraction` inspect actual Vulkan pixels for medium transitions, region bounds,
IOR-dependent geometry shifts, foreground rejection, strength, resize and saved
settings. Existing water Play also verifies streaming without mesh-wide waits.

`water_course_scene` runs both authored maps and transfers real particle volume
through the expedition chute. `water_course` drives the C module through actual
walk/swim/dive/current movement, positioned checkpoint/goal visits, hot reload,
restart and scene transition, then operates the real pouring objective.
`water_course_export` removes sources, relocates both maps and repeats C Play.
Release CPU/mesh and native render benchmarks are separate executable targets;
their timings are diagnostics, without machine-specific CI thresholds.
