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

Dynamic water meshes now keep a CPU snapshot and one persistently mapped vertex/
index allocation per frame in flight. Updates happen after that frame's fence;
capacity grows geometrically, unchanged revisions reuse buffers, non-coherent
memory is flushed, and removed meshes retire without reindexing live assets.
Normal water updates/removals no longer call device-wide idle. Resize/shutdown
and unrelated static asset operations retain their required synchronization.
The implementation follows Khronos' [frames-in-flight resource ownership](https://docs.vulkan.org/tutorial/latest/03_Drawing_a_triangle/03_Drawing/03_Frames_in_flight.html).

Engine diagnostics expose water upload bytes/count/time and static mesh idle
waits. The 18-case Release render benchmark reports zero mesh idle waits during
each case. At high pouring quality uploads average ~0.032 ms on this M4 run;
frame pacing still dominates render-loop wall time, so no uncapped-FPS claim is
made. Native water Play verifies uploads, constant idle-wait count, scene
retirement and cleanup; water pixel, asset-reload and material regressions pass.
The public diagnostics extension uses C module ABI 19.

Calm height fields now stop after their height/velocity falls below thresholds;
wind and disturbances wake them. Floating rigid bodies can use balanced buoyancy
as an island support and retain sleep in calm water. Authored fluid changes or a
new disturbance wake affected sleeping islands conservatively. Settled contained
particles freeze as a set after a quiet interval; emission and changed solid
poses/geometry wake them. Sleeping fluid uses unchanged meshes rather than
regenerating geometry for interpolation alone. Stats expose sleeping fields and
particles. `water_sleep` checks fixed sleeping positions and wake events, while
pool/particle and existing rigid island/sleep regressions pass.

Fluid contacts now retain up to four distinct solid normals per particle and
compute reaction impulses using the particle mass plus the receiver's linear/
angular effective mass. Kinematic panels are swept through intermediate poses
across physics substeps; particles move against the relative swept shape, rather
than testing only its final position. Tests cover 0.1/10/1000/10000 kg receivers,
2 cm thick panels, translation and 90-degree rotation, existing cup retention/
spilling, calm sleeping and all six C demos. The minimum-dimension validation
allows floating-point roundoff at the 1 cm half-extent boundary. This remains a
bounded position-based coupling approximation, without a global pressure solve.

The optional C swimming controller reuses normal character movement on land and
uses swept capsule sliding in water. It tracks immersion with entry/exit
hysteresis, neutral surface buoyancy, head submersion and a brief surface-jump
exit interval. Vertical input dives/ascends and horizontal input is relative to
water current. It excludes pourable particles. `swim_config_default` and
`swim_character` are available through the C host. Tests compare 30/60/144 Hz
surface equilibrium, diving, ascending, surface jump, walls, currents, dry exits
and transactional rejection of invalid input.

Underwater rendering clips the camera-to-fragment path to the active water
region, applies RGB absorption/scattering, and blends through a 20 cm surface
band. Sky and geometry use the same treatment; UI remains separate. River fog
uses the local tangent region near the camera, so distant bends remain an
approximation. Scene format 20 saves an `underwater` rendering toggle; old scenes
receive the enabled default. Pixel tests verify absorption, disable, surface
continuity, dry-region exclusion and persistence; existing water, environment
and material data/pixel regressions pass. Shared GLSL includes are now included
in corresponding-source archives and license-header verification.

Scene refraction snapshots opaque HDR color and depth into independent per-image
textures, then resumes a compatible load pass for transparent geometry. Water
projects an IOR-dependent refracted lookup, clips it to the scene viewport and
rejects foreground-depth samples. Snapshot barriers and frame descriptor updates
are explicit; the shader never samples its active color attachment. The
implementation uses Vulkan 1.1 image copies, following the [Khronos image-copy requirements](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdCopyImage.html).

Format 21 saves `refraction` and `refraction_strength` (0–2); earlier scenes get
the enabled default at strength 1. This is screen-space opaque-scene refraction:
o off-screen geometry, multiple refracted layers or full ray tracing. Underwater
fog on the opaque snapshot is retained without applying it a second time to the
transmitted color. Pixel tests verify shifted underwater stripes, IOR, foreground
rejection, strength/toggle, resize and saved settings. Water/underwater, alpha and
material regressions pass with Vulkan synchronization validation.

`run_water-course_editor` starts the first-person swim/raft course;
`run_water-expedition_editor` adds a river, cargo, another raft and a lab where
50 litres must reach the receiving cup before finishing. `_player` variants are
also available. WASD moves, Space jumps/ascends, Ctrl dives, Shift runs on land,
F/V grab/throw and E near the source cup tips/returns it. R reloads all authored
state and N changes courses. The HUD shows controls and the current objective.
The course module supports character/progress state reload and treats external
player transform edits as teleports. Its player is kinematic and does not add
physical body weight to rafts.

The combined course exposed a fluid sweep bug: a contact discarded remaining
tangential travel and stalled fluid on ramps. Sweeps now retain the slide, with
bounded contact friction and equal/opposite solid reaction. A downhill regression
and the actual receiving-cup transfer cover this path. Quiescent cup tests keep
100 particles; falling-fluid labs may splash outside, with escaped volume counted.
Course checks drive real movement and pouring, use positioned visits for ordered
checkpoint/goal logic, and cover restart, scene change, hot reload and Edit
isolation. Source removal/relocation is tested for both standalone courses.

Verification on 6 November 2026: all 143 macOS Debug checks passed, including
47 graphics checks. AddressSanitizer/UndefinedBehaviorSanitizer passed the same
coverage in separate 96-CPU and 47-graphics runs; macOS leak detection was
disabled. Linux Clang 18 passed 95 CPU checks with address, undefined-behavior
and leak sanitizers enabled. Native Linux graphics and Windows remain unverified.
After the final input-comparison cleanup, the Debug course and sanitizer graphics
checks passed again. Both Release courses and all six Release labs passed source
removal, relocation, C Play and standalone launches. Sanitized player exports
remain intentionally rejected; distribution tests use normal Debug/Release builds.

Final Release measurements cover all 18 scene/quality cases. The 90-tick CPU run
reports 6.55 ms physics / 0.67 ms mesh generation for the high-quality pouring lab;
the 60-frame render run reports zero mesh-wide idle waits in every case and
0.031 ms mean water upload for that case. Raw results are in
build/release/018-water-final.csv and 018-render-final.csv. Scene evolution and
presentation pacing affect measurements; these are diagnostics rather than a
claim about uncapped frame rates.

Current scene format is 21 and C module ABI is 20. Rebuild game modules and restart
older engine/editor binaries. Fluid coupling, local river fog and screen-space
refraction retain the approximations described above; full CFD, off-screen
refraction and layered transparent refraction are outside this milestone.
