# Fixed simulation, collisions and the obstacle course

Afterlight 0.5 introduced a 60 Hz simulation loop, interpolated rendering, primitive
colliders, collision queries and a kinematic capsule controller. Version 0.6 adds
projects, editable player/camera components, checkpoints, timing and audio. Engine, editor
and gameplay remain C17; no physics library or C++ runtime was added.

## Run

```sh
cmake --preset debug
cmake --build --preset debug --target run_playground
```

The mouse is captured at startup. WASD walks, mouse movement looks around,
Space jumps and R starts a new run. Escape releases capture; when already released,
Escape exits. Click the viewport to recapture. The crosshair marks the camera direction. The course contains a
walkable ramp, a steep slope, a wall, hurdles, raised jumping platforms, a moving
platform and a green finish arch. Entering the arch's trigger completes
the course after both checkpoints. A trigger below the floor and a low-Y fallback
return a fall to the latest checkpoint. Ground
under the jumping route lets a missed jump recover via R or a return to the ramp.

Open the same course in the editor:

```sh
cmake --build --preset debug --target run_playground_editor
```

This uses `--playground`, opens the bundled course as an untitled document and
selects `afterlight_playground_game`. Save As creates your own level. Play/F5
starts the first-person C module with the mouse captured. Escape releases it;
click the viewport to recapture. Hold right mouse for the editor's free camera;
release it to return to the player. Pause freezes simulation. **Step 1/60** pauses
if necessary and advances exactly one simulation tick. Stop restores the edit
scene/camera. Rebuild `afterlight_playground_game` to hot reload it during Play.

## Player, camera and course editing

Open **Project** to select/save the startup scene and C module together. The
playground template selects its own module. Select Player in the hierarchy and
scroll below Collider to **Gameplay components**. Edit walk speed, gravity,
jump height, capsule dimensions, mouse sensitivity and the Spawn reference.
Enable Camera/Primary game camera and edit FOV, eye height and near/far clips.
Other objects can have a Camera component too. Play uses the selected primary
camera; the course requires its player camera to be primary.

The controller requires a root object with unit scale and a solid zero-centered
capsule matching its player settings. Editing player settings synchronizes its
collider. Spawn position is the capsule center. Camera eye height is relative to
that center. Components participate in scene save/load, undo/redo and duplication.
Game code uses components/IDs; display names can be changed freely.

Course role selects Spawn, Checkpoint, Goal trigger, Restart trigger or Moving
platform. Checkpoints require enabled trigger colliders and consecutive unique
orders starting at 1. Their world-space centers are the respawn capsule centers;
place them safely above a supporting platform. Up to 16 root moving platforms
are supported by the sample module. Their sample motion remains a sinusoid.

The shipped course has two checkpoints: the green landing and blue jump platform.
The timer starts on the first movement/jump, advances in simulation time, pauses
with the game and freezes at the finish. Falls increment the fall counter and
return to the latest checkpoint without clearing elapsed time. The finish arch
requires both checkpoints. R clears progress/time/falls and returns to the
original spawn. The HUD and original jump/landing/checkpoint/finish sounds work
in both the editor and standalone game. Module hot reload reinitializes private
course progress and starts the player at spawn.

F6 or the Play toolbar's Maximize button expands the game viewport. F6 restores
the editor layout. Crosshair, paused state, timer/progress and mouse ownership
hints stay visible. Stop restores the edit camera/scene/layout. Escape releases
the mouse; clicking the viewport recaptures it. Holding right mouse temporarily
uses the editor fly camera and suppresses gameplay controls.

## Simulation contract

`af_game.update` remains a per-render callback for UI, mouse input and camera
look. Optional `af_game.fixed_update` runs after it with `AF_FIXED_DELTA = 1/60`.
The clock accumulates elapsed wall time, accepts at most 0.25 s per frame and runs
at most eight ticks. It discards excess whole ticks rather than keeping an
unbounded backlog; stats expose discarded time. Pausing clears the accumulator;
a single-step request is one tick, independent of the render delta.

