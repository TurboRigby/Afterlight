# Afterlight architecture

## Current boundary

Afterlight is a C17 static library. A separate executable provides game callbacks
through `af_game`; the engine owns the main loop. The public header exposes no
SDL or Vulkan types. Renderer details live behind a private opaque pointer.

```text
Sandbox (C game callbacks) ──> Afterlight public C API
                                  │
                            Engine lifetime
                             /           \
                    SDL3 platform     Vulkan renderer
                                           │
                                 Native Vulkan driver
                                 or MoltenVK on macOS
```

There is one engine/window at a time. Create, run, resize, and destroy it on the
main thread. The caller owns the `af_engine` pointer and destroys it after
`af_engine_run` returns. Do not destroy the engine inside a game callback.
An engine can run once; create a fresh engine for another session. Public symbols
use the `af_` prefix. The API is an early foundation and is not ABI-stable yet.

## Ownership and failure

The engine owns SDL initialization, its window, and its renderer. The renderer
owns the Vulkan instance, optional validation messenger, surface, device,
swapchain, and frame resources. Destruction reverses those dependencies and
handles partial initialization. The caller owns `af_game.user_data` throughout
the run. The base window title is copied during creation; the config is not
retained. The window
stays hidden during renderer and game initialization and is shown before the
first image acquisition. This avoids exposing a black window while pipelines
compile without trying to present to a hidden surface.

Game callbacks are optional. If `init` is invoked and returns false, `shutdown`
still runs once so the game can release partially initialized resources.
If no `init` is supplied, `shutdown` still runs once on exit. Returning false
from `init`, a rendering failure, or an observed validation error produces a
nonzero run result. Destruction also reports Vulkan errors to stderr.

GPU work is drained before `shutdown` and before renderer destruction. Renderer
failures stop the session rather than attempting device-loss recovery. Logs go
to stderr; the sandbox's status and controls go to stdout.

## Frames and resize

The renderer targets Vulkan 1.1. Each frame clears an RGBA16F offscreen color attachment and draws
the attached scene with a graphics pipeline and depth testing. FIFO
presentation is used for vsync. On macOS, driver properties identify MoltenVK
and prefer it over other ICDs exposing the same GPU. Selection then favors
hardware GPUs over software devices; other platforms prefer discrete GPUs.
The chosen driver name/version is logged instead of only the GPU name. Graphics and
presentation may use separate queue families. Portability enumeration and the
portability-subset device extension are enabled when available for MoltenVK.

Two frames can be in flight. Each frame owns a command buffer, acquisition
semaphore, and completion fence. Each swapchain image owns its presentation
semaphore, so that semaphore is reused only after that image is reacquired.
Each swapchain image also owns a depth image, allocation, and view, rebuilt
with the framebuffer on resize. The acquisition wait and external subpass
dependency cover color output and early/late fragment tests. This orders both
color and depth use before reusing an acquired image. Depth clears to 1 and
uses LESS comparison with depth writes enabled. Supported depth formats are
queried, preferring D32 float with depth/stencil fallbacks.

A frame fence is reset only after acquiring an image successfully. An
out-of-date acquisition therefore leaves the frame fence signaled and cannot
hang a later frame. Swapchain recreation handles resized and suboptimal
surfaces, waits for the device, and rebuilds extent/format-dependent resources.
Duplicate window size events retain the existing swapchain when extent and
surface format are unchanged. Vulkan out-of-date/suboptimal results explicitly
invalidate it and force recreation even at the same size. Stats count actual
swapchain creations so redundant rebuilds can be caught in regression tests.
This deliberately simple recreation path can stall during resize; a future
renderer can retire old swapchains asynchronously with presentation fences.

