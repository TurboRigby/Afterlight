# Afterlight 0.11: playable animated characters

Development follows eight separate commits: third-person controls, locomotion
states, speed blending, animation events, optional root motion, debugging tools,
larger-scene profiling, and simple/obstacle-course demos with export checks.

## Third-person controls

`third_person.h` builds on the kinematic capsule controller. Fixed ticks move
relative to camera yaw, normalize diagonal input, choose walk/run speed, jump
and smoothly turn a visual heading. Mouse look is independent per-frame input.
The camera follows an elevated target with a sphere sweep. It retracts
immediately at geometry and eases outward as space clears; trigger colliders
are excluded. Pass the player's collision ID in the query filter. A wall very
close to the pivot may bring the camera closer than its preferred minimum.

## Locomotion states

`locomotion.h` selects Idle, Walk, Run, Jump or Fall from collision results.
Horizontal speed and grounded vertical velocity drive the state; hysteresis
avoids oscillation around standing and running thresholds. Landing and takeoff
are edge signals. `sandbox/assets/locomotion.gltf` is an original nine-joint
character with all five clips. Regenerate it with
`python3 scripts/generate_animation_assets.py`.

## Speed blending

The owned `af_locomotion_player` blends adjacent Idle/Walk/Run clips at measured
speed, using one normalized phase so different clip lengths remain synchronized.
Translation/scale interpolate linearly and rotations use quaternion SLERP.
Airborne transitions capture the current mixed pose, including interrupted fades.
`af_engine_locomotion_step` exposes this fixed-tick driver to C modules; it takes
over automatic playback until explicit Play, Stop or Seek. Reloading an asset or
changing scenes discards its driver. Sampling never changes authored scene data.

## Animation events

glTF animation `extras.afterlight_events` accepts up to 64 `{name,time}` markers.
Names contain 1–31 bytes and time is finite in `[0,clip duration)`. Invalid event
metadata rejects the asset. The bounded collector handles exact boundaries,
multiple loops and reverse traversal. Events exclude the old time and include
the new time; seeking emits none. C modules drain `animation_events`; overflow
is counted. Ground blending emits markers from its dominant moving clip once,
and collision-driven landing adds `land`. Each instance queues at most 16 events.

## Optional root motion

Configure `locomotion_root_motion(root,node,enabled)` before driving a rig. Both
modes strip horizontal translation from the selected node to avoid double
movement. Enabled extraction blends clip displacements across full loops and
returns `locomotion_root_delta` in model space each fixed tick. Rotate it into
world space and pass it to `move_character_root_motion`: capsule sweeps, wall
sliding, slopes and platform carry still apply. Gravity and jump own Y; turning
is controller-driven. Air movement uses the regular controller. The playable
fixture uses glTF node 10 with forward motion on -Z. Root motion is translation
only; arbitrary root rotation/vertical trajectories are not extracted.

## Debugging

The editor Animation panel can overlay posed joints and parent connections,
including during Play with the game camera. It reports locomotion state, speed,
phase, adjacent weights, extracted root delta, emitted/queued/dropped events and
the last event. Queries leave the event queue and scene history unchanged.
Transport controls are disabled while gameplay drives locomotion. Bone queries
report total count and safely write only the caller's capacity.

## Larger-scene profiling

`afterlight_animation_stress sandbox/assets/locomotion.gltf` measures CPU pose,
hierarchy and palette work for 1/16/64 shared-asset rigs (13/208/832 objects),
with 30 warmup ticks followed by 120 samples. It verifies all posed joints and
reports mean, p95 and maximum without a machine-specific timing assertion.
On this Apple M4 Debug build, the 64-rig mean fell from 2.385 ms to 1.381 ms
after indexing scene IDs once per update and reusing refreshed instances.
Transition capture buffers are also reused. These are CPU-only measurements;
GPU skinning/shadows are exercised separately. The Performance panel exposes
animation CPU time, included in its render total.


## Playable demos and compatibility

Run `run_character_editor` first, then `run_character-course_editor`; press Play.
The matching `run_character_player` and `run_character-course_player` targets
start standalone games. WASD walks, Shift runs, Space jumps, E switches between
velocity and root motion, R restarts, Escape releases the mouse. The course keeps
ordered checkpoints, goal locking, moving platforms, falling/respawn and timers.
Right mouse in editor Play temporarily returns to the editor camera.

The C module uses a 0.65-scale visual rig attached to the unit-scale capsule.
Walk/run speeds are 1.3/3.25 world units per second; the rig's 2/5 model-space
speeds and extracted displacement are scaled consistently. Velocity mode blends
from collision-resolved speed. Root mode uses requested speed and the preceding
contact result before moving through collision; contacts reach its animation on
the following fixed tick. The module assumes this fixture's root node 10 and
all five named clips. Crowd actors are visual-only, without individual controllers.
The 64-character stress scene shares assets and has independently driven poses.

Regenerate scenes deliberately (this overwrites these three sample files):

```sh
cmake --build --preset debug --target afterlight_character_builder
build/debug/afterlight_character_builder "$PWD/sandbox/assets" \
  "$PWD/sandbox/scenes/obstacle.afterlight.json" "$PWD/sandbox/scenes"
```

Scene format remains **10**, reading 1–9. These are runtime controls and do not
add serialized fields. C game ABI is **10**: rebuild custom modules against the
current headers. Source archives include the new C code, original assets,
contribution instructions and DCO. No editor or custom module should be hot-loaded
across an ABI change; restart the editor after rebuilding.

The CPU role regression runs the course after renaming objects and verifies
ordered checkpoints, respawn, a locked goal, finish, restart and timer freezing.
A Vulkan Play test loads the real module, exercises movement states/events/root
motion, checks startup camera and authored-scene isolation, and resizes during
course play. Separate simple/course/crowd smoke tests check validation. Export
checks remove the source project before relocating and running both games.


## Verified results

On Apple M4/MoltenVK, the 75-test Debug suite and 75-test ASan/UBSan suite
passed with synchronization validation. After isolating native mouse motion
from the scripted character inputs, the affected Debug checks passed three
consecutive runs; the final sanitizer control check also passed. The Linux
Clang 18 ASan/UBSan build passed all 46 CPU tests. Both Release character exports
ran after source deletion and relocation. Sanitizer exports are rejected.

The Release fixed-camera profiler on the 64-rig scene measured 0.550 ms mean CPU
animation work and 2.334 ms mean completed GPU frame time (120 samples after 30
warmup frames, 1280×800 window at 2× display scale). This profiler uses automatic
clip playback and excludes presentation from GPU timestamps. These numbers are
machine-specific and are not pass/fail budgets. Run:

```sh
build/release/afterlight_profile sandbox/scenes/character-stress.afterlight.json
```
