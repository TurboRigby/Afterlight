# Afterlight MCP Server

Afterlight includes a zero-dependency **Model Context Protocol (MCP)** server so
AI coding and game-creation agents can create projects, author scenes, generate
or import assets, sculpt terrain and foliage, configure PBR materials, lighting,
cameras, physics, water, and post-processing, build and run C17 game modules,
inspect runtime or compiler errors, and capture real Metal 3 / Vulkan GPU
screenshots to visually verify every iteration without manually operating the
editor.

## Architecture

```
MCP Client (Antigravity / Claude / Cursor / Codex / custom)
   │  JSON-RPC 2.0 over stdio (ndjson or Content-Length)
   ▼
tools/mcp/afterlight_mcp.py   (Python 3 stdlib only — zero external packages)
   ├── tools/mcp/procedural.py (PNG encoding, procedural .obj meshes & .png textures)
   ├── build/*/afterlight_agent (Pure C17 helper linked against Afterlight::Engine)
   │      • Lossless v30 scene API (af_scene_*, af_landscape_*, af_scene_import_model)
   │      • Real GPU capture (Metal 3 or Vulkan) + render/ray-tracing stats
   ├── build/*/afterlight_player (Validation --check and timed runtime execution)
   └── scripts/afterlight.py     (Project scaffolding, export, and single-binary bundling)
```

Using a native C17 `afterlight_agent` helper guarantees exact round-trip parity
with the engine's scene loader/saver, physics raycaster, glTF/GLB importer,
terrain generator, and GPU renderer while isolating crashes or user C game module
bugs from the MCP server process.

## Build prerequisites

Build `afterlight_agent` and `afterlight_player` (included in default builds):

```sh
cmake --preset debug
cmake --build --preset debug --target afterlight_agent afterlight_player --parallel
```

No `pip install` or Python virtual environment is required — `afterlight_mcp.py`
uses only the Python 3 standard library.

## Connecting an MCP client

Configure your MCP client to run `python3 /path/to/afterlight-engine/tools/mcp/afterlight_mcp.py`
over stdio. Optional CLI flags: `--build-dir`, `--agent`, `--player` (or env vars
`AFTERLIGHT_BUILD_DIR`, `AFTERLIGHT_AGENT_BIN`, `AFTERLIGHT_PLAYER_BIN`).

### Antigravity / Claude Desktop / Cursor (`mcpServers` JSON)

```json
{
  "mcpServers": {
    "afterlight": {
      "command": "python3",
      "args": [
        "/ABSOLUTE/PATH/TO/afterlight-engine/tools/mcp/afterlight_mcp.py",
        "--build-dir",
        "/ABSOLUTE/PATH/TO/afterlight-engine/build/debug"
      ]
    }
  }
}
```

### Claude Code CLI

```sh
claude mcp add afterlight -- python3 /ABSOLUTE/PATH/TO/afterlight-engine/tools/mcp/afterlight_mcp.py
```

## Tools (39)

### Project & build lifecycle
- `project_create` — Scaffold a new project (`game.afterlight.project`, `scenes/start.afterlight.json`, `game/game.c`, `CMakeLists.txt`, headers).
- `project_inspect` — Inspect manifest, startup scene, scenes, game sources, assets, and compiled module status.
- `project_build_module` — Configure and compile `game/game.c` with CMake/Ninja and return parsed compiler diagnostics (`file`, `line`, `column`, `severity`, `message`).
- `project_validate` — Run `afterlight_player --project <proj> --check` to verify manifest, startup scene, assets, and module ABI.
- `project_export` — Package a playable game directory via `scripts/afterlight.py export`.
- `project_bundle` — Bundle into a single standalone executable via `scripts/afterlight.py bundle`.

### Scene lifecycle & queries
- `scene_create` — Create a new v30 `.afterlight.json` scene (optional starter Ground, Player/Camera, ACES skybox).
- `scene_inspect` — Dump all objects, components, assets, sun/local lights, shadows, render settings, terrain, foliage layers, and active game camera.
- `scene_raycast` — Raycast against all scene colliders and terrain heightfield for exact placement and line-of-sight checks.
- `scene_undo` — Roll back the most recent MCP mutation to a scene (per-scene 32-step snapshot history).

### Entities & components
- `object_create` — Add an entity with mesh, transform, parent, PBR material, collider, rigid body, gameplay/player/camera, animation, and/or physical water volume.
- `object_update` — Patch any subset of fields or components on an existing object by `id` or `object_name`.
- `object_delete` — Delete an object (recursively removing children by default).
- `object_duplicate` — Duplicate an object and all its components with optional `new_name` and position `offset`.
- `collider_part_add` / `collider_part_remove` — Author box/sphere/capsule child parts on `compound` colliders.
- `joint_add` / `joint_remove` — Connect rigid bodies with `hinge` or `distance` joints (limits and motors supported).

