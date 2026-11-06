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