Logical window dimensions and framebuffer pixel dimensions are separate. The
swapchain uses framebuffer dimensions for high-density displays. Zero-sized
surfaces defer rendering until there is a drawable extent. While minimized, the
engine polls events and sleeps between polls. Game updates, simulation and
rendering resume on restore with a fresh frame clock and interpolation history.
On Wayland, losing input focus after the window has been activated also
suspends updates because some compositors only signal focus loss on minimize.
The per-render update delta is capped at 100 ms for UI/presentation. Optional
fixed updates use a separate 60 Hz accumulator with an eight-tick catch-up limit.
Excess whole ticks are discarded and reported; paused single steps execute one
tick. Presentation interpolates CPU scene poses and simulation-owned camera
positions. See `gameplay.md` for the scheduling/input/interpolation contract. `rendered_frames` counts successful/suboptimal
presentation submissions, not proof that a pixel has reached the display.

FPS stats count successful presentation submissions over actual elapsed time,
separately from the capped game delta. Samples update each second and include
pacing/vsync and skipped-frame time. Minimized frames add no presentations. An
optional title suffix shows FPS and average milliseconds per submitted frame;
this is not a GPU timestamp measurement. The sandbox enables it by default.

## Shaders and geometry

The game supplies `af_mesh_data` through `af_engine_upload_mesh` with a mesh
asset name. The renderer validates vertices/indices and copies the data into
owned vertex/index buffers. A registry holds multiple named assets. Re-uploading
a name replaces its buffers only after a successful upload and GPU idle wait;
failure preserves the existing asset. Removal also waits before releasing buffers.
The engine owns this registry and frees every asset during teardown.

Scenes are CPU-only and contain named objects referencing those mesh names.
The renderer iterates visible objects, resolves the referenced asset, and sends
each object's model matrix to the shader before its indexed draw. A shared asset
is uploaded once, regardless of how many objects use it. This is not hardware
instancing: there is one draw per visible, resolved object. Missing assets are
skipped, counted in stats, and logged when the missing-object count changes.

The sandbox animates two cubes and an imported OBJ pyramid over a ground
platform. Cube objects share an asset with 24 vertices and 36 indices; the
pyramid uses imported geometry and the demo also loads a checker texture. Scene changes,
mesh lookup, and drawing currently use simple linear arrays/searches, suitable
for the current small scenes. No lookup pointers are retained across mutations.

Mesh memory is host-visible, preferring coherent memory. Noncoherent allocations
are flushed before unmapping. This is a setup-time upload path; staging buffers,
device-local mesh allocations, and streaming can follow when needed. Object
transform and camera changes do not re-upload geometry or wait for the GPU.

The math module is independent of SDL and Vulkan. Matrices use column-major
storage and column vectors, so the shader receives `projection * view * model`
in a 3216-byte per-frame camera/light/shadow/water uniform plus a 128-byte per-draw push constant
(model matrix, material/selection tint, flags, surface values and emission). World space is right-handed with Y up,
angles use radians, and the camera looks along -Z by default. Perspective maps
near/far planes to Vulkan's [0,1] depth and flips clip Y for the positive-height
viewport. Aspect ratio follows the logical scene viewport, which is scaled to framebuffer pixels.

Binding stride and attribute offsets come from the C vertex struct layout.
The pipeline uses opaque triangle lists, no culling, and no alpha blending.
Vertex normals and UVs feed textured ambient/Lambert directional lighting. A
material can select unlit output. Directional diffuse is multiplied by shadow
visibility; ambient remains unshadowed. Normal transforms use the inverse transpose
of the world model matrix, with a fragment geometric fallback for zero normals.
Depth rejects hidden surfaces regardless of index submission order. Viewport
and scissor are dynamic. The pipeline/layout are owned by the swapchain and
rebuilt with the render pass. Mesh buffers survive resize. Shader modules are
released immediately after pipeline creation.

CMake runs `glslangValidator` for Vulkan 1.1 with SPIR-V validation and embeds the
result as generated C headers. No shader file lookup or compilation occurs at
runtime. This is the built-in color pipeline, not a custom material/shader API.

## Directional shadows

