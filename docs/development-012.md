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
The CPU limit is 4096 objects per prefab. This is explicit instancing, not a live
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
