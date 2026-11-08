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
The current ABI is **28** (0.10 adds animation components and playback/posed-world host functions; 0.9 added named HDR environment/skybox settings; 0.8 added HDR/environment/bloom settings, local shadow
fields, overlay image IDs and renderer diagnostics), including PBR/emission/normal material fields and
host functions for local lights, directional light, render settings, collision
queries, character movement, input edges, camera/capture control, world transforms, gameplay status and sound
playback. Scene objects also carry saved player/camera/role components. Rebuild
modules with the current headers; editors using earlier ABIs must be restarted
to use newly built ABI 28 modules.
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

## Compound props (0.16)

ABI 15 adds collider-part IDs to query/contact results and C host functions
`add_part`, `set_part`, `remove_part`, `get_part`, `part_at` and `part_count`.
Parts use a local pose about their owning object, centered box/sphere dimensions
and material density. Enable a compound collider after adding its parts. Dynamic
owners require root placement and uniform positive scale. Physics state positions
are centers of mass; scene transforms retain authored visual pivots.

Use part IDs for part selection and owner IDs for forces, queries, grabs and
body filtering. Character push budgets apply once per owner.

## Water in C (0.17)

ABI 18 adds `set_water`, `remove_water`, `get_water`, `sample_water`,
`disturb_water`, `water_particles` and `water_stats` to the host table. Water
owners are root `__node` objects with unit scale and yaw rotation. Add/edit an
`af_water_default()` definition and select `AF_WATER_POOL`, `AF_WATER_RIVER` or
`AF_WATER_PARTICLES`. The sample returns height/bottom/current by XZ for pools
and rivers; it excludes particles. Particle queries copy positions, velocities
and per-particle volume into caller storage and return the total available count.
The runtime fluid state is isolated to Play; definitions belong to the scene.
`game/water_game.c` demonstrates controls and gradual kinematic cup tipping.

## Swimming and course feedback (0.18)

ABI 20 includes `swim_config_default` and `swim_character` in the C host, plus a
bounded `hint` string in `af_gameplay_status` for the Play HUD. Keep an
`af_swim_state` per character and call the swimming controller with world-space
XZ movement, vertical input in −1…1 and a jump edge. It uses ordinary walking on
land, so existing game modules keep their chosen movement behavior. Scene water
queries provide the current; particle fluids are excluded from swimming.
`game/water_course_game.c` demonstrates input, first-person camera, ordered
checkpoints, restart/scene changes, saved C state and an actual pouring objective.
Runtime transform edits to its player are treated as teleports. Code reload
preserves character/progress state, while fluid state follows the existing scene
reset/reinitialization rules. Checkpoint/death respawn preserves the active world;
R reloads the authored scene and resets all fluids.

## Wind in C (0.19)

ABI 21 adds `wind_default`, `get_wind`, `set_wind` and `wind_velocity` to the host.
Settings belong to the scene. Sample velocity at a simulation time in seconds;
the engine uses the same deterministic gust function for physics and water detail.
Direction points downwind in world XZ. See the [wind guide](development-019.md).

ABI 22 extends `af_render_settings` with `anti_aliasing`, `aa_sharpness` and
`taa_history_weight`. Use `AF_AA_OFF`, `AF_AA_FXAA`, `AF_AA_SMAA` or `AF_AA_TAA`
and update through the existing render-settings host functions. Rebuild modules
and restart older editor/player processes after upgrading.

ABI 23 extends `af_engine_stats` with visibility counts, retained GPU allocation
bytes, render-budget diagnostics and effective AA/debug modes. Rebuild game
modules against the matching public headers. Scene format remains 23.

ABI 24 adds Boolean `af_render_settings.ray_traced_shadows`, ray capability/AS
diagnostics in `af_engine_stats`, and host `get_render_settings` / `get_stats`
callbacks. The requested scene preference and actual active path are separate;
check `ray_traced_shadows_active` and `ray_query_supported`. The
[ray lab](../game/ray_shadow_game.c) demonstrates toggling and reporting fallback.
Scene format is 26; rebuild modules and restart old processes.

ABI 25 adds `af_render_settings.ray_traced_water_reflections` and the active
reflection flag in `af_engine_stats`. The C game ABI is independent of the
0.22 release version.

ABI 26 adds `af_render_settings.ray_traced_gi`, `rtgi_intensity`,
`rtgi_ray_distance` and `rtgi_samples`, plus `ray_traced_gi_active` in
`af_engine_stats`. The new render fields sit before `anti_aliasing`, so rebuild
game modules and restart editor/player processes built against earlier ABIs;
the struct layout is not backward compatible. `rtgi_samples` is clamped to
1..4. Scene format is 25 and stores the GI fields only when non-default.


ABI 27 adds `af_material.ior` (1.0–3.0) and
`af_render_settings.ray_traced_material_effects`, plus the corresponding active
renderer statistic. The IOR is serialized in scene format 26; older scenes load
with a default IOR of 1.45 and material ray effects disabled. Rebuild game modules
and restart editor/player processes because the public material and render-settings
struct layouts changed.

ABI 28 adds `af_directional_light.angular_radius` in radians (0–0.25). It controls
area-sun sampling for ray-traced shadows; the default 0.00465 radians approximates
the physical sun. Scene format 27 persists this value. Rebuild native game modules
and restart editor/player processes because the public directional-light layout changed.