Key-down state is current input. Press edges are buffered across render frames
with no simulation tick, exposed once to the next fixed callback and cleared
before a second catch-up tick. Focus loss and pause/resume clear buffered edges.
The per-render pressed API still reports the current event batch.

Before each tick, the engine clones the attached CPU scene. Rendering interpolates
previous/current parent-local position, scale and shortest-angle Euler rotation,
then composes the hierarchy. New/reparented nodes use current local state.
Visibility, materials, colliders and other scene properties remain discrete.
Shadow casters and visible meshes use the same interpolated model matrices.
Camera positions set by simulation are interpolated; per-render look rotation
and independent free-camera movement stay responsive. This introduces the usual
one-tick presentation delay without changing collision/query state.

Scene replacement, pause changes and `af_engine_reset_interpolation` discard the
old presentation pose. Call the latter after teleporting. Paused/stepped views
show the current pose. Scene snapshots copy CPU data each tick; this is a simple
small-scene implementation, not a large-world optimized pose cache. Queries and
editor picking use current simulation geometry, not interpolated presentation.

## Colliders and queries

Each object has a collider: None, Box, Sphere or Capsule. Center and dimensions
are object-local. Box half extents are positive. Sphere/capsule radius is positive;
capsule `half_height` is half the straight segment, so full height is
`2 * (half_height + radius)`. Enabled and trigger flags are independent of mesh
visibility, allowing invisible walls/triggers. Layer is a nonzero 32-bit mask.

World boxes are oriented boxes. Spheres and capsule radii conservatively use the
largest world scale; a capsule segment follows transformed local Y. Nonuniform
scale can therefore enlarge a rounded collider. Zero scales or sheared world
transforms are skipped, with an inspector message. Parent TRS otherwise applies.
There is no triangle-mesh collider or convex-hull collider yet.

`af_scene_raycast` returns the nearest hit along a normalized direction, up to
the requested distance. Rays starting inside return distance zero. Sphere and
upright world-Y capsule overlap queries return total hit count, even when the
caller supplies a smaller output buffer. Capsule sweeps conservatively advance
against OBB/sphere/capsule distance functions to prevent tunneling through thin
surfaces. Hit data includes object ID, normal, point, distance/fraction,
penetration and trigger state.

`af_query_filter_default` selects all layers and excludes triggers. Filters can
ignore two object IDs, choose a layer mask and include triggers. Hidden objects
still collide; disabled colliders do not. Queries inspect CPU scene data only.

## Character controller

`af_character_step` moves an upright capsule through static/kinematic colliders.
It resolves starting overlaps, sweeps movement, projects remaining movement
along contacts, applies gravity/jumping, checks the slope limit and snaps a
previously grounded capsule down small gaps. Steep surfaces do not permit uphill
climbing; gravity can slide the character downhill. Air motion retains momentum
and accepts limited directional control. Moving platforms carry a grounded
character through a stored local anchor; carry movement is swept against other
objects. Removing/disabling the supporting platform releases grounding.

Position is the capsule center. The default radius is 0.35, straight-segment
half-height 0.55, speed 4.5, jump speed 6.5, gravity 18 and slope limit 45 degrees.
The scene/character state changes only after a valid step. Triggers do not block
movement. This is a kinematic controller, without rigid-body impulses, friction
materials, pushing dynamic bodies, automatic stair climbing, crush resolution,
ragdolls or joints. Queries use linear scene scans, appropriate to these small
levels. Use consistent world units and fixed ticks.

## Editor and persistence

The Inspector's Collider section edits type, enabled/trigger flags, center,
dimensions and a layer bit. The full mask is shown; choosing a bit replaces it
with a single-bit layer. Edits participate in undo/redo, clone and subtree
duplication. **Colliders** in the viewport toolbar toggles x-ray wireframes:
cyan for solids, purple for triggers and amber for the selected object. These
show simulation geometry. The toggle itself is session UI state.

