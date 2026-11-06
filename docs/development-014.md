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
and 16 impact events per substep; exhaustion conservatively truncates motion and
reports `ccd_clamped`. A pair receives at most one CCD impulse per substep, so
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
