# Afterlight

A game engine written in C17, with Vulkan rendering and game code in C.
Built for the fun of building an engine.

Licensed under the [Mozilla Public License 2.0](LICENSE). Original Afterlight
files are MPL-2.0; bundled dependencies retain their [own licenses](THIRD_PARTY_NOTICES.md).
Builds and exports include covered source and notices. See [licensing and
distribution](docs/licensing.md), including how MPL applies to C game templates.

Afterlight **0.9** adds HDR panorama import and skyboxes, asynchronous previews,
viewport light tools, and Linux build/test CI. It builds on 0.8's RGBA16F rendering,
environment IBL, local shadows and bloom. Try the updated
[Lighting Lab and asset workflow](docs/assets-09.md):

```sh
cmake --build --preset debug --target run_lighting_editor
```

The sandbox renders two independently animated cubes, an imported textured pyramid, and a ground platform
over a dusk background. Four named scene objects use shared meshes and image textures.
Their definitions load from `sandbox/scenes/demo.afterlight.json`, copied into
the application at build time.
It uses indexed meshes, model/view/projection matrices, depth testing, materials,
ambient/directional lighting, and directional shadows. The default uses four
stabilized cascades with filtered edges; a simpler single-map mode is available. All engine
and game code is C; shaders are GLSL. The window title shows FPS and average
frame time, updated once a second. These count successful presentation submissions
using real elapsed time, including vsync/pacing; they are not GPU timings.
Games can read the same values through `af_engine_get_stats` and opt into the
title display with `af_engine_config.show_fps`.

- Click the viewport to capture the mouse and look around.
- WASD flies the camera; Q/E moves down/up; left Shift moves faster.
- Space pauses the cube and background cycle; R resets the camera and rotation.
- Escape releases the mouse; press it again to quit. Changing focus also releases
  the mouse and clears held keys.

The separate **Afterlight Editor** uses the same C engine and Vulkan renderer.
It has a scene hierarchy, viewport transform gizmos, snapping, an inspector,
OBJ/glTF/image/audio import, materials, lighting and shadow controls, undo/redo, native file dialogs, and
Play/Stop with C game-code hot reload.
The editor also has a searchable asset browser and a CPU/GPU performance panel.
Materials support opt-in metallic/roughness PBR, emission and linear normal maps;
scene textures use sRGB-aware mipmaps, trilinear sampling and supported anisotropy.
Point/spot lights and exposure/tone mapping are editable and persist with scenes.
See [editor usage and controls](docs/editor.md), [rendering](docs/rendering.md) and
the [completed fifteen-step checklist](docs/development-15.md).

The **Playground** adds fixed 60 Hz gameplay, interpolated motion and a capsule
character controller with gravity, jumping, slopes, sliding and moving platforms.
Objects can have box/sphere/capsule colliders, raycasts, overlaps and triggers.
The editor supports collider editing/wireframes and paused single-tick stepping.
The 0.6 milestone adds project files, saved player/camera components, controller
settings, F6 maximized Play, crosshair, checkpoints/timing, WAV cues and static
glTF/GLB import. The course includes an original colored GLB beacon. See
[the completed seven-step milestone and projects](docs/projects.md) and
[gameplay and obstacle-course controls](docs/gameplay.md).

Afterlight 0.7 adds a generic project player, a New Project wizard, glTF
hierarchy/per-primitive PBR textures, automatic asset reloading with last-good
recovery, and an export command that packages a runnable game. The finished-game
milestone is deferred. See [player, creation, models, reload and export](docs/shipping.md).

```sh
cmake --build --preset debug --target run_playground
cmake --build --preset debug --target run_playground_editor
```

## Build and run

Requires a C17 compiler, CMake 3.24+, Ninja, SDL3 3.4+, Vulkan headers and loader,
`glslangValidator` with SPIR-V validation support, and a Vulkan 1.1+ driver.
CMake uses installed dependencies; nothing is fetched implicitly during
configuration.

### macOS

Install Xcode command line tools and the dependencies:

```sh
xcode-select --install # Skip if Xcode or command line tools are installed.
brew install cmake ninja sdl3 vulkan-headers vulkan-loader molten-vk glslang vulkan-validationlayers
```

From the repository root:

```sh
cmake --preset debug
cmake --build --preset debug
cmake --build --preset debug --target run
```

Launch the editor:

