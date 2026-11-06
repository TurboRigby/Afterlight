# Afterlight 0.13: physics and interaction

Eight commits cover body components, motion/impulses, collision detection,
contact solving, sleep/fast bodies, character interaction, editor tools and
simple-to-complex integration/export verification. Original code remains C17.

## Body components

Scene version 13 adds `body`: enabled/type, mass, friction, restitution, damping,
gravity scale, sleep/continuous flags and initial linear/angular velocities.
Versions 1–12 load disabled defaults. Dynamic bodies require a root sphere/box
collider, centered origin and positive scale. Static scene colliders continue
working without a body component. Game ABI is 12; rebuild custom C modules.

## Fixed-tick motion

`physics.h` exposes an owned, main-thread world, bounded forces/point impulses,
state queries and diagnostics. The engine advances it after game fixed callbacks,
using internal substeps no longer than 1/240 s and the existing pause/step clock.
Quaternion rotation drives angular motion; box/sphere inertia uses scaled shapes.
Runtime velocities are written into the Play scene so clones/reloads retain them.
Static colliders stay fixed; externally moved kinematic shapes derive point
velocity from their pose changes. Limits: 1024 sphere/box shapes, 200 linear and
100 angular units/s, and a 1,000,000-unit world radius. C modules call the host.

## Contacts and broadphase

A deterministic sweep-and-prune index filters pairs by bounds and collision layer.
Sphere/sphere, sphere/OBB and full 15-axis OBB SAT detect contacts. Face clipping
produces up to four manifold points; edge cases use closest support edges.
Only sphere/box shapes participate in rigid-body contacts; trigger shapes and
legacy character capsules remain handled by the existing query/controller APIs.
Shape dimensions must be at least .01 world units. Contact output is capped at
8192 points and reports the latest substep. Sparse-scene tests check pair pruning.

## Contact response

The sequential impulse solver combines linear/angular effective mass, warm-started
normal impulses, Coulomb friction and a restitution threshold. Twenty velocity
iterations and eight split position iterations run per substep. Face-manifold
contacts keep flat stacks stable without adding penetration energy to velocity.
A four-box stack and a sliding-friction regression settle within bounded tolerances.
This is a primitive solver, not a general constraint/joint or mesh-collision system.

## Sleeping and continuous movement

Resting, supported bodies sleep after 0.6 s below velocity thresholds. Forces,
impulses, impacts, moving supports and support removal wake them. Sleeping bodies
act immovable until woken. Continuous movement sweeps spheres against current
static/kinematic shapes before integration. Boxes use their inscribed sphere as a
conservative center-path guard; full rotating-box or dynamic/dynamic TOI is not
implemented. Internal substeps and contact response handle ordinary dynamic pairs.
Regression checks cover sleep/wake, removed support and a 180-unit/s sphere
against a .02-unit-thick wall.

## Character and object interaction

C hosts expose bounded capsule pushing, grab targets and release/throw impulses.
Grabs use a critically damped, force-limited spring on the picked local point;
held objects keep their mass and collisions. Objects over 50 mass units cannot
be grabbed. Throw velocity is capped at 40. Removing a held object releases it.
Player capsules enter the rigid-body world as conservative upright box proxies,
so falling objects meet a kinematic obstruction; the exact capsule controller
still handles player movement. This is one-way kinematic character interaction,
not a dynamic/ragdoll character. CPU tests cover pushing, holding, throwing and
an object settling on the player proxy.