`shadow_math.c` fits one camera-frustum slice or four mixed uniform/logarithmic
splits. Rotation-stable enclosing spheres and texel-snapped light-space centers
reduce camera-motion shimmer. Overlapping slices permit fragment-side blending.
World-space bounds from all visible, cast-enabled objects extend the depth range
when they overlap a slice in light-space XY, including offscreen casters. The
renderer culls each caster against each resulting light frustum.

`shadow_vulkan.c` owns a sampled D32/D16 depth array for each frame in flight,
with one or four layers, per-layer framebuffer/view, and a nearest border-white
sampler. A vertex-only depth pipeline draws opaque casters before the main pass.
Each frame fence protects its own array and uniform updates. External dependencies
order fragment reads against early/late depth writes; each layer ends in
DEPTH_STENCIL_READ_ONLY_OPTIMAL. Disabled shadows clear without drawing so
bound descriptors still have valid layouts. Dynamic raster depth bias and a
world-normal receiver offset reduce self-shadow artifacts.

The fragment shader selects a cascade from camera-forward depth, performs
manual hard/3x3/5x5 PCF with explicit texture LOD, blends adjacent ranges and fades
the last range to lit. Debug tint exposes cascade selection. Cast/receive flags
are material data, inherited visibility suppresses casters, and unlit materials
bypass shadow reception. Shadow arrays survive swapchain resize. Changing map
resolution or layer count waits for the device and constructs a replacement
before retiring the previous resources. See `shadows.md` for costs and limits.

`render_test.h` provides a private opt-in single-pixel readback for graphics
regressions. Supported swapchains permit TRANSFER_SRC; a requested pixel is
copied after the main pass with explicit image/host barriers. Tests wait before
reading mapped memory and handle RGBA/BGRA order and noncoherent invalidation.
Normal rendering allocates no readback buffer and performs no copy/readback wait.

## Scene data and ownership

`af_scene` owns a growable dense array of `af_scene_object` values and no GPU
resources. It can be created, inspected, and edited without an engine, window,
or game callbacks. Each object has a scene-local 64-bit ID, copied display name,
copied mesh asset name, position/Euler rotation/scale, and visibility. Transform
composition is `T * Rz * Ry * Rx * S`; angles are radians. Nonfinite transforms
and empty/overlong names are rejected without changing existing state.

IDs increase monotonically, with zero reserved for failure/none. Deleting an
object compacts the array but never changes other IDs or reuses the removed ID.
Object display names may repeat. Read-only object pointers are temporary views;
structural changes invalidate them, so callers should retain IDs instead.

The engine borrows the attached scene and never destroys it. Mutate an attached
scene on the main thread. Detach it before destruction, or keep it alive until
the engine is destroyed. The sandbox detaches and frees its scene during game
shutdown; the renderer then frees the shared GPU assets independently. The
scene now supports parent/child relationships. Mesh references are
asset names, not GPU handles, and are preserved in the scene file format.

## Scene persistence

`af_scene_load` and `af_scene_save` work without SDL, Vulkan, or game callbacks.
Version 5 uses UTF-8 JSON with a format tag and explicit version. Object IDs and
the next-ID counter use canonical decimal strings to retain all 64 bits; the
counter preserves deleted/exhausted ID history. Objects retain order, names,
mesh/texture references, materials, parent IDs, visibility, and local TRS. It also
records the asset manifest, directional light, shadow settings and per-material
cast/receive flags; versions 1–4 load with backward-compatible defaults.
Version 4 adds surface/normal/emission fields, exposure and bounded local lights. The loader validates
schema, uniqueness, counter consistency, encoding, finite float ranges, and
file/object/depth limits before returning a new scene. Failed loads leave the
caller's existing scene untouched.

Saving creates the complete JSON before opening an exclusive sibling temporary
file. It replaces the destination after a successful write/close and cleans up
on failure. This prevents partial destination files, without promising crash or
power-loss durability. A vendored MIT-licensed cJSON parser handles the JSON tree;
scene schema and file IO stay private. See `scene-format.md` for the specification.

