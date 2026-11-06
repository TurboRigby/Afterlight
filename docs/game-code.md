# C game code, Play and hot reload

The editor runs `game/demo_game.c` through a versioned C shared-library API.
Start with **Play** or **F5**. The game receives a clone of the editor scene,
including hierarchy, assets and materials. Editor mutation controls are disabled
while playing. Pause suspends game updates; right-mouse camera navigation still
works. Stop/F5 calls game shutdown, releases the runtime copy and restores the
editor scene/camera. Play does not change document history, saved state or files.

The sample rotates the first three visible root objects. WASD moves the first
one while playing. Right-mouse camera capture suppresses gameplay key input so
camera navigation does not also move it. Held keys are tracked independently of
text entry. macOS accent press-and-hold is disabled for Afterlight; text input is
active only while editing a field.

Edit the C game and rebuild **only its target**, keeping the editor open:

```sh
cmake --build --preset debug --target afterlight_game
```

With Auto reload enabled in Play, the editor detects the rebuilt library after
its size/modification time remain stable for 0.3 seconds. Reload game manually
retries a load, including a previously rejected build. The status bar reports
success or why a candidate was rejected. The active game keeps running through
missing/truncated builds, ABI mismatches and failed candidate initialization.
No editor restart is required.

The default module is the build configuration's `afterlight_game` target. Use
**Choose C game module** while stopped, or pass `--game LIBRARY`, to load another
one. Project files persist the startup scene and module together; the Project panel
opens/saves them. Scene files contain no executable module path. macOS bundles also contain a copy under Resources/game for inspection;
the development editor watches the original build output.

## Module contract

Include `afterlight/game_module.h` and export:

```c
AF_GAME_EXPORT const af_game_module *af_game_get_api(uint32_t abi_version);
```

Return NULL for an unsupported host ABI. The descriptor specifies ABI/struct
sizes, name, a bounded state allocation size, and init/update/shutdown callbacks.
The current ABI is **14** (0.10 adds animation components and playback/posed-world host functions; 0.9 added named HDR environment/skybox settings; 0.8 added HDR/environment/bloom settings, local shadow
fields, overlay image IDs and renderer diagnostics), including PBR/emission/normal material fields and
host functions for local lights, directional light, render settings, collision
queries, character movement, input edges, camera/capture control, world transforms, gameplay status and sound
playback. Scene objects also carry saved player/camera/role components. Rebuild
modules with the current headers; editors using earlier ABIs must be restarted
to use newly built ABI 14 modules.
Use the host's function table to inspect, create, delete, parent, transform and
assign materials to objects and query input. The game does not link its own copy
of the engine. `game/demo_game.c` is the complete working example and CMake target.

Module `update` runs at fixed 60 Hz. Optional `frame_update` handles per-render
mouse input and camera look, including zero-delta paused camera presentation.
The host flags distinguish permitted gameplay input from camera ownership so
editor navigation can temporarily take over. See [gameplay](gameplay.md) for
`game/playground_game.c`, the shared standalone/reloadable obstacle-course game.

The host owns a zero-initialized state allocation of up to 16 MiB. Shutdown must
release any extra allocations/resources the module owns, tolerate partial init,
and leave no threads, callbacks, scene views or function pointers alive after
unload. Host tables and object views are borrowed for the current callback only;
store object IDs and re-query instead of retaining object pointers. All callbacks
run on the engine's main thread. Do not retain the host/scene pointer globally.

A candidate loads from a unique copy in Afterlight's per-user application cache,
so rebuilding the watched file never overwrites the library currently executing.
The host validates ABI and structure sizes, then initializes the candidate with
a clone of the current runtime scene and fresh state. Failure destroys only the
candidate. Success shuts down/unloads the old module and adopts the candidate.
The old runtime scene is preserved in the clone; private C module state is
**reinitialized** unless the module supplies the versioned state callbacks added in 0.12. The sample takes current
transforms as its new animation base. Stop always restores the original edit
scene. Shadow copies are removed on normal unload.

This runs native C code in-process. An arbitrary crash in game code is not
isolated from the editor. The ABI is an early interface; rebuild modules with the
current headers/compiler/architecture when it changes. There is no automatic migration of arbitrary module layouts, multithreaded gameplay, debugger integration or automatic compiler
invocation yet. Build failures leave the previous module active because the
editor watches successful output changes, not source edits.

## Verification

`afterlight.runtime` checks clone isolation, pause/stop, ABI checks and failed-init
cleanup without a window. `afterlight.hot_reload` loads real shared libraries,
changes behavior while playing, rejects bad ABI/initialization/truncated outputs,
continues the previous code, recovers, and checks shadow-file cleanup. The editor
smoke test exercises Play/Stop and restoration with Vulkan validation enabled.
The obstacle-course editor test separately verifies immediate player capture,
Escape/recapture and temporary editor camera ownership.

macOS Clang module targets include a C destructor shim that correctly unregisters
sanitized globals after user destructors run. This fixes the previous unload
tracking abort while preserving ASan/UBSan checks. See [sanitizer verification
and module link-order requirements](testing.md).

Generated starter projects use `game/starter_game.c` and matching copied headers.
The generic player runs the same C module API without the editor; see
[creation/build/export](shipping.md).


## Animation control (0.10)

Use the host's `animation_root`, `animation_clip` and `animation_status` to find
an imported model and its clips. `animation_play(engine, root, clip, blend_seconds)`
starts or crossfades a clip; `animation_options`, `animation_pause`,
`animation_stop` and `animation_seek` control it. `animated_world_matrix` exposes
the last computed visual pose for attachments. These calls work in game `init`:
the runtime scene is attached before the callback. See
[playback, timing and collider limits](development-010.md).

## Reusable tools (0.12)

Game ABI 11 adds named actions, prefab instantiation/spawning, positioned audio,
optional versioned state save/load and deferred scene requests. The host owns
its runtime context and project directory; borrowed values must not be retained
across transitions/reloads. See [the complete contract and limits](development-012.md).

## Physics (0.13)

Game ABI 12 adds saved rigid bodies, force/point impulses, state queries,
force-limited character pushing and grab/move/release operations. Physics advances
after fixed callbacks and respects pause/single-step. Call `push_character`
after character movement with the requested world velocity; held objects use
`move_grab` targets each tick. The sample physics module implements these controls.
See [supported shapes, limits and examples](development-013.md).

## Meshes and constraints (0.14)

ABI 13 includes mesh world-collider views and joint host calls `add_joint`,
`set_joint`, and `remove_joint`. Include `afterlight/joint.h`; start from
`af_joint_default()` and assign endpoints, anchors and distance or hinge axes.
Use host calls from game DLLs. Engine-linked CPU tools can prepare imported
meshes with `af_scene_prepare_collision_meshes` and query scene joint frames.
Pointers to collision meshes are borrowed until cache replacement or scene
destruction. See [limits and anchor conventions](development-014.md).

## Powered mechanisms (0.15)

ABI 14 adds joint-state diagnostics and joint views/counts to the host table.
For a hinge, set `motor_enabled`, relative B-A `motor_speed` in rad/s and
`max_motor_torque` in Nm. Update a copied joint through `set_joint`; never mutate
borrowed views. Zero speed brakes, disabled motors leave free twist, and limits
still apply. Joint-state reactions are cached impulses/substep duration.

The machinery C module oscillates limited motors before their endpoints, and
exposes MotorToggle (Z), MotorReverse (X) and authored-state restart (R). Its
state callbacks preserve targets and pause state across reload.
