# Afterlight 0.14: mesh collision and constraints

Eight commits cover physics stress measurements, static triangle meshes and BVHs,
mesh queries/controller sweeps, rigid-body mesh contacts, stronger CCD, distance
joints, hinge authoring/debugging and simple-to-complex portable demos.

## Stress baseline

`afterlight.physics_stress` measures 16/64 bodies arranged in independent four-box
stacks with an 8:1 top-body mass ratio. Forty warmup ticks precede 60 CPU timing
samples. CSV reports mean/p95 CPU time, maximum penetration and pair/contact
counts. Bounds and finite-state assertions are independent of machine speed.
Moving supports, restitution, 30/144 Hz timing and sleep/wake retain their existing
regressions. These are bounded primitive-physics tests, not a universal stability
claim or a machine-specific timing budget.

## Static meshes and queries

Scene format 14 adds the `mesh` collider type, using an object's mesh asset.
`collision_mesh.h` supports CPU uploads and transactional preparation from imported
OBJ/glTF mesh assets. Up to 64 immutable meshes / 262144 total triangles are shared
by scene clones; degenerate triangles are removed and invalid data retains the
last valid mesh. Mesh bodies must be static, with zero collider center and a
nonsheared invertible world transform. Source references remain in the manifest.

BVH-backed two-sided raycasts, sphere/capsule overlaps and conservative capsule
sweeps use transformed triangles, including mirrored/nonuniform TRS. The character
controller and orbit camera consume these existing query APIs automatically.
CPU callers prepare mesh assets before querying; engine asset loading does it.
Queries treat triangle surfaces rather than defining a solid interior volume.

## Rigid-body mesh contacts

Sphere/triangle distances and 13-axis box/triangle SAT feed the rigid-body solver.
Triangle clipping reduces coplanar patches to a wide, bounded contact footprint.
The BVH filters world-space candidate triangles before narrowphase. Dropped boxes,
spheres and a three-box stack settle on an imported two-triangle floor; existing
primitive stack/friction/sleep tests remain in place. Triangle surfaces are static;
convex decomposition or dynamic concave mesh bodies are not provided.

## Continuous collision

Full box extents, angular velocity bounds and relative body motion now participate
in conservative advancement, including BVH-filtered triangles. Regression cases
cover thin walls, two approaching fast spheres and a rotating box corner. Low-speed
resting contacts stay in the discrete solver. Work is bounded to 80 distance samples
and 16 impact events per substep. The event cap truncates remaining motion and
reports `ccd_clamped`; a nonconverging pair uses its last safe sample as an
approximate impact. A pair receives at most one CCD impulse per substep, so
multiple rebounds between the same pair remain an approximation. This is bounded
CCD for small scenes, not an exact general time-of-impact solver.

## Distance constraints

Up to 128 scene-owned joints use stable IDs and connect a dynamic sphere/box to
another physics body or a world anchor. Anchors are in unscaled body axes about
the collider center; world endpoints use world coordinates. Distance zero is a
point constraint. The iterative impulse solver includes angular anchor leverage
and positional correction. Connected-body collision defaults off. Joint bodies
stay awake until constraint-island sleeping is implemented.

Scene 14 saves joint IDs/settings; clones, body deletion and C game host calls
retain their meaning. Prefabs remap internal body references and reject external
or world anchors. Endpoint edits that invalidate a joint fail transactionally.
Pendulum/chain, save/load, clone, deletion and prefab remapping regressions run
as CPU tests.

## Hinges and authoring

A hinge constrains both anchors and two angular axes while leaving twist free.
Optional limits use a signed angle from A's zero reference to B's, in radians
within [-pi, pi]. Axes and perpendicular zero references are explicit unit vectors.
There are no motors, break forces or constraint-island sleeping yet.

Select a dynamic object in the editor to add distance/hinge joints, choose body B
or world, edit anchors/axes/references and limits, or delete a joint. Changes use
undo/redo. Physics overlays show anchors, connecting lines, hinge axes and limit
arcs; collider overlays show up to 256 imported triangles. Duplicating a subtree
copies bodies and internal/world joints with fresh IDs, offsetting world anchors
with the duplicate; joints to external bodies are omitted.

Free hinge motion, angular alignment, bounded twist, persistence, editor undo/redo
and duplicate reference regressions pass alongside the distance tests.

## Simple-to-complex demos

`run_mesh-room_editor` starts an original imported triangle room with a ramp,
stairs, dropped bodies and a distance pendulum. Then use
`run_mechanical-playground_editor` for a six-body chain with varying masses, a
limited door, swinging beam, seesaw, ramp balls, a fast mesh projectile and a
spinning box. The room has enclosing walls to keep energetic bodies in bounds.
The standalone `_player` targets use the same C gameplay.

WASD/Shift/Space move/run/jump; E toggles root motion; F grabs/releases, V throws,
Q spawns a crate, R resets bodies/player and N switches these two rooms.
**Physics** shows joints and contacts. Source scenes stay unchanged during Play.

Scene format 14 reads versions 1–13; game modules use ABI 13 and must be rebuilt
with current headers. Assets remain external references and export relocatably.

## Verification

All 104 native macOS ASan/UBSan tests pass with Vulkan validation. All 67 Linux
Clang ASan/UBSan CPU tests pass with leak detection. Debug regressions, relocated
Debug exports and optimized Release builds are also checked. The legacy v7/v8
render fixture removes the new root fields when downgrading its version.

A local Apple M4 Debug sample after these changes measured 5.33 ms mean / 5.49 ms
p95 for 16 bodies and 18.92 / 19.53 ms for 64 bodies. Maximum penetration was
0.00364 / 0.00659 units; contacts and pairs were 64 / 256. These CPU-only samples
are diagnostics, not a frame-rate promise. Optimization and larger-island stress
remain future work.
