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
