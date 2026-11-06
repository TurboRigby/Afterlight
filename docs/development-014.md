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
