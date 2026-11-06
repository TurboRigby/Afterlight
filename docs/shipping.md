# Player, project creation, richer models, reload and export (0.7)

This milestone implements engine/tooling items 1–6. The proposed finished game
(item 7) is deliberately deferred until the engine is further developed.
All engine/editor/game/runtime code is C17; project creation/export uses Python's
standard library. CMake now finds a Python 3 interpreter for these authoring tools.

Builds and exports include MPL-2.0 notices and a build-time covered-source
archive. Starter projects preserve template licenses and archive their marked
MPL source when built. Keep these files with distributed binaries; see
[licensing and distribution](licensing.md) for scope and custom module handling.

## Generic player

Build `afterlight_player`, then run any saved project without the editor:

```sh
cmake --build --preset debug --target afterlight_player
build/debug/afterlight_player.app/Contents/MacOS/afterlight_player \
  --project build/debug/playground.afterlight.project
```

On non-macOS builds use `build/debug/afterlight_player`. The player loads the
project scene/assets and a matching C game module, then runs its fixed/render
callbacks. No obstacle-course code is built into the generic player. F6 pauses;
Escape releases the mouse, then exits on a second press. Click to recapture if
the C game supports it. `--hot-reload` enables C-module and asset watching during
development; packaged play does not watch by default. `--frames N` bounds a run.

`--check --project PATH` validates scene JSON, decodes every referenced asset,
and validates the loaded C module's ABI/callback table without opening Vulkan or
calling game callbacks. Native module constructors still run when loading the
library, so use your own trusted game modules. Without `--project`, the player
loads `game.afterlight.project` from its resource directory, as used by exports.

## New Project wizard

Open **Project** in the editor (Ctrl/Cmd+Shift+N). In **New Project wizard / Export**, enter a name
and a new destination directory, then choose **Create starter project**. It
creates a floor/player/camera scene, editable `game/game.c`, matching public
headers, a CMake module target, project JSON, README and `.gitignore`. The current
open document is preserved. Existing directories are never overwritten.

The equivalent command is:

```sh
python3 scripts/afterlight.py new /path/to/MyGame --name "My Game"
cmake -S /path/to/MyGame -B /path/to/MyGame/build -G Ninja
cmake --build /path/to/MyGame/build
```

Open `/path/to/MyGame/game.afterlight.project` in the editor or player. Rebuild
the generated C target to reload it while playing. The default module location
is `build/game.so` (`game.dll` on Windows); use the documented `build` directory.
The generated build needs a C compiler/CMake, not SDL/Vulkan headers or a second
engine link. Refresh its copied public headers when changing engine ABI.
The macOS editor bundles templates and the player for its wizard/export tools;
a Python 3 installation is still needed for authoring, not for exported play.

## Rich static glTF import

Editor **Import mesh (OBJ/glTF)** preserves a glTF/GLB model's node hierarchy and
node-local TRS. Every primitive becomes a child object with its own material;
mesh geometry is shared by stable source-mesh/primitive keys. Transform-only
objects use the reserved mesh name `__node` and produce no draw or missing-asset
warning. Changing a parent's transform moves its imported children normally.
Import is one undoable document action. Invalid input adds no CPU nodes/assets.

Core opaque/cutout/blended metallic/roughness materials import base-color factors/images,
normal maps/strength, metallic and roughness factors/maps, unlit flags and
emissive factors. Color images upload as sRGB; normal and packed surface images
are linear. Surface map **G** multiplies roughness and **B** multiplies metallic.
The Inspector and Assets browser can assign/import a metallic/roughness map.
Embedded GLB images, base64 image URIs and external images/buffers are supported.

The saved asset manifest retains source references such as
`model.glb#mesh=0,primitive=1` and `model.glb#image=2`. They reload from the original
model, so the editor does not create unexplained intermediate files beside it.
`af_model_file_load` returns CPU nodes, primitives, materials, decoded images and
external dependency paths. `af_scene_import_model` transactionally instantiates
that data. The older `af_mesh_file_load_gltf` remains a flattened geometry API for
existing callers; the editor uses the richer path.