CMake packages the sandbox's demo scene with the executable (bundle Resources
on macOS). A platform path accessor lets the game find it without an SDL include
or dependence on its working directory. The sandbox can load a custom scene and
save an explicit snapshot on exit. Default demo animation offsets the loaded base
transforms; authored transforms stay separate from imported animation poses.

## Camera and input

`af_camera` is plain data with position, yaw, pitch, vertical FOV, and clip
planes. Helpers provide a view matrix, flying movement, and rotation with pitch
limited to 89 degrees. The engine copies valid camera state. The sandbox owns
the camera, movement speed, sensitivity, and reset/pause policy.

Input is tracked from events. Held keys persist until release; pressed keys and
mouse button presses last one update. A down/up pair in the same event batch
still produces a press, so short Escape/Space/R taps are not lost. Mouse delta
accumulates only while capture is requested and resets each update. Relative
mouse mode is enabled only after clicking the viewport. Focus loss clears input
and disables capture; engine shutdown also releases the mouse.

## Editor and overlay rendering

`afterlight_editor` is a separate C executable linked to the same engine. It owns
a scene document, selected object ID, camera, UI context, and undo/redo history.
It edits scene data while stopped, and runs C game code on a cloned scene in Play. The initial
demo opens as an untitled document; saving requires a destination instead of
silently modifying the bundled resource.

The document controller is independent of SDL and Vulkan. Each accepted edit
clones the scene and swaps it into the document. This preserves the previous
scene for undo and reattaches the replacement before rendering. Consecutive
changes to the same field kind/object form one history entry until the edit
ends. New edits discard redo history. Each stack is limited to 32 entries and
32 MiB, except a single snapshot that itself exceeds that size. Snapshots retain
selection, object IDs, allocation history and revision; saved revisions drive
the dirty marker. Opening a document clears history. Failed IO keeps the current
document. Scene cloning has no GPU allocations.

The editor uses vendored Nuklear in C for immediate-mode widgets. Nuklear stays
outside the engine. Each update converts its output into an `af_overlay_frame`:
vertices, 32-bit indices and clipped draw commands. Arrays are borrowed until
that update's rendering finishes, and the engine clears the packet before the
next update. One RGBA font atlas is uploaded into engine-owned GPU memory. The
Vulkan overlay pipeline uses alpha blending and no depth tests after scene draws
in the same render pass. Per-frame mapped buffers grow only after their frame
fence completes; noncoherent allocations are flushed. The atlas and buffers
survive resize while the pipeline follows the swapchain render pass.

Scene viewport rectangles, UI coordinates, cursor positions and clip rectangles
use logical window units. Rendering scales them to framebuffer pixels. Picking
casts a camera ray through that viewport and tests inverse-transformed mesh
bounding boxes; it ignores hidden, unresolved and singularly transformed objects.
Selection applies a warm vertex-color tint. It does not render an outline or
perform triangle-level picking.

The engine collects UTF-8 text, wheel deltas, absolute cursor positions, mouse
button state and clipboard data. The editor forwards these to Nuklear, retaining
short presses even when release arrives in the same event batch. Right-button
capture is editor camera navigation. A close-veto callback lets the editor prompt
for unsaved changes; explicit application quit bypasses it.

Native SDL3 file dialogs may complete asynchronously. Their callback writes a
refcounted result with release/acquire synchronization; it never touches editor
or scene state. The main update applies completed results. Shutdown can drop its
reference while a callback is pending. A private SDL bridge supplies the parent
window to the editor's dialog module; public engine headers expose no SDL types.
New/Open/Close on a dirty document offer Save, Discard or Cancel. Canceled or
failed saves do not perform the pending document action.

## Gizmos, hierarchy and appearance

