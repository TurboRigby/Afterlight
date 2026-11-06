# Afterlight projects

For the generic player, New Project wizard, model materials/hierarchy, asset
reload and export workflow, see [the 0.7 engine/tooling milestone](shipping.md).
The finished-game milestone remains deferred.

The editor's **Project** button opens the project panel. **Open project** loads
its startup scene and selects its C game module together. Unsaved scene changes
use the existing Save/Discard/Cancel flow. Opening a project does not execute its
module until Play. **Save project as** records the current saved scene and selected
module; save the scene first. Project changes do not go into scene undo history.

```sh
cmake --preset debug
cmake --build --preset debug --target run_playground_editor
# Or explicitly select a project:
build/debug/afterlight_editor.app/Contents/MacOS/afterlight_editor \
  --project build/debug/playground.afterlight.project
```

CMake generates `demo.afterlight.project` and `playground.afterlight.project` in
each build directory. The editor also bundles relative project files and module
copies. Development launches watch the original build module; a moved bundle can
use its packaged module. `--game LIBRARY` overrides a project's module for that
launch. `--scene PATH` without `--project` remains a scene-only launch.

Project JSON is version 1, with exactly these fields:

```json
{
  "format": "afterlight.project",
  "version": 1,
  "name": "My game",
  "startup_scene": "scenes/start.afterlight.json",
  "game_module": "game/my_game.so"
}
```

References resolve against the project file, independent of the current working
directory. Save writes relative references when the target is inside the project
folder; external targets remain absolute. A portable project must include its
scene, referenced assets and a module built for the destination OS/architecture.
The project description is consumed by the player, project wizard and exporter.
Native C modules remain trusted, in-process code.

`af_project_load` preserves its output on failure. `af_project_save` writes a
sibling temporary file and replaces the destination after a complete write.
UTF-8, duplicate/unknown fields, unsupported versions and oversized input are
validated. Project files are limited to 64 KiB. CMake's development examples point
to the source scenes; bundled templates start untitled to protect shipped scenes.

Completed in order for Afterlight 0.6:

1. Project files and editor Open/Save Project.
2. Saved Player, Camera and course-role components, with explicit spawn IDs.
3. Inspector movement, jump height, gravity, capsule, sensitivity and camera fields.
4. F6 maximized Play viewport, crosshair, pause and mouse ownership hints.
5. Ordered checkpoints, fall respawns, timer, locked finish and new-run reset.
6. Named WAV effects, asset import/preview and jump/land/checkpoint/finish cues.
7. Static glTF 2.0/GLB import, including the original beacon model in the course.

All own engine/editor/game code remains C17. See [gameplay](gameplay.md) for the
controller/course, [assets](assets.md) for model/audio limits, and
[testing](testing.md) for verification. Scene format is 10 and C game ABI is 10;
restart older editors and rebuild custom modules against the current headers.