Current limits: triangle meshes, one UV set (UV0), repeat wrapping and no
texture transforms. Core TRS animation and four-influence skins are supported
within the [0.10 budgets](development-010.md). Morphs,
mesh compression, material texture extensions, emissive textures and imported
cameras/lights are not implemented. Unsupported required features are rejected. Sampler
filtering uses the engine's trilinear/anisotropic policy. Node-local shear and
singular transforms are rejected because editable objects use TRS. Nonuniform
and mirrored TRS are supported. Material values must fit engine ranges. Model
limits: 1,048,576 triangle vertices, 1024 primitives, 16384 nodes, 128 images,
64 MiB source buffers, 128 MiB parser allocation and 256 MiB decoded images.
The current renderer requires five bound descriptor sets for its material maps;
device capability is checked before creating the scene pipeline.

## Reloading assets

**Reload changed assets** in the Assets panel is enabled by default. A watcher
checks source fingerprints every 250 ms and waits at least 350 ms of stable input.
It follows a model's external buffers/images, even when the glTF file is unchanged.
Mesh/image entries from one model are staged under temporary GPU names and
committed together only after all uploads succeed. Invalid/truncated/missing input
retains previous resources, reports rejection and retries after another change.
Temporary resources are released. Audio reload retains its previous clip on a
failed decode. Manual **Reload asset from disk** uses the same grouped path.

Watching changes GPU geometry/images, not the saved scene or its undo history.
Existing node transforms, material edits and topology remain document data.
Reimport after changing a model's hierarchy, primitive layout or material factors;
a removed referenced primitive rejects reload. This avoids overwriting edits made
in the engine Inspector. Failed input does not force document changes.

## Export

Build a normal Release player and game module, save the scene/project, then:

```sh
cmake --preset release
cmake --build --preset release
python3 scripts/afterlight.py export build/release/playground.afterlight.project \
  --output build/AfterlightExport --player build/release/afterlight_player.app
```

Use the executable path instead of `.app` on Linux/Windows. The editor's **Export
saved project** uses its current-config player. The CMake **export_game** target
uses `AFTERLIGHT_EXPORT_PROJECT` and `AFTERLIGHT_EXPORT_DIRECTORY` cache paths;
defaults are the bundled course project and a new `exported-game` build directory.
Build the selected custom project's module before exporting it.

Export checks the project with the generic player first, copies the startup scene
and adjacent `*.afterlight.json` scenes, walks manifest/model dependencies,
rewrites references to package-local paths and includes the C game module.
Both external and embedded glTF/GLB forms preserve their selectors. Asset files
are limited to 64 MiB each/1 GiB total, scenes to 256. Export uses a sibling staging
directory and publishes it only after completion; existing destinations are
refused and failure removes only its own staging output. Source files are untouched.

On tested macOS, `Game.app` includes SDL, Vulkan loader and MoltenVK dependencies,
rewritten library references, an ICD manifest and dependency license notices.
It uses ad-hoc signing; signing/notarization for public distribution requires your
own developer identity. Open `Game.app` or run its player binary without arguments.
The game does not require Python or a Homebrew installation at runtime. It targets
the exporting OS/CPU and still needs a supported Metal/Vulkan device.

Linux packaging copies non-core linked libraries and creates `run.sh`; destination
Vulkan drivers remain system-provided. Windows copies adjacent runtime DLLs;
provide SDL/Vulkan runtime DLLs beside the player before exporting. These branches
are not yet tested on their native platforms. Sanitizer runtimes are explicitly
rejected for distribution; export a normal Release build. Packaging is not a
cross-compiler, installer generator, or asset cook/compression pipeline.

`export-manifest.json` records platform, scene/asset counts and runtime libraries.
The end-to-end regression creates/builds a starter C module, runs it, imports a
textured multi-material model, exports, deletes source data, moves the output and
runs its player. Separate Vulkan tests compare rendered metallic/roughness maps
and verify dependency reload/last-good recovery. See [testing](testing.md).

Scene names are preserved in exports for deferred transitions. Optional project
`input.json` and `prefabs/` resources are bundled; see [0.12](development-012.md).

Physics scenes save body components in scene format 13. Exports include the
original dynamic crate prefab, input actions and the same C physics module used
by the editor. The integration test runs both rooms after source deletion and
relocation; see [0.13 supported physics and controls](development-013.md).