The gizmo module is CPU math separate from Nuklear. It projects fixed-size axis
handles and Euler rotation planes into the viewport. Picking uses line-segment
screen distances. Translation uses projected parent axes and accounts for parent
scale. Rotation intersects camera rays with the selected ring plane, transforms
back into parent coordinates and accumulates signed angles. Scale changes one
local component. Snapping quantizes displacement; a drag groups document edits,
and Escape restores the pre-drag history state.

Parent IDs are scene-local, zero for roots. Setters reject missing/self/cyclic
parents and chains exceeding the depth limit. World matrices multiply local TRS
through ancestors; visibility also inherits. Rendering and bounding-box picking
use those world matrices. Reparenting preserves local TRS. Subtree deletion and
editor duplication retain predictable undo through scene snapshots. The hierarchy
UI groups children beneath their parent and filters invalid parent choices.
Lookups still use simple arrays appropriate to the current small scenes.

OBJ/image loaders are CPU-only. OBJ positions/UVs/normals and convex face fans
become owned vertex/index arrays, generating missing normals. Image decoding
uses vendored C stb_image with format/dimension limits. Loader outputs are freed
after GPU upload. Manifest paths resolve relative to the loaded scene. Version 5
save/load retains hierarchy, materials, lighting, shadows and asset references; saving
converts references under the destination directory back to relative paths.

The texture backend is shared by scene textures and the UI atlas. It owns image,
allocation, sampler, view and a descriptor layout/pool/set. Upload uses a staging
buffer and explicit transfer/shader-read barriers, then waits before releasing
staging memory. Scene color textures are sRGB, normal maps are UNORM, and both get CPU-generated
area-filtered mip chains with all levels uploaded by a single staging submission.
sRGB channels are averaged in linear space; NPOT edge texels contribute. Samplers
use trilinear repeat sampling and up to 8x supported anisotropy. The device enables
the samplerAnisotropy feature only when supported. The UI atlas remains single-level
UNORM with clamped sampling. Replacement/removal waits for GPU
work. Each frame has an independent uniform buffer/descriptor updated only after
its fence. Mesh pipelines bind frame data at set 0, texture at set 1, and the shadow array
at set 2 and the linear normal map at set 3. Texture and uniform resources survive swapchain recreation; the main
pipeline follows its pass.

## Play and C module lifecycle

`ed_runtime` owns a clone of the edit scene, callback state, host services, loaded
module and its shadow path. Play initializes the C module on that clone. Pause
skips updates. Stop calls shutdown, detaches/releases runtime state, and restores
the edit scene and camera; document history/revision are unaffected. Editing
controls are disabled in Play. Input goes to gameplay unless editor camera
capture owns it. SDL text input is active only for fields; the macOS press-and-hold
accent hint is disabled before SDL initialization. OS key repeats are tracked
separately from initial key presses for text-edit navigation/deletion.

The versioned `af_game_module` export includes ABI and structure sizes, a name,
state size and init/update/shutdown callbacks. The host table exposes scene/input
operations without linking a second engine instance into a module. Callback views
are borrowed. State is allocated/zeroed by the host and freed after shutdown.
Modules must release their own resources and leave no threads/callbacks alive
before unloading. Game code executes in-process with no crash isolation.

The watcher checks library size/mtime and waits for 0.3 seconds of stability.
It loads a unique copy in the application's per-user cache, validates the export
and ABI, and initializes a candidate with a clone of the current runtime scene.
Failure frees only candidate state and keeps the previous loaded functions and
runtime scene. Success shuts down/unloads the old module and adopts the candidate.
Private module state is reset; runtime scene data survives. No raw state migration
is attempted. Unique copies prevent loader caching and Windows file-lock issues
when rebuilding the source library. Normal unload removes the owned copy. The
module path is project configuration (or a session override), never auto-executed
scene data. Project loading selects the path; only Play executes the C module.
See `game-code.md` for the build/reload workflow and module contract.

## Material, light and profiling extensions

