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
