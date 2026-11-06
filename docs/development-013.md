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