Scene format **7** retains saved gameplay/player/camera components; versions 1–6
remain readable, with legacy defaults and old-course component migration. See [scene format](scene-format.md).
C game ABI is **6**, including collision/character/input/camera host functions
and an optional per-render module callback. Module `update` is the fixed callback.
Game modules call the host table and do not link a separate engine instance.
The standalone playground and reloadable module share `game/playground_game.c`.

## Verification

The CPU simulation test compares 30/60/144/240 Hz rendering over identical fixed
simulation time, checks catch-up limits/paused steps and hierarchy interpolation.
Collision tests exercise rays, caps, overlap, trigger/layer filtering, thin-wall
sweeps, jumping and wall sliding. Character tests cover walkable/steep slopes,
downhill sliding, platform carry/removal and 30/144 Hz independence. The actual
course test runs its C gameplay against the shipped scene, including trigger
restarts. Collider data tests cover legacy v4, strict v5 JSON, undo/duplicate and
all primitive outline builders.

The Vulkan fixed-loop test verifies single-step execution, buffered quick key
presses and a forced 200 ms stall. Editor smoke checks paused stepping and collider
outlines alongside its existing UI/file regressions. The standalone playground
also has a bounded graphics smoke test. See [testing](testing.md) for full-suite
commands and the macOS sanitizer module-unload regression and fix.

## Player, New Project, models and export (0.7)

Ctrl/Cmd+Shift+N opens the Project panel, which includes a New Project wizard and **Export saved project**.
Enter a name/new destination for creation; existing directories are preserved.
The wizard generates a C module, scene, headers and CMake project. Build that
module before Play. Export packages the generic player with saved scene/assets
and module. Full commands and platform limits are in [shipping](shipping.md).

The glTF importer preserves hierarchy and per-primitive materials, including
base-color/normal/metallic-roughness images. The Assets panel adds packed surface
map import/assignment and **Reload changed assets**. Watch reload updates GPU
geometry/images with last-good retention; hierarchy and material document edits
are preserved. Reimport to update model structure or material definitions.

## Physics authoring (0.13)

The Inspector exposes body type, mass, friction, bounce, damping, gravity,
sleep/continuous flags and initial velocity/spin. Dynamic bodies need a centered,
enabled sphere/box collider on a root with positive scale. Enable **Physics** in
the viewport toolbar to see awake/sleeping bodies, velocity vectors and contact
normals. Performance shows physics CPU time and counts. Pause and **Step 1/60**
advance the actual solver. Stop restores the authored scene. See
[the physics rooms and limits](development-013.md).

## Mesh collision and joints (0.14)

For a static imported model primitive, choose **Imported mesh** in Collider. The
editor prepares its CPU BVH and retains the prior collider if cooking fails.
Collider debug displays the first 256 triangles.

Select a dynamic box/sphere, expand **Joints**, and add a distance or hinge
constraint. Body A is the selected creator; Body B can be another collider or
world. Edit anchors in body units, hinge axes and perpendicular zero references,
and optional degree limits. **Physics** overlays draw anchors, axes, reference
rays and limit arcs in Edit and Play. Edits/deletion support undo and redo.
See [the full physics contract](development-014.md).

## Powered mechanisms and viewport joints (0.15)

Use Hinge motor, Speed and Max torque in a hinge's inspector. Speed measures
body B relative to A, in radians per second; torque uses newton-metres.
During Play the inspector reports sleeping state, anchor error, angle and cached
reaction forces/torques. The Performance panel separates physics phases and
shows islands, cache probes and CCD clamps.

Choose Drag anchor A/B or Rotate axis A/B for viewport handles/rings. Snap uses
0.5 units for anchors and 15 degrees for orientation. Axis rotation preserves a
perpendicular zero reference. Release commits one grouped undo operation; Escape
cancels. Finish joint edit restores the object tools. Body anchors use unscaled
collider axes, including on scaled objects; world endpoints use world units.

Start with `run_motor-room_editor`, then `run_machinery-playground_editor`. In
Play, Z brakes/resumes motors, X reverses them, R restores authored poses/targets
and N switches rooms. The usual movement, pushing, grabbing and throwing remain.