The optional metallic/roughness path evaluates a GGX distribution, Schlick
Fresnel and Smith visibility approximation with energy-split Lambert diffuse.
Legacy Lambert and unlit paths remain available. Emission is independent of
lighting. Normal mapping reconstructs a tangent frame from world-position/UV
derivatives, preserves UV handedness and falls back for degenerate UVs. The
minimum Vulkan push-constant budget remains sufficient at 128 bytes per draw.

Scene-owned point/spot lights are bounded at 16, copied into the per-frame UBO,
and iterated in the fragment shader. Range falloff smoothly windows inverse-square
attenuation; spot half-cones apply a smooth inner/outer angular ramp. They do not
use shadow maps. Scene exposure and optional Reinhard mapping apply to scene
fragment output; there is no floating-point intermediate color buffer or IBL.

CPU counters measure event processing, game/editor update and render submission
including waits. Timestamp query pools belong to frame slots. After the slot's
fence completes, query results are read without an extra wait. Timestamp valid-bit
masks handle wrapping and timestampPeriod converts to milliseconds. Four stamps
bound shadow, scene and overlay work; supported queues are detected at device
selection. Queries reset only when recording a new submission. Stats expose a
delayed completed-frame sample, separate from paced FPS and CPU submit time.

The editor's Assets window browses the current manifest, includes the built-in
cube, filters by kind/name/path and delegates assignments/creation to document
commands. Imports and assignments keep undo/revision semantics. Performance and
Assets floating windows consume pointer actions before viewport picking/camera
capture. Browser filters and panel visibility are not scene data.

## Simulation and collision boundary

The fixed clock is independent of SDL/Vulkan. Engine frame input latches quick
key edges until a fixed tick consumes them. Before each tick, the engine owns a
CPU scene snapshot; renderer model/shadow transforms interpolate parent-local
TRS from that snapshot. Scene replacement, pause and teleport reset it. Queries
and editor picking inspect current CPU geometry. Frame-slot Vulkan resource
ownership and synchronization are unchanged by simulation snapshots.

Collider components are CPU values retained by object/scene history. World
collider resolution supports OBBs, spheres and oriented capsules, excluding
singular/sheared transforms. Segment-to-box piecewise distance and segment-pair
distance feed overlaps and conservative capsule sweeps. Ray queries use primitive
intersections. Character movement depenetrates, sweeps/slides and tracks a local
support anchor for moving platforms. All scans are linear; this is kinematic
movement without a general rigid-body solver. The editor builds primitive 3D
wire segments and projects them into the existing overlay packet.

`game_host.c` constructs the versioned host function table shared by editor
runtime and the standalone playground. C modules call that table for scene,
collision, character and input/camera services. The obstacle course uses the same
C source linked statically into its standalone app and as a reloadable module.


## HDR and editor preview resources (0.8)

`post_vulkan.c` owns HDR/half-resolution bloom attachments and presentation
pipelines per swapchain image. Scene geometry ends in a sampled layout; bloom
and final tone mapping precede the separate display-space UI. `ibl.c` builds
original HDR environment images and GGX/diffuse/BRDF integration inputs.
`local_shadow.c` builds point/spot projections; local depth arrays are separate
from cascaded directional shadows and protected per frame in flight.
`editor/content/thumbnail.c` produces bounded CPU previews and maintains a 64-entry
visible-widget cache. Overlay commands select independent UI images without
exposing Vulkan descriptors in the public API. See [0.8 details](lighting-08.md).


## Imported environment and thumbnail jobs (0.9)

The frame uniform includes inverse view-projection for the skybox, drawn before
opaque geometry without depth writes. Imported RGBE sources supply full-size
backgrounds and convolved IBL maps; per-frame descriptors select procedural or
named resources. Replacements drain GPU work transactionally.

Thumbnail jobs own small immutable scene/asset/material snapshots. A bounded SDL
worker queue returns CPU pixels tagged by generation. Main-thread publication
queues one image transfer per frame, and previous GPU versions retire behind
frame fences. UI decoding and ordinary frame upload avoid device/queue-idle
waits; worker shutdown joins before document teardown. Light tools reuse the
object gizmo math with separate light-array selection and grouped history.
See [0.9 limits and checks](assets-09.md).


