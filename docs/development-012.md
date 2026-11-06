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