```sh
cmake --build --preset debug --target run_editor
```

You can also open `build/debug/afterlight_editor.app` or
`build/debug/afterlight_sandbox.app` in Finder. The application
uses local Homebrew libraries; it is not yet a standalone distributable bundle.

To run for a fixed number of presented frames:

```sh
./build/debug/afterlight_sandbox.app/Contents/MacOS/afterlight_sandbox --frames 120
```

To inspect a saved scene or save a snapshot when the sandbox exits:

```sh
./build/debug/afterlight_sandbox.app/Contents/MacOS/afterlight_sandbox --save-scene build/debug/snapshot.afterlight.json
./build/debug/afterlight_sandbox.app/Contents/MacOS/afterlight_sandbox --scene build/debug/snapshot.afterlight.json
```

`--scene PATH` loads that scene without the default demo animation changing its
transforms. `--save-scene PATH` writes only the requested file, on exit after
successful initialization. Add `--frames 120` for a short automated capture.
Without `--scene`, the default file is found relative to application resources,
so launching from another directory or Finder works. The sandbox provides the built-in `cube` mesh and loads asset manifests from
custom scenes. OBJ meshes and PNG/JPEG/BMP/TGA textures can be imported through
the editor and inspected here. The scene format stores asset file references, materials, hierarchy, lighting and shadow settings;
it does not embed asset bytes, camera state, or game code. See [scene format](docs/scene-format.md).

On macOS, Afterlight prefers MoltenVK when several drivers expose the same GPU.
Startup logs name the selected driver. Other compatible drivers remain fallbacks;
Vulkan loader environment variables can restrict the available drivers explicitly:

```sh
VK_DRIVER_FILES="$(brew --prefix)/etc/vulkan/icd.d/MoltenVK_icd.json" \
  cmake --build --preset debug --target run
```

Debug builds request `VK_LAYER_KHRONOS_validation` when available. If it is not
found, the engine logs a warning and continues. With a nonstandard SDK install,
set `VK_LAYER_PATH` to the directory containing `VkLayer_khronos_validation.json`.

### Linux and Windows

The code uses SDL3 and Vulkan rather than platform-specific window APIs. These
platforms have not yet been tested. Install the prerequisites with your package
manager or the Vulkan SDK, and use the same CMake presets from a shell with a C
compiler and Ninja available. Set `CMAKE_PREFIX_PATH` in a local
`CMakeUserPresets.json` if SDL3 is outside CMake's search path.

The executable on Linux is `build/debug/afterlight_sandbox`; on Windows it is
`build/debug/afterlight_sandbox.exe`. The `run` target works on all platforms.
Windows builds require the SDL3 and Vulkan loader DLLs to be discoverable at
runtime, for example on `PATH`.

### Release

```sh
cmake --preset release
cmake --build --preset release
cmake --build --preset release --target run
```

Validation is off by default in Release. Pass `--validation` or `--no-validation`
to the executable to override the default. `--help` lists the sandbox arguments.

## Verify

```sh
ctest --preset debug
```

The 44-test suite includes CPU shadow fitting and GPU pixel checks for both the
simple single map and four-cascade variant: cast/receive switches, PCF edges,
cascade selection/blending, offscreen and distant casters, parent visibility,
quality changes and resizing. Graphics tests enable synchronization validation.
See [shadow controls and implementation](docs/shadows.md).

New rendering checks compare actual pixels for minified checker textures, PBR
roughness/metallic response, emission, normal maps, exposure, point-light range
and spot cone edges. CPU checks exercise all new fields, legacy scenes, malformed
JSON, limits, undo and asset-browser filtering/assignment.

Fixed-simulation and collision tests additionally cover render-rate independence,
bounded catch-up, buffered tick input, paused steps, primitive queries, slopes,
wall sliding, moving platforms, collider persistence and actual course gameplay.

The runtime/hot-reload tests load real C shared libraries, change behavior while
playing, reject bad ABI/initialization/truncated rebuilds, retain the last good
code and clean up shadow libraries. Gizmo tests check constrained transforms and
snapping; hierarchy tests cover composition, inherited visibility, cycles and
subtree undo. Asset tests cover OBJ parsing/normals/UVs, images and manifests.

