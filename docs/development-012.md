# Afterlight 0.12: reusable engine tools

Eight commits cover configurable character rigs, input actions/controller support,
prefabs, state-preserving reload, scene transitions, spatial audio, transparent
materials, and simple-to-complex integration/export checks.

## Character rigs

Scene version 11 adds a `rig` to objects, with an enable flag, five source clip
indices (Idle/Walk/Run/Jump/Fall), root node, model-space walk/run speeds, initial
root-motion mode and camera/turn settings. Versions 1–10 use disabled defaults.
The Animation panel edits bindings and settings through undo history. C modules
read the component and scale model speeds/displacement by the visual rig scale;
the sample accepts positive uniform visual scale. Clip names are unrestricted.
Game ABI is 11. Explicit mappings validate all clips against the loaded asset.

## Input actions

`input.h` defines named, rebindable actions with keyboard pairs, SDL-standard
controller buttons/axes, deadzones and mouse axes. The engine opens the first
available mapped controller, handles disconnect/reconnect and neutralizes input
on focus loss. Presses persist until the next fixed tick and are consumed once.
Character gameplay uses MoveX/MoveY/LookX/LookY/Jump/Run/Interact/Restart/Release.
The editor Input panel changes bindings and saves/loads JSON maps. Games can
load maps with `af_engine_load_input_map`; custom actions use `af_engine_bind_action`.
Stick look is converted into per-frame pixel-equivalent motion. Physical
controller hardware is not required for the CPU mapping regressions.

## Prefabs

`prefab.h` saves a selected hierarchy and referenced assets as a single-root
scene file (`.afterlight.prefab`). Instantiation allocates fresh IDs and remaps
parents, spawn references and rig bindings. Conflicting asset names are aliased;
matching absolute assets are shared. Placement overrides the instance root TRS.
Instances are independent editable copies; source changes do not propagate.
External spawn/rig references are rejected, and a failed instance changes nothing.
Use the editor Prefabs panel to save or instantiate through undo history. C games
can instantiate through the host and then call `load_scene_assets` before drawing.
For runtime spawning, prefer `spawn_prefab`, which stages CPU objects and assets
before committing. The CPU limit is 4096 objects per prefab. This is explicit instancing, not a live
prefab override/inheritance system.

## State-preserving hot reload

C modules may provide paired `save_state`/`load_state` callbacks and a format
version. The host queries a blob size, limits it to 1 MiB, initializes a candidate
against a cloned scene, then lets that candidate validate/restore the blob.
Failed serialization, incompatible versions or rejected data keep the old module
and scene running. Modules without callbacks keep the previous reset behavior.
The character module preserves its POD controller/progress/platform state in a
versioned same-process blob. Private pointers must never be copied blindly.
Pause, camera ownership and gameplay status survive a successful reload.

## Scene transitions and portable resources

C games request a scene by name through `host->request_scene(host,"level")`.
Names contain only letters, digits, underscores and hyphens, resolving to
`level.afterlight.json` beside the startup scene. Requests take effect after a
callback returns; CPU data and the previous module are released only after a
candidate initializes. A missing/invalid scene keeps the current game running.
Transitions initialize fresh game state; hot reload remains a separate operation.
Assets use the bounded shared engine cache. An initialization failure reloads the
previous scene's assets; applications should keep those source assets available.
Loads are synchronous. Project files remain version 1.

The player/editor load an optional `input.json` beside the project. Exports retain
scene filenames (and therefore aliases), package that input map and copy the
project's `prefabs/` subtree with rewritten asset paths. Source removal and
relocation remain supported.

## Spatial audio and overlapping voices

WAV clips decode to finite stereo float PCM at 48 kHz within the 64 MiB cache.
Up to 32 independent voices can play the same clip simultaneously. Positional
emitters use equal-power stereo panning and quadratic distance attenuation;
listener movement updates gains in the main-thread mixer. The device queue is
kept to roughly 43 ms. `play_sound_at` is available to C modules; character
footsteps use it. The CPU gain/mixing tests use SDL's dummy audio driver. This
adds positioned one-shots, not streaming music, occlusion, reverb or Doppler.

## Cutout and blended materials

Scene version 12 adds material `alpha_mode` (opaque/mask/blend) and
`alpha_cutoff`; versions 1–11 retain opaque defaults. Core glTF alpha modes import
and the Inspector exposes opacity, mode and threshold. Opaque/cutout geometry
writes depth first; blended objects sort back-to-front by object origin and test
depth without writing it. Straight alpha blending is supported. Intersecting
transparent geometry is not order-independent transparency.

Cutout thresholds include texture, vertex and material alpha in both color and
directional/local shadow passes. Blended objects do not cast opaque shadows.
Texture uploads and alpha edits invalidate the shadow cache. Portable draw pushes
remain within 128 bytes. Pixel tests check discard, compositing, sorted layers,
resize, cutout shadows and texture-reload invalidation with Vulkan validation.

## Simple-to-complex integration demos

Run `run_toolbox_editor` first, then `run_toolbox-course_editor`, and press Play.
The matching `_player` targets launch standalone play. Existing character and
animation demos remain available. WASD/Shift/Space move/run/jump; E toggles root
motion, Q spawns an independent crate prefab, N switches practice/course scenes,
R restarts and Escape releases the mouse. The prefab combines a solid collider,
textured cutout screen and blended glass. Input bindings are in `sandbox/input.json`.
The fixture generator is `python3 scripts/generate_toolbox_assets.py`.

The real-module Vulkan integration exercises movement, root motion, prefab asset
loading, state-preserving reload, two deferred transitions, shadows and resize.
Export tests copy both scenes, input bindings and prefab assets, remove the source
project, relocate the output and run each startup scene. Scene format is 12,
project format remains 1 and C game ABI is 11: rebuild custom C modules and restart
older editors. `spawn_prefab` stages CPU changes and assets before committing.

Controller mappings are covered with deterministic CPU samples; physical
controller hardware and native Windows/Linux interactive graphics remain untested.

## Verification results

On Apple M4/MoltenVK, all 82 checks passed (the complete ASan/UBSan suite plus
final targeted Debug/sanitizer reruns after fixes). Vulkan tests require
synchronization validation. Linux Clang 18 ASan/UBSan passed all 50 CPU tests.
Static analysis of the new rig/input/prefab/audio/runtime/game code found no
issues. Physical controller hardware was not available for verification.

Both Debug and Release exports were relocated after their source project was
removed. The real-module integration test then spawned a textured prefab,
restored state during reload and switched practice/course scenes in both
directions from those relocated resources. Source archives and license coverage
were rebuilt and checked after the documentation changes.