## Animation runtime (0.10)

A bounded immutable clip library, per-root players and sampled pose buffers live
in `animation_runtime.c`. Scene objects only store startup configuration and
source-node bindings. After game callbacks, the runtime interpolates authored
transforms, applies sampled bind-relative local animation, computes hierarchy
worlds and publishes borrowed matrices to the renderer. Mesh-local bone palettes
are copied into per-frame storage buffers after owner fences. Scene and shadow
shaders share four-influence deformation; palette revisions invalidate cached
maps. Physics still uses authored colliders. See
[limits, timing, ownership and source references](development-010.md).

## Playable character runtime (0.11)

[The 0.11 notes](development-011.md) describe the C third-person controller,
fixed-tick locomotion driver, synchronized adjacent clip blending, validated
animation markers, bounded event queues, horizontal root-motion extraction and
posed skeleton queries. Game ABI is 10; scene format remains 10. The pose runtime
builds an object-ID index once per update and reuses per-instance transition
buffers. `cpu_animation_ms` measures pose/world/palette work inside render time.

## Primitive rigid-body world (0.13)

The engine owns one bounded main-thread physics world for its attached scene.
Fixed game callbacks run first, then physics steps, then interpolation/animation
and rendering consume the updated scene. Dynamic roots publish their quaternion
orientation as Euler TRS and their runtime velocities into the Play clone;
render interpolation uses quaternion SLERP for dynamic roots. Scene identity
changes reset contact caches and grabs. CPU worlds can be used independently.
Broadphase, narrowphase, solver, sleep/CCD and interaction live in separate C files.
Scene version is 13 and game ABI is 12; see [the physics notes](development-013.md).

## Static mesh collision and constraints (0.14)

Immutable triangle arrays and median-split BVHs are shared by scene clones with
main-thread reference counts. CPU ray/capsule queries and rigid-body SAT use the
same geometry. Asset reload stages collision cooking with GPU uploads and keeps
the prior cache on malformed input. Contact warm starts clear when a mesh is
replaced. Conservative CCD considers relative linear and angular motion.

Scene-owned distance/hinge joints bind stable object IDs into the physics solver.
Anchor rows include angular inertia; hinges add two alignment rows and optional
twist-limit rows. Connected collision filtering is shared by discrete and CCD
paths. Joint bodies remain awake pending constraint-island sleeping.

## Constraint islands and powered rows (0.15)

Discrete and continuous collision share an adaptive-axis bounds sweep. CCD uses
rotation-conservative swept bounds, refreshed after each impact. Stable-ID pair
hash buckets replace global contact-cache scans.

Dynamic contact/constraint graphs form islands; static ground does not join
independent groups. Quiet supported islands sleep together, and impulses, moved
supports and removed anchors propagate waking. Joint rows retain scaled impulses
between substeps. Hinge alignment, unilateral stops and torque-budgeted velocity
motors share the iterative solver. Diagnostics estimate reaction from cached
impulses. Running motors remain awake; zero-speed brakes may sleep when quiet.

The character capsule is a passive, accurately rounded obstacle to rigid bodies.
Pose deltas do not inject kinematic landing energy. Character pushing remains
explicitly force limited. Center/edge/corner landing regressions guard against
self-propelled crates.

## Compound rigid bodies (0.16)

Scene-owned collider-part tables have stable owner/part IDs. Queries flatten
parts into actual box/sphere shapes. Physics bakes local part frames and keeps
body COM axes separate from shape axes. A full inverse inertia tensor handles
rotated/offset masses. The authored pivot is recovered when publishing poses.

Broadphase bounds encompass all parts. Narrowphase contacts and warm starts
retain part-pair identity, and CCD predicts each part from one shared body motion.
Editor transforms, prefab cloning and host APIs operate on the same definitions.