The editor document test checks grouped edits, stable selections, undo/redo,
dirty tracking, failed IO, and history bounds. Its graphics smoke test renders
the Nuklear interface, resizes, injects quick clicks into add/duplicate,
undo/redo, visibility and numeric transform controls, drags a gizmo, tests text/repeat input,
runs Play/Stop, and saves/reloads the result.

The scene IO test checks JSON round trips, exact 64-bit IDs, deleted/exhausted ID
history, Unicode/escaped names, transforms, visibility, malformed files, file and
object limits, and preservation of saved files on failure. Run it without a
window using `ctest --preset debug -R afterlight.scene_io`.

The CPU-only scene test checks copied names, object growth/removal, stable IDs,
visibility, transform composition, and invalid input without creating a window.
Run it alone with `ctest --preset debug -R afterlight.scene`.

The driver-policy test checks macOS MoltenVK preference and GPU fallback without
opening a window. The smoke test also checks that initialization keeps the window
hidden and duplicate size notifications do not rebuild the swapchain.

The math test runs without a window and checks vector operations, matrix
composition, rotations, inverse/singular matrices, view transforms, Vulkan depth projection, and camera
movement/pitch limits. Run it alone with `ctest --preset debug -R afterlight.math`.

The graphics smoke test needs a desktop session and a working Vulkan driver.
It draws objects sharing two named mesh assets, grows/compacts the asset registry,
replaces/removes meshes while frames are in flight, checks visibility, missing
assets, object deletion, scene detach/reattach, and updates object/camera transforms.
It also resizes through landscape and portrait sizes,
and minimizes/restores the window. It checks invalid indices/cameras, quick key
presses, held keys, mouse button events, and capture/input reset on focus loss.
It also checks failed game initialization and quitting during initialization.
Validation errors, including errors during teardown, fail the test.
Synchronization validation is enabled for the test when the validation layer is
installed. To require that validation is actually available:

```sh
AFTERLIGHT_REQUIRE_VALIDATION=1 ctest --preset debug
```

Tested on an Apple M4 Mac with MoltenVK, including Debug and Release builds.
See [sanitizer checks and the macOS module-unload fix](docs/testing.md).

## Layout

```text
engine/include/afterlight/afterlight.h   Public engine, mesh, and input API
engine/include/afterlight/math.h         Vector and matrix API
engine/include/afterlight/camera.h       Flying camera API
engine/include/afterlight/mesh.h         Mesh data and asset-name definitions
engine/include/afterlight/scene.h        CPU-only objects and scene API
engine/src/scene.c                      Object lifetime, IDs, names, transforms
engine/src/scene_io.c                   Versioned JSON save/load and file replacement
engine/include/afterlight/game_module.h Versioned C game/host contract
engine/src/asset.c                      OBJ/image loading and path resolution
engine/src/texture_vulkan.c             Shared image upload and descriptors
engine/include/afterlight/shadow.h       Directional shadow settings
engine/src/shadow_math.c                Cascade splits, fitting and stabilization
engine/src/shadow_vulkan.c              Per-frame shadow arrays and depth passes
game/demo_game.c                       Reloadable C gameplay example
editor/runtime.c                       Play copies, module loading and hot reload
sandbox/scenes/demo.afterlight.json     Default sandbox scene
engine/src/math.c                       C math implementation
engine/src/camera.c                     Camera movement and view matrix
engine/src/engine.c                     Lifetime, events, input, timing
engine/src/renderer_vulkan.c            Buffers, pipeline, depth, presentation
engine/shaders/mesh.vert                Model/view/projection vertex transform
engine/shaders/mesh.frag                Texturing, lighting, PCF and cascade blending
engine/shaders/shadow.vert              Light-space depth transform
sandbox/main.c                         Example C game and camera controls
sandbox/cube.h                         Indexed cube geometry
tests/vulkan_policy.c                  Vulkan device/driver selection checks
tests/scene_io.c                       Scene persistence and malformed-file checks
tests/scene.c                          CPU-only scene and transform checks
tests/math.c                           Math and camera checks
editor/                                C editor, document history, Nuklear UI, dialogs
engine/src/ui_vulkan.c                  Overlay atlas, buffers, alpha blending
engine/include/afterlight/overlay.h     Renderer-independent overlay packet
tests/editor_document.c                Editor document regression checks
docs/editor.md                         Editor usage and limitations
tests/smoke.c                          Graphics, input, and lifecycle checks
docs/architecture.md                   Ownership and editor boundaries
```

