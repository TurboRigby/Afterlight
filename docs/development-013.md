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

## Editor workflow

The Inspector adds undoable rigid-body settings and initial velocity/spin. Dynamic
body edits reject incompatible parenting, collider changes or negative scale.
Physics debug overlays draw awake/sleeping centers, velocity vectors and contact
points/normals. Performance shows CPU time, bodies, sleep, contacts, broadphase
pairs, substeps and CCD hits, with a selected-body wake action. Play pause and
single-step use the same fixed clock as physics; Edit scenes remain authored data.
Play and prefab spawning preflight body/shape limits before entering simulation.

## Simple and harder demos

Build `run_physics-drop_editor`, press Play and watch the two crates and ball
settle. Then run `run_physics-playground_editor` for a four-box stack, ramp,
rolling balls, dominoes, moving support, thin wall and fast projectile. The
matching `_player` targets run standalone. Both scenes retain the animated
third-person character. WASD/Shift/Space walk/run/jump, F grabs/releases an
object under the reticle, V throws, Q spawns a dynamic crate, N switches rooms,
E toggles root motion, R resets authored bodies and removes spawned crates,
and Escape releases the mouse. Grabs are released on scene change or reload;
body poses and velocities survive hot reload through the runtime scene clone.

Enable the Physics viewport overlay and Performance panel for diagnostics.
The drop room is tested before the harder room. The original fixture generator is
`python3 scripts/generate_physics_assets.py`. All demos/resources are original and
included with covered source archives. Scene format is 13, project format stays 1
and C game ABI is 12. Rebuild modules and restart older editors after upgrading.

## Scope and practical limits

Supported dynamics are centered root boxes/spheres; statics can be oriented,
parented boxes/spheres with nonsheared world transforms. Kinematic bodies are
moved by gameplay transforms, rather than by their initial velocity fields.
The world is bounded to 1024 participating shapes and 8192 contact points.
Physics rejects invalid geometry/limits when entering Play or spawning prefabs.
The simulation is single-threaded, with sequential impulses and no joints,
convex/triangle meshes, ragdolls, soft bodies or gyroscopic torque integration.
Nonuniformly scaled spheres use the largest world scale, matching scene queries.
The dynamic radius/half-extents range is .01–10000 world units.

Continuous collision handling sweeps spheres against the current
static/kinematic geometry. Boxes use an inscribed sphere to guard their center
path; rotating corners and fast dynamic/dynamic pairs still rely on substeps.
Sleeping is a per-body optimization; wake/contact propagation uses thresholds.
The character collision proxy is conservative and does not receive rigid-body
impulses. These limits are explicit; complex physics games need further work.

## Verified results

On Apple M4/MoltenVK, the full 92-test ASan/UBSan suite passed with Vulkan
synchronization validation. The initial Debug run passed 91/92, with a failure
in the existing native editor-camera ownership test. A later repeated run
confirmed a delayed native focus-loss event; the test now waits two event batches
to distinguish it from a stable-focus failure, and passed five consecutive Debug
runs. The complete sanitizer run passed as well. Final targeted checks cover the changed
force API, physics demo and editor behavior. F-to-focus is disabled during Play
so it cannot interfere with the physics grab action. Linux Clang 18 ASan/UBSan passed
all 58 CPU tests. Static analysis reported no issues in the new physics files.

Both Release physics exports ran after their source project was deleted and the
outputs relocated. The integration then exercised dropping/resting/sleep, the
module's grab/throw actions, dynamic prefab spawning, character pushing,
pause/single-step, state-preserving reload and the larger playground. The picking
result is deterministic in this test; raycasts themselves retain their existing
separate tests. Quaternion interpolation and 30/144 Hz render independence are
covered by CPU regressions. Source archives and licensing are rebuilt/checked
after documentation changes. Native Windows/Linux interactive graphics remain
unverified; Linux coverage here is the CPU sanitizer suite.