### Assets, procedural meshes, textures & custom shaders
- `asset_import` — Validate and import `model` (full glTF/GLB hierarchy, PBR materials, skins, animations), `mesh` (`.obj`, `.gltf`, `.glb`, `.afgeom`, `.afpages`), `texture`, `normal_map`, `metallic_roughness`, `environment` (`.hdr`), `animation`, or `audio` (`.wav`).
- `asset_remove` — Unregister a named asset from a scene manifest.
- `generate_mesh` — Synthesize clean Wavefront `.obj` meshes (`box`, `plane`, `sphere`, `cylinder`, `cone`, `ramp`, `stairs`, `arch`, `rock`, `tree`) with normals and UVs, optionally registering directly into a scene.
- `generate_texture` — Synthesize tileable RGBA8 PNG textures or tangent-space normal maps (`checker`, `grid`, `brick`, `wood`, `grass`, `stone`, `sand`, `metal`, `noise`, `gradient`, `normal_noise`, `normal_brick`, `normal_grid`), optionally registering directly into a scene.
- `compile_shader` — Compile a GLSL 450 fragment shader to SPIR-V (`.spv`) with `glslangValidator` (runs on both Vulkan and native Metal via SPIRV-Cross).

### Terrain & foliage
- `terrain_create` — Create a chunked `af_landscape` terrain with optional `procedural_hills`, 4 PBR splat layers, and `auto_paint_slopes`.
- `terrain_sculpt` — Sculpt or paint terrain (`raise`, `lower`, `smooth`, `flatten`, `paint_layer`, `auto_paint_slopes`, `procedural_hills`, `set_layer`).
- `terrain_query` — Sample terrain heights, surface normals, and 4-layer splat weights at single or batch `[x, z]` coordinates.
- `foliage_add_layer` — Configure an instanced terrain foliage layer (mesh, PBR material, slope/height/density/spacing/scale rules).
- `foliage_populate` — Run `populate`, `paint`, `erase`, or `clear` on a foliage layer.
- `foliage_remove_layer` — Remove a foliage layer by index.

### Lighting, shadows, cameras, wind & post-processing
- `lighting_set_sun` — Set directional sun direction, color, intensity, ambient, and soft RT angular radius.
- `lighting_add_local` / `lighting_update_local` / `lighting_remove_local` — Manage up to 128 tiled point and spot lights.
- `lighting_set_shadows` — Configure cascaded/single directional shadow maps, resolution, distance, biases, and PCF filter radius.
- `render_set_settings` — Configure AA (`off`/`fxaa`/`smaa`/`taa`), exposure, tone mapping (`reinhard`/`aces`/`agx`), bloom, skybox & IBL environment, depth of field, motion blur, vignette, chromatic aberration, film grain, underwater/refraction, and hardware ray tracing (`ray_traced_shadows`, `ray_traced_water_reflections`, `ray_traced_material_effects`, `ray_traced_gi`).
- `wind_set` — Configure global wind speed, direction, gusts, and air density.
- `camera_configure` — Configure or create a scene camera with automatic `position` + `look_at` aiming.

### Run, inspect errors & visually verify
- `run_game` — Execute `afterlight_player --project <proj> --frames <N> --validation` and return exit code, stdout, stderr, and extracted warnings/errors.
- `capture_screenshot` — Render a scene or live running project (`run_game: true`) on the real Metal 3 or Vulkan GPU, save a full-resolution PNG to disk, and return both structured metrics (active backend, GPU/CPU ms, draw calls, visible/culled objects, missing meshes/textures, validation errors, ray-tracing flags, camera pose, engine stderr log) **and** an inline MCP `image/png` content block.

## MCP Resources & Prompts

- **Resources** (`resources/list`, `resources/read`): Exposes engine docs (`afterlight://docs/*`) and public C17 API headers (`afterlight://headers/game_module.h`, `afterlight.h`, `scene.h`, `material.h`, `landscape.h`, `water.h`, `physics.h`) so agents can write accurate C gameplay modules without guessing API signatures.
- **Prompts** (`prompts/list`, `prompts/get`): Provides `create_game` (full game creation loop) and `visual_iterate` (multi-angle screenshot inspection and polish loop).

## Verification

Run the end-to-end MCP integration test (exercises stdio framing, project creation, procedural meshes/textures, glTF import, terrain/foliage, physics/water, C module build + compiler error diagnostics, `run_game`, `scene_undo`, and live GPU screenshot capture):

```sh
ctest --test-dir build/debug --output-on-failure -R "^afterlight\.mcp_server$"
```