## Water runtime (0.17)

Scenes own at most eight `af_water` definitions. Physics owns independent wave
slots and optional particle allocations, synchronized by owner and structural
settings. Fixed ticks update waves, volume-sampled rigid-body buoyancy/drag and
bounded position-based particles. Queries distinguish height-field surfaces from
particle fluids. Source definitions clone/save; transient state frees on reset,
owner removal and destruction. Quality changes numerical work and particle budgets.

The renderer builds reserved `__water_ID` meshes, reuses unchanged previews and
removes stale slots. Water first attenuates existing HDR color per channel, then
adds shadowed PBR reflection/scattering through a compatible additive pipeline.
Both use the existing 128-byte push constant layout and portable Vulkan 1.1.
Particle rendering reconstructs bounded implicit surfaces with a drop fallback.
The 0.17 mesh upload waited for GPU safety; 0.18 replaces water uploads with the
streaming buffers described below.

## Water integration (0.18)

Dynamic water meshes keep CPU snapshots and per-frame mapped GPU allocations.
Only the completed frame's buffers are resized/written; retirement keeps asset
indices stable until every in-flight frame releases its allocation. Diagnostics
separate upload cost from fence/acquire/presentation waits. Calm fields and
balanced buoyant islands sleep; particle sets wake conservatively when solid
poses/geometry or emission changes.

Particle sweeps preserve tangential displacement and use bounded boundary
friction, multiple contact normals and solid effective mass. Kinematic boundaries
are interpolated across substeps. Swimming is an optional C controller built on
capsule queries/sliding, with surface hysteresis and current-relative motion.
Underwater shading uses a bounded view-path medium and smooth surface band.
Refraction snapshots opaque HDR/depth, then resumes a compatible transparent
load pass. It uses separate sampled images, explicit barriers and per-frame
descriptor updates; it does not sample its active attachment. Opaque depth
rejects foreground lookups. Render settings expose effect toggles/strength.

## Wind and water boundaries (0.19)

Water meshes include outward side/bottom faces, with inside/outside boundary selection.
The fragment shader layers a filtered procedural ripple normal texture over surface
normals. Scene wind is sampled at fixed simulation time for wave forcing, normal
advection and projected-area rigid-body drag. Drag accounts for mass, exposed area,
angular response and a bounded upstream shelter ray. See [limits](development-019.md).

## 0.21 foundations

Scenes retain authored object order and maintain an eager ID-to-index hash table.
Insertion grows amortized; compaction, loading and prefab structural edits rebuild
the index. Clones own independent tables, and lookups remain read-only.

Swapchain-dependent post resources are configured from effective AA, bloom,
motion and water-refraction needs. Inactive effect images are released; valid
placeholder descriptors cover unused bindings. Allocation-failure tests destroy
partially initialized resources and create a fresh engine in the same process.
The [rendering guide](rendering.md#resource-and-visibility-diagnostics-021) describes
transient debug modes, culling and allocation accounting.

Progress loading constructs and initializes a candidate runtime before replacing
the active game. Checksums, module ABI/state versions, local scene keys and the
authored scene's byte fingerprint are checked first. Failure retains the active
scene/camera/input state. Hot reload preserves the live module state and original
authored revision; it does not compare the moved player against its spawn.

## 0.22 ray queries

`ray_query_vulkan.c` owns cached BLAS objects and per-frame TLAS/instance/scratch
resources. The renderer checks extension/feature support before device creation,
selects a separate optional SPIR-V fragment shader, and prepares resources after
the current frame fence and before image acquisition. Recording orders host
uploads, BLAS builds, TLAS builds and fragment traversal. Failed optional
preparation can therefore retry raster without consuming an acquired image.
Shared geometry changes retire GPU use before invalidation; transforms rebuild
the affected frame's TLAS without rebuilding BLAS. See the [0.22 lifecycle notes](development-022.md#resources-and-synchronization).