The sandbox links `Afterlight::Engine`. Its source includes only the public
Afterlight header and the C standard library; SDL and Vulkan stay inside the
engine. The game supplies optional `init`, `update`, and `shutdown` callbacks.
`af_engine_upload_mesh(engine, "cube", &mesh)` copies vertices and 32-bit indices
into an engine-owned named asset. Uploading the same name replaces that asset;
all objects using it see the replacement. Failed uploads preserve existing assets.
`af_engine_remove_mesh` releases an asset after outstanding GPU work completes.

Scenes exist independently of the engine. `af_scene_add_object(scene, "Left cube",
"cube")` creates a visible object with an identity transform and a stable ID.
Use that ID to rename, transform, hide, or remove it. Objects store a mesh asset
name and separate position/rotation/scale. Name lengths are bounded by the
capacities in the public headers; names are copied, and empty/overlong names fail.

`af_engine_set_scene` attaches a borrowed scene. Detach it with NULL before
freeing it, or keep it alive until engine destruction. The next frame reflects
object edits without mesh uploads. Objects with unloaded mesh names are skipped
and counted in `af_engine_stats.missing_meshes`. Stats also expose `loaded_meshes`
and the last submitted frame's `draw_calls`. Sharing saves GPU allocations and
uploads; each visible object currently gets one indexed draw, not instancing.

Read-only object views are invalidated by structural scene changes. Retain IDs
and query again after adding/removing objects. IDs are local to a scene and never
reused after deletion. The temporary single-mesh/model APIs have been replaced
by named assets and scene objects.

`af_engine_set_camera` copies camera state; the renderer builds perspective
projection using the current viewport aspect ratio. Math uses column-major
matrices, column vectors, a right-handed Y-up world, and radians. Object transforms
compose as `T * Rz * Ry * Rx * S`. Vulkan projection maps depth to [0, 1] and flips
clip Y. The color pipeline is opaque and uses materials, textures, normals and ambient/
directional diffuse lighting, with an unlit material option. It has depth testing
and no alpha blending. Mesh uploads wait for GPU work; transform and camera edits do not.

GLSL sources in `engine/shaders/` compile to validated SPIR-V headers during the
build and are embedded in the engine. Editing a shader rebuilds its header and
the renderer automatically. The running app does not need shader files or a
shader compiler, and can launch from any working directory.

Scene persistence is available through `af_scene_load` and `af_scene_save`.
The versioned JSON format preserves object IDs and their allocation history,
names, mesh references, transforms, visibility, and object order. Loading creates
a new CPU-only scene; saving replaces a destination only after the complete file
is written. See [scene format and API details](docs/scene-format.md).

## C gameplay and reload

Press Play/F5 in the editor, edit `game/demo_game.c`, then rebuild just the game:

```sh
cmake --build --preset debug --target afterlight_game
```

Auto reload adopts a valid rebuilt library without restarting the editor.
Failed builds/loads retain the previous running code. Stop restores the edit
scene. Runtime scene state survives a reload; private module state is freshly
initialized. See [C game contract and workflow](docs/game-code.md).

The scene format is version 9 and still reads versions 1–8. Asset bytes stay in
source files referenced by the manifest. See [assets/materials](docs/assets.md),
[scene format](docs/scene-format.md) and [editor controls](docs/editor.md).

## Next milestones

- glTF animation and richer material extensions.
- Reflection probes, multiscale bloom and adaptive local shadows.
- Rigid-body dynamics, spatial/streaming audio and module state migration.

The engine remains a small prototype. These next items are not implemented.
The editor uses vendored [Nuklear](third_party/nuklear/README.md) under MIT and
[Fira Sans](editor/assets/README.md) under SIL OFL. Image loading uses
[stb_image](third_party/stb/README.md) under its MIT option. All own engine/game
code and bundled library implementations compile as C; shaders are GLSL.

## References

- [SDL3 Vulkan integration](https://wiki.libsdl.org/SDL3/CategoryVulkan)
- [Vulkan portability enumeration](https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_portability_enumeration.html)
- [Vulkan vertex input and buffers](https://docs.vulkan.org/tutorial/latest/04_Vertex_buffers/00_Vertex_input_description.html)
- [Vulkan depth buffering](https://docs.vulkan.org/tutorial/latest/07_Depth_buffering.html)
- [Vulkan swapchain semaphore reuse](https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html)
