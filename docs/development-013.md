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
