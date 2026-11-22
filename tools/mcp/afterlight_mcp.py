#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Afterlight Engine MCP (Model Context Protocol) server over stdio.

Zero external dependencies (Python 3 standard library only). Pairs with the native
C17 `afterlight_agent` helper and `afterlight_player` so AI agents can create
projects, author scenes/entities/components, generate or import meshes/textures/
animations, sculpt terrain & populate foliage, configure lighting/post-processing,
compile C gameplay modules and SPIR-V shaders, run games to inspect errors, and
capture real GPU screenshots (Metal 3 or Vulkan) in a closed visual iteration loop.
"""
from __future__ import annotations

import argparse
import base64
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
import time
from typing import Any

# Ensure sibling helper imports work regardless of working directory.
_THIS_DIR = pathlib.Path(__file__).resolve().parent
if str(_THIS_DIR) not in sys.path:
    sys.path.insert(0, str(_THIS_DIR))

from procedural import decode_afra, downscale_rgba, encode_png, generate_obj_mesh, generate_texture_png

SUPPORTED_PROTOCOL_VERSIONS = ("2025-03-26", "2024-11-05", "2024-10-07")
DEFAULT_PROTOCOL_VERSION = "2024-11-05"

# Scene-mutating commands whose target scene file is snapshotted for scene_undo.
MUTATING_AGENT_COMMANDS = {
    "object_create",
    "object_update",
    "object_delete",
    "object_duplicate",
    "collider_part_add",
    "collider_part_remove",
    "joint_add",
    "joint_remove",
    "asset_import",
    "asset_remove",
    "terrain_create",
    "terrain_sculpt",
    "foliage_add_layer",
    "foliage_populate",
    "foliage_remove_layer",
    "lighting_set_sun",
    "lighting_add_local",
    "lighting_update_local",
    "lighting_remove_local",
    "lighting_set_shadows",
    "render_set_settings",
    "wind_set",
    "camera_configure",
}


def _read_engine_version(repo_root: pathlib.Path) -> str:
    vh = repo_root / "engine/include/afterlight/version.h"
    if vh.is_file():
        text = vh.read_text(encoding="utf-8", errors="ignore")
        m = re.search(r'#define\s+AF_VERSION_STRING\s+"([^"]+)"', text)
        if m:
            return m.group(1)
    return "0.25.0"


def _parse_compiler_diagnostics(text: str) -> list[dict[str, Any]]:
    diags: list[dict[str, Any]] = []
    pat = re.compile(r"^(.+?):(\d+):(?:(\d+):)?\s*(fatal error|error|warning|note):\s*(.+)$")
    for line in text.splitlines():
        m = pat.match(line.strip())
        if m:
            diags.append(
                {
                    "file": m.group(1),
                    "line": int(m.group(2)),
                    "column": int(m.group(3)) if m.group(3) else None,
                    "severity": m.group(4),
                    "message": m.group(5),
                }
            )
    return diags


def _extract_engine_issues(text: str) -> list[str]:
    issues: list[str] = []
    for raw in text.splitlines():
        line = raw.strip()
        if not line:
            continue
        low = line.lower()
        if (
            "[error]" in low
            or "[warn]" in low
            or "validation error" in low
            or "failed" in low
            or "missing" in low
            or "player project:" in low
        ):
            issues.append(line)
    return issues


def _rel_asset_path(scene_path: pathlib.Path, asset_path: pathlib.Path) -> str:
    try:
        rel = os.path.relpath(asset_path.resolve(), scene_path.resolve().parent)
        # Keep clean relative paths within the project tree; fall back to absolute if deeply disjoint.
        if rel.count("..") <= 3:
            return rel.replace("\\", "/")
    except ValueError:
        pass
    return str(asset_path.resolve()).replace("\\", "/")


# ---- Tool Schema Definitions ----

_VEC3_SCHEMA: dict[str, Any] = {
    "type": "array",
    "items": {"type": "number"},
    "minItems": 3,
    "maxItems": 3,
}
_VEC2_SCHEMA: dict[str, Any] = {
    "type": "array",
    "items": {"type": "number"},
    "minItems": 2,
    "maxItems": 2,
}
_COLOR_SCHEMA: dict[str, Any] = {
    "type": "array",
    "items": {"type": "number"},
    "minItems": 3,
    "maxItems": 4,
    "description": "Linear or sRGB-normalized [r, g, b] or [r, g, b, a] in 0..1.",
}

_MATERIAL_SCHEMA: dict[str, Any] = {
    "type": "object",
    "description": "PBR or legacy material settings.",
    "properties": {
        "alpha_mode": {"type": "string", "enum": ["opaque", "mask", "blend"]},
        "alpha_cutoff": {"type": "number"},
        "base_color": _COLOR_SCHEMA,
        "texture": {"type": "string", "description": "Registered texture asset name (or empty string to clear)."},
        "normal_map": {"type": "string", "description": "Registered normal_map asset name."},
        "normal_strength": {"type": "number"},
        "metallic_roughness_map": {"type": "string", "description": "Registered metallic_roughness asset name."},
        "shader": {"type": "string", "description": "Custom SPIR-V fragment shader path."},
        "physically_based": {"type": "boolean"},
        "roughness": {"type": "number", "description": "0.04 (mirror) to 1.0 (matte)."},
        "metallic": {"type": "number", "description": "0.0 (dielectric) to 1.0 (metal)."},
        "ior": {"type": "number", "description": "Index of refraction (default 1.5)."},
        "emission": _VEC3_SCHEMA,
        "emission_strength": {"type": "number"},
        "unlit": {"type": "boolean"},
        "cast_shadows": {"type": "boolean"},
        "receive_shadows": {"type": "boolean"},
    },
}

_COLLIDER_SCHEMA: dict[str, Any] = {
    "type": "object",
    "description": "Collision shape attached to the object.",
    "properties": {
        "type": {"type": "string", "enum": ["none", "box", "sphere", "capsule", "mesh", "compound"]},
        "enabled": {"type": "boolean"},
        "trigger": {"type": "boolean"},
        "center": _VEC3_SCHEMA,
        "half_extents": _VEC3_SCHEMA,
        "radius": {"type": "number"},
        "half_height": {"type": "number"},
        "layer": {"type": "integer"},
    },
}

_BODY_SCHEMA: dict[str, Any] = {
    "type": "object",
    "description": "Rigid body physics component.",
    "properties": {
        "enabled": {"type": "boolean"},
        "type": {"type": "string", "enum": ["static", "kinematic", "dynamic"]},
        "mass": {"type": "number"},
        "friction": {"type": "number"},
        "restitution": {"type": "number"},
        "linear_damping": {"type": "number"},
        "angular_damping": {"type": "number"},
        "gravity_scale": {"type": "number"},
        "allow_sleep": {"type": "boolean"},
        "continuous": {"type": "boolean"},
        "velocity": _VEC3_SCHEMA,
        "angular_velocity": _VEC3_SCHEMA,
    },
}

_GAMEPLAY_SCHEMA: dict[str, Any] = {
    "type": "object",
    "description": "Gameplay role, player controller, and/or camera component.",
    "properties": {
        "role": {"type": "string", "enum": ["none", "spawn", "checkpoint", "goal", "restart", "platform"]},
        "checkpoint_order": {"type": "integer"},
        "player": {
            "type": "object",
            "properties": {
                "enabled": {"type": "boolean"},
                "mouse_sensitivity": {"type": "number"},
            },
        },
        "camera": {
            "type": "object",
            "properties": {
                "enabled": {"type": "boolean"},
                "primary": {"type": "boolean"},
                "eye_height": {"type": "number"},
                "fov_degrees": {"type": "number"},
                "near_clip": {"type": "number"},
                "far_clip": {"type": "number"},
            },
        },
    },
}

_ANIMATION_SCHEMA: dict[str, Any] = {
    "type": "object",
    "description": "Skeletal/node animation playback component.",
    "properties": {
        "asset": {"type": "string", "description": "Registered animation asset name."},
        "clip": {"type": "integer"},
        "speed": {"type": "number"},
        "autoplay": {"type": "boolean"},
        "loop": {"type": "boolean"},
    },
}

_WATER_SCHEMA: dict[str, Any] = {
    "type": "object",
    "description": "Physical water volume (owner must be root, unit scale, yaw-only rotation). Set remove:true to delete.",
    "properties": {
        "remove": {"type": "boolean"},
        "enabled": {"type": "boolean"},
        "mode": {"type": "string", "enum": ["pool", "river", "particles"]},
        "quality": {"type": "string", "enum": ["low", "medium", "high"]},
        "half_size": _VEC2_SCHEMA,
        "depth": {"type": "number"},
        "roughness": {"type": "number"},
        "ior": {"type": "number"},
        "wind": {"type": "number"},
        "ripple_scale": {"type": "number"},
        "ripple_strength": {"type": "number"},
        "absorption": _VEC3_SCHEMA,
        "scattering": _VEC3_SCHEMA,
    },
}


def build_tool_definitions() -> list[dict[str, Any]]:
    return [
        # ---- Project & Build Lifecycle ----
        {
            "name": "project_create",
            "description": "Create a new Afterlight game project directory with starter scene, C17 gameplay module, headers, and CMake build.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "directory": {"type": "string", "description": "New project directory path (must not already exist)."},
                    "name": {"type": "string", "description": "Human-readable game/project name."},
                },
                "required": ["directory", "name"],
            },
        },
        {
            "name": "project_inspect",
            "description": "Inspect an Afterlight project (.afterlight.project file or project directory): lists manifest, scenes, game C files, assets, and compiled module status.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "project_path": {"type": "string", "description": "Path to .afterlight.project file or project folder."},
                },
                "required": ["project_path"],
            },
        },
        {
            "name": "project_build_module",
            "description": "Configure and compile a project's C17 gameplay module (game/game.c -> build/game.so|dll). Returns exit code, stdout/stderr, and parsed compiler errors/warnings.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "project_dir": {"type": "string", "description": "Project root directory (or path to .afterlight.project)."},
                    "clean": {"type": "boolean", "description": "Remove existing build/ directory before configuring."},
                },
                "required": ["project_dir"],
            },
        },
        {
            "name": "project_validate",
            "description": "Validate a project manifest, startup scene, assets, and compiled C game module using `afterlight_player --check`.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "project_path": {"type": "string", "description": "Path to .afterlight.project file."},
                },
                "required": ["project_path"],
            },
        },
        {
            "name": "project_export",
            "description": "Export a playable game directory containing the Afterlight player, scenes, assets, compiled game module, and license notices.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "project_path": {"type": "string", "description": "Path to .afterlight.project file."},
                    "output_dir": {"type": "string", "description": "Destination directory (must not already exist)."},
                },
                "required": ["project_path", "output_dir"],
            },
        },
        {
            "name": "project_bundle",
            "description": "Bundle a project into a single standalone executable with embedded assets and player.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "project_path": {"type": "string", "description": "Path to .afterlight.project file."},
                    "output_path": {"type": "string", "description": "Destination standalone binary path."},
                },
                "required": ["project_path", "output_path"],
            },
        },
        # ---- Scene Lifecycle & Inspection ----
        {
            "name": "scene_create",
            "description": "Create a new Afterlight v30 .afterlight.json scene file (optionally populated with starter Ground, Player/Camera, and ACES skybox).",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "scene_path": {"type": "string", "description": "Destination .afterlight.json path."},
                    "starter": {"type": "boolean", "description": "If true (default), add starter Ground, Player+Camera, and ACES skybox."},
                },
                "required": ["scene_path"],
            },
        },
        {
            "name": "scene_inspect",
            "description": "Inspect all objects, transforms, materials, colliders, rigid bodies, gameplay/camera components, animations, water volumes, assets, lights, shadows, render settings, terrain, and foliage in a scene.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "scene_path": {"type": "string", "description": "Path to .afterlight.json scene."},
                },
                "required": ["scene_path"],
            },
        },
        {
            "name": "scene_raycast",
            "description": "Cast a ray against all scene colliders and terrain heightfield to find exact hit position, surface normal, distance, and hit object/terrain.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "scene_path": {"type": "string"},
                    "origin": _VEC3_SCHEMA,
                    "direction": _VEC3_SCHEMA,
                    "distance": {"type": "number", "description": "Max ray distance (default 1000)."},
                    "include_triggers": {"type": "boolean"},
                },
                "required": ["scene_path", "origin", "direction"],
            },
        },
        {
            "name": "scene_undo",
            "description": "Undo the most recent MCP mutation applied to a scene file (restores the previous in-memory snapshot).",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "scene_path": {"type": "string", "description": "Path to .afterlight.json scene to roll back."},
                },
                "required": ["scene_path"],
            },
        },
        # ---- Entities & Components ----
        {
            "name": "object_create",
            "description": "Create a scene entity/object with mesh (built-in 'cube', '__node' for empty transform, or any imported mesh asset), transform, material, collider, rigid body, gameplay/player/camera, animation, and/or water component.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "scene_path": {"type": "string"},
                    "name": {"type": "string", "description": "Object display name."},
                    "mesh": {"type": "string", "description": "Mesh asset name ('cube', '__node', or registered mesh asset)."},
                    "parent": {"type": "string", "description": "Optional parent object ID."},
                    "visible": {"type": "boolean"},
                    "position": _VEC3_SCHEMA,
                    "rotation": {**_VEC3_SCHEMA, "description": "Euler rotation [rx, ry, rz] in radians."},
                    "scale": _VEC3_SCHEMA,
                    "material": _MATERIAL_SCHEMA,
                    "collider": _COLLIDER_SCHEMA,
                    "body": _BODY_SCHEMA,
                    "gameplay": _GAMEPLAY_SCHEMA,
                    "animation": _ANIMATION_SCHEMA,
                    "water": _WATER_SCHEMA,
                },
                "required": ["scene_path", "name"],
            },
        },
        {
            "name": "object_update",
            "description": "Update any fields or components on an existing scene object identified by `id` or `object_name`.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "scene_path": {"type": "string"},
                    "id": {"type": "string", "description": "Object ID (string or integer)."},
                    "object_name": {"type": "string", "description": "Exact object name if id is omitted."},
                    "new_name": {"type": "string"},
                    "mesh": {"type": "string"},
                    "parent": {"type": "string", "description": "Parent object ID ('0' to unparent to root)."},
                    "visible": {"type": "boolean"},
                    "position": _VEC3_SCHEMA,
                    "rotation": _VEC3_SCHEMA,
                    "scale": _VEC3_SCHEMA,
                    "material": _MATERIAL_SCHEMA,
                    "collider": _COLLIDER_SCHEMA,
                    "body": _BODY_SCHEMA,
                    "gameplay": _GAMEPLAY_SCHEMA,
                    "animation": _ANIMATION_SCHEMA,
                    "water": _WATER_SCHEMA,
                },
                "required": ["scene_path"],
            },
        },
        {
            "name": "object_delete",
            "description": "Delete a scene object by `id` or `object_name` (recursively deletes child hierarchy by default).",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "scene_path": {"type": "string"},
                    "id": {"type": "string"},
                    "object_name": {"type": "string"},
                    "recursive": {"type": "boolean", "description": "Delete descendant objects too (default true)."},
                },
                "required": ["scene_path"],
            },
        },
        {
            "name": "object_duplicate",
            "description": "Duplicate an existing scene object (all components copied) with optional `new_name` and position `offset`.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "scene_path": {"type": "string"},
                    "id": {"type": "string"},
                    "object_name": {"type": "string"},
                    "new_name": {"type": "string"},
                    "offset": _VEC3_SCHEMA,
                },
                "required": ["scene_path"],
            },
        },
        {
            "name": "collider_part_add",
            "description": "Add a box/sphere/capsule child shape to an object with a `compound` collider.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "scene_path": {"type": "string"},
                    "id": {"type": "string"},
                    "object_name": {"type": "string"},
                    "part_name": {"type": "string"},
                    "type": {"type": "string", "enum": ["box", "sphere", "capsule"]},
                    "position": _VEC3_SCHEMA,
                    "rotation": _VEC3_SCHEMA,
                    "half_extents": _VEC3_SCHEMA,
                    "radius": {"type": "number"},
                    "density": {"type": "number"},
                },
                "required": ["scene_path"],
            },
        },
        {
            "name": "collider_part_remove",
            "description": "Remove a compound collider child part by `part_id`.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "scene_path": {"type": "string"},
                    "part_id": {"type": "string"},
                },
                "required": ["scene_path", "part_id"],
            },
        },
        {
            "name": "joint_add",
            "description": "Connect two rigid bodies with a physics `hinge` or `distance` joint (anchors, axes, limits, motor).",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "scene_path": {"type": "string"},
                    "name": {"type": "string"},
                    "type": {"type": "string", "enum": ["hinge", "distance"]},
                    "object_a": {"type": "string", "description": "Object ID A."},
                    "object_b": {"type": "string", "description": "Object ID B."},
                    "anchor_a": _VEC3_SCHEMA,
                    "anchor_b": _VEC3_SCHEMA,
                    "distance": {"type": "number"},
                    "axis_a": _VEC3_SCHEMA,
                    "axis_b": _VEC3_SCHEMA,
                    "collide_connected": {"type": "boolean"},
                    "limits_enabled": {"type": "boolean"},
                    "lower_limit": {"type": "number"},
                    "upper_limit": {"type": "number"},
                    "motor_enabled": {"type": "boolean"},
                    "motor_speed": {"type": "number"},
                    "max_motor_torque": {"type": "number"},
                },
                "required": ["scene_path", "object_a", "object_b"],
            },
        },
        {
            "name": "joint_remove",
            "description": "Remove a physics joint by `joint_id`.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "scene_path": {"type": "string"},
                    "joint_id": {"type": "string"},
                },
                "required": ["scene_path", "joint_id"],
            },
        },
        # ---- Assets, Procedural Geometry, Textures & Shaders ----
        {
            "name": "asset_import",
            "description": "Validate and import an asset into a scene. Use kind='model' for full glTF/GLB hierarchy + PBR materials + skins + animations; or 'mesh', 'texture', 'normal_map', 'metallic_roughness', 'environment' (.hdr), 'animation', 'audio' (.wav).",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "scene_path": {"type": "string"},
                    "name": {"type": "string", "description": "Asset name (or root prefix for kind='model')."},
                    "kind": {
                        "type": "string",
                        "enum": [
                            "model",
                            "mesh",
                            "texture",
                            "normal_map",
                            "metallic_roughness",
                            "environment",
                            "animation",
                            "audio",
                        ],
                    },
                    "asset_path": {"type": "string", "description": "Path to asset file (relative to scene or absolute)."},
                    "position": {**_VEC3_SCHEMA, "description": "Optional root position (for kind='model')."},
                    "rotation": {**_VEC3_SCHEMA, "description": "Optional root Euler rotation in radians (for kind='model')."},
                    "scale": {**_VEC3_SCHEMA, "description": "Optional root scale (for kind='model')."},
                },
                "required": ["scene_path", "name", "kind", "asset_path"],
            },
        },
        {
            "name": "asset_remove",
            "description": "Unregister a named asset from a scene manifest.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "scene_path": {"type": "string"},
                    "name": {"type": "string"},
                },
                "required": ["scene_path", "name"],
            },
        },
        {
            "name": "generate_mesh",
            "description": "Procedurally generate a clean Wavefront .obj mesh with normals and UVs (box, plane, sphere, cylinder, cone, ramp, stairs, arch, rock, tree), write to disk, and optionally import directly into a scene.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "shape": {
                        "type": "string",
                        "enum": ["box", "plane", "sphere", "cylinder", "cone", "ramp", "stairs", "arch", "rock", "tree"],
                    },
                    "output_path": {"type": "string", "description": "Destination .obj file path."},
                    "params": {
                        "type": "object",
                        "description": "Shape parameters (e.g., radius, segments, rings, size, width, height, depth, steps, thickness, detail, roughness, seed, trunk_height, canopy_radius, tiers).",
                    },
                    "scene_path": {"type": "string", "description": "Optional scene to immediately register the mesh into."},
                    "asset_name": {"type": "string", "description": "Asset name when registering into scene_path (defaults to file stem)."},
                },
                "required": ["shape", "output_path"],
            },
        },
        {
            "name": "generate_texture",
            "description": "Procedurally generate a tileable RGBA8 PNG texture or tangent-space normal map (checker, grid, brick, wood, grass, stone, sand, metal, noise, gradient, normal_noise, normal_brick, normal_grid), and optionally register into a scene.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "pattern": {
                        "type": "string",
                        "enum": [
                            "checker",
                            "grid",
                            "brick",
                            "wood",
                            "grass",
                            "stone",
                            "sand",
                            "metal",
                            "noise",
                            "gradient",
                            "normal_noise",
                            "normal_brick",
                            "normal_grid",
                        ],
                    },
                    "output_path": {"type": "string", "description": "Destination .png file path."},
                    "width": {"type": "integer", "description": "Texture width in pixels (16..1024, default 256)."},
                    "height": {"type": "integer", "description": "Texture height in pixels (16..1024, default 256)."},
                    "params": {
                        "type": "object",
                        "description": "Pattern parameters: color_a ([r,g,b]), color_b ([r,g,b]), scale (int), seed (int), strength (float for normal maps).",
                    },
                    "scene_path": {"type": "string", "description": "Optional scene to register the texture into."},
                    "asset_name": {"type": "string", "description": "Asset name when registering into scene_path."},
                    "asset_kind": {
                        "type": "string",
                        "enum": ["texture", "normal_map", "metallic_roughness"],
                        "description": "Defaults to normal_map for normal_* patterns, else texture.",
                    },
                },
                "required": ["pattern", "output_path"],
            },
        },
        {
            "name": "compile_shader",
            "description": "Compile a GLSL 450 fragment shader (.frag) to SPIR-V (.spv) using glslangValidator (works on both Vulkan and native Metal via Afterlight's SPIR-V->MSL runtime).",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "source_path": {"type": "string", "description": "Path to existing .frag GLSL file (or provide source_code)."},
                    "source_code": {"type": "string", "description": "Inline GLSL fragment shader source (written to source_path if provided, else temp file)."},
                    "output_path": {"type": "string", "description": "Destination .spv file path."},
                },
                "required": ["output_path"],
            },
        },
        # ---- Terrain & Foliage ----
        {
            "name": "terrain_create",
            "description": "Create a chunked heightfield terrain (af_landscape) in a scene, with optional procedural hills, up to 4 PBR splat layers, and slope-based auto-painting.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "scene_path": {"type": "string"},
                    "chunks_x": {"type": "integer", "description": "Chunk count along X (1..16, each chunk = 16 quads)."},
                    "chunks_z": {"type": "integer", "description": "Chunk count along Z (1..16)."},
                    "vertex_spacing": {"type": "number", "description": "Meters between terrain vertices (default 1.0)."},
                    "origin": _VEC3_SCHEMA,
                    "procedural_hills": {
                        "type": "object",
                        "properties": {
                            "scale": {"type": "number"},
                            "amplitude": {"type": "number"},
                            "seed": {"type": "integer"},
                        },
                    },
                    "layers": {
                        "type": "array",
                        "maxItems": 4,
                        "items": {
                            "type": "object",
                            "properties": {
                                "name": {"type": "string"},
                                "diffuse_texture": {"type": "string"},
                                "normal_texture": {"type": "string"},
                                "color": _COLOR_SCHEMA,
                                "roughness": {"type": "number"},
                                "metalness": {"type": "number"},
                                "uv_scale": {"type": "number"},
                            },
                        },
                    },
                    "auto_paint_slopes": {"type": "boolean"},
                    "rock_layer": {"type": "integer"},
                    "min_slope_rad": {"type": "number"},
                    "sand_layer": {"type": "integer"},
                    "sand_max_height": {"type": "number"},
                },
                "required": ["scene_path"],
            },
        },
        {
            "name": "terrain_sculpt",
            "description": "Sculpt or paint an existing terrain: raise, lower, smooth, flatten, paint_layer, auto_paint_slopes, procedural_hills, or set_layer.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "scene_path": {"type": "string"},
                    "operation": {
                        "type": "string",
                        "enum": [
                            "raise",
                            "lower",
                            "smooth",
                            "flatten",
                            "paint_layer",
                            "auto_paint_slopes",
                            "procedural_hills",
                            "set_layer",
                        ],
                    },
                    "center": _VEC3_SCHEMA,
                    "radius": {"type": "number"},
                    "falloff": {"type": "string", "enum": ["smooth", "linear", "spherical", "flat"]},
                    "delta": {"type": "number", "description": "Height delta in meters for raise/lower."},
                    "strength": {"type": "number", "description": "Brush strength (0..1) for smooth/flatten/paint_layer."},
                    "target_height": {"type": "number", "description": "Target Y height for flatten."},
                    "layer": {"type": "integer", "description": "Terrain layer index (0..3) for paint_layer or set_layer."},
                    "name": {"type": "string"},
                    "diffuse_texture": {"type": "string"},
                    "normal_texture": {"type": "string"},
                    "color": _COLOR_SCHEMA,
                    "roughness": {"type": "number"},
                    "metalness": {"type": "number"},
                    "uv_scale": {"type": "number"},
                    "rock_layer": {"type": "integer"},
                    "min_slope_rad": {"type": "number"},
                    "sand_layer": {"type": "integer"},
                    "sand_max_height": {"type": "number"},
                    "scale": {"type": "number"},
                    "amplitude": {"type": "number"},
                    "seed": {"type": "integer"},
                },
                "required": ["scene_path", "operation"],
            },
        },
        {
            "name": "terrain_query",
            "description": "Sample terrain height, surface normal, and 4-layer splat weights at one (x, z) point or a batch of points [[x, z], ...] to accurately ground objects.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "scene_path": {"type": "string"},
                    "x": {"type": "number"},
                    "z": {"type": "number"},
                    "points": {
                        "type": "array",
                        "items": {"type": "array", "items": {"type": "number"}, "minItems": 2, "maxItems": 2},
                        "description": "Optional list of [x, z] world coordinates to sample in batch.",
                    },
                },
                "required": ["scene_path"],
            },
        },
        {
            "name": "foliage_add_layer",
            "description": "Add a GPU-instanced foliage layer to the terrain with mesh, PBR material, and procedural scatter rules (slope/height range, density, spacing, scale jitter, alignment).",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "scene_path": {"type": "string"},
                    "name": {"type": "string"},
                    "mesh": {"type": "string", "description": "Registered mesh asset name (or 'cube')."},
                    "visible": {"type": "boolean"},
                    "cast_shadows": {"type": "boolean"},
                    "max_distance": {"type": "number"},
                    "material": _MATERIAL_SCHEMA,
                    "rules": {
                        "type": "object",
                        "properties": {
                            "random_yaw": {"type": "boolean"},
                            "random_pitch_roll": {"type": "boolean"},
                            "uniform_scale": {"type": "boolean"},
                            "collision_avoidance": {"type": "boolean"},
                            "max_tilt": {"type": "number"},
                            "scale_min": _VEC3_SCHEMA,
                            "scale_max": _VEC3_SCHEMA,
                            "slope_min": {"type": "number"},
                            "slope_max": {"type": "number"},
                            "height_min": {"type": "number"},
                            "height_max": {"type": "number"},
                            "density": {"type": "number"},
                            "min_spacing": {"type": "number"},
                            "align_to_normal": {"type": "number"},
                            "offset_y": {"type": "number"},
                            "paint_mask_layer": {"type": "integer", "description": "-1 for everywhere, or 0..3 for terrain splat layer."},
                        },
                    },
                },
                "required": ["scene_path", "name", "mesh"],
            },
        },
        {
            "name": "foliage_populate",
            "description": "Populate, brush-paint, brush-erase, or clear instanced foliage on a terrain foliage layer.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "scene_path": {"type": "string"},
                    "mode": {"type": "string", "enum": ["populate", "paint", "erase", "clear"]},
                    "layer_index": {"type": "integer", "description": "Foliage layer index (use -1 with erase/clear for all layers)."},
                    "seed": {"type": "integer"},
                    "center": _VEC3_SCHEMA,
                    "radius": {"type": "number"},
                    "strength": {"type": "number"},
                },
                "required": ["scene_path"],
            },
        },
        {
            "name": "foliage_remove_layer",
            "description": "Remove a terrain foliage layer by index.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "scene_path": {"type": "string"},
                    "layer_index": {"type": "integer"},
                },
                "required": ["scene_path", "layer_index"],
            },
        },
        # ---- Lighting, Shadows, Render/Post, Wind & Camera ----
        {
            "name": "lighting_set_sun",
            "description": "Configure the scene's directional sun light (direction vector, RGB color, intensity, ambient, angular_radius for soft RT shadows).",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "scene_path": {"type": "string"},
                    "direction": _VEC3_SCHEMA,
                    "color": _VEC3_SCHEMA,
                    "intensity": {"type": "number"},
                    "ambient": {"type": "number"},
                    "angular_radius": {"type": "number"},
                },
                "required": ["scene_path"],
            },
        },
        {
            "name": "lighting_add_local",
            "description": "Add a point or spot local light (up to 128 tiled lights per scene).",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "scene_path": {"type": "string"},
                    "name": {"type": "string"},
                    "type": {"type": "string", "enum": ["point", "spot"]},
                    "enabled": {"type": "boolean"},
                    "position": _VEC3_SCHEMA,
                    "color": _VEC3_SCHEMA,
                    "intensity": {"type": "number"},
                    "range": {"type": "number"},
                    "direction": _VEC3_SCHEMA,
                    "cast_shadows": {"type": "boolean"},
                    "shadow_bias": {"type": "number"},
                    "inner_angle": {"type": "number"},
                    "outer_angle": {"type": "number"},
                },
                "required": ["scene_path", "position"],
            },
        },
        {
            "name": "lighting_update_local",
            "description": "Update an existing local light by zero-based `index`.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "scene_path": {"type": "string"},
                    "index": {"type": "integer"},
                    "name": {"type": "string"},
                    "type": {"type": "string", "enum": ["point", "spot"]},
                    "enabled": {"type": "boolean"},
                    "position": _VEC3_SCHEMA,
                    "color": _VEC3_SCHEMA,
                    "intensity": {"type": "number"},
                    "range": {"type": "number"},
                    "direction": _VEC3_SCHEMA,
                    "cast_shadows": {"type": "boolean"},
                    "shadow_bias": {"type": "number"},
                    "inner_angle": {"type": "number"},
                    "outer_angle": {"type": "number"},
                },
                "required": ["scene_path", "index"],
            },
        },
        {
            "name": "lighting_remove_local",
            "description": "Remove a local light by zero-based `index`.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "scene_path": {"type": "string"},
                    "index": {"type": "integer"},
                },
                "required": ["scene_path", "index"],
            },
        },
        {
            "name": "lighting_set_shadows",
            "description": "Configure directional shadow maps (enabled, cascaded/single mode, resolution 512/1024/2048/4096, distance, biases, PCF filter radius, cascade split).",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "scene_path": {"type": "string"},
                    "enabled": {"type": "boolean"},
                    "mode": {"type": "string", "enum": ["cascaded", "single"]},
                    "resolution": {"type": "integer", "enum": [512, 1024, 2048, 4096]},
                    "distance": {"type": "number"},
                    "constant_bias": {"type": "number"},
                    "slope_bias": {"type": "number"},
                    "normal_bias": {"type": "number"},
                    "filter_radius": {"type": "integer"},
                    "split_lambda": {"type": "number"},
                    "blend_fraction": {"type": "number"},
                    "debug_cascades": {"type": "boolean"},
                },
                "required": ["scene_path"],
            },
        },
        {
            "name": "render_set_settings",
            "description": "Configure post-processing, anti-aliasing, skybox/IBL environment, and hardware ray tracing settings (ray-traced shadows, water reflections, material effects, RTGI).",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "scene_path": {"type": "string"},
                    "anti_aliasing": {"type": "string", "enum": ["off", "fxaa", "smaa", "taa"]},
                    "aa_sharpness": {"type": "number"},
                    "taa_history_weight": {"type": "number"},
                    "exposure": {"type": "number"},
                    "tone_mapping": {"type": "boolean"},
                    "tone_mapper": {"type": "string", "enum": ["off", "reinhard", "aces", "agx"]},
                    "auto_exposure": {"type": "boolean"},
                    "saturation": {"type": "number"},
                    "contrast": {"type": "number"},
                    "color_filter": _VEC3_SCHEMA,
                    "bloom": {"type": "boolean"},
                    "bloom_strength": {"type": "number"},
                    "bloom_threshold": {"type": "number"},
                    "bloom_radius": {"type": "number"},
                    "depth_of_field": {"type": "boolean"},
                    "dof_focus_distance": {"type": "number"},
                    "dof_focus_range": {"type": "number"},
                    "dof_blur_radius": {"type": "number"},
                    "motion_blur": {"type": "boolean"},
                    "motion_blur_strength": {"type": "number"},
                    "motion_blur_samples": {"type": "integer"},
                    "vignette": {"type": "boolean"},
                    "vignette_intensity": {"type": "number"},
                    "vignette_smoothness": {"type": "number"},
                    "chromatic_aberration": {"type": "boolean"},
                    "chromatic_aberration_strength": {"type": "number"},
                    "film_grain": {"type": "boolean"},
                    "film_grain_intensity": {"type": "number"},
                    "underwater": {"type": "boolean"},
                    "refraction": {"type": "boolean"},
                    "refraction_strength": {"type": "number"},
                    "environment_enabled": {"type": "boolean"},
                    "environment_profile": {"type": "integer", "description": "Built-in sky/IBL profile index (0..3)."},
                    "environment_asset": {"type": "string", "description": "Registered .hdr environment asset name."},
                    "skybox": {"type": "boolean"},
                    "skybox_intensity": {"type": "number"},
                    "environment_intensity": {"type": "number"},
                    "environment_rotation": {"type": "number"},
                    "local_shadow_resolution": {"type": "integer"},
                    "ray_traced_shadows": {"type": "boolean"},
                    "ray_traced_water_reflections": {"type": "boolean"},
                    "ray_traced_material_effects": {"type": "boolean"},
                    "ray_traced_gi": {"type": "boolean"},
                    "rtgi_intensity": {"type": "number"},
                    "rtgi_ray_distance": {"type": "number"},
                    "rtgi_samples": {"type": "integer"},
                    "rtgi_bounces": {"type": "integer"},
                    "rtgi_emissive": {"type": "boolean"},
                },
                "required": ["scene_path"],
            },
        },
        {
            "name": "wind_set",
            "description": "Configure global scene wind for foliage animation, water ripples, and aerodynamic physics forces.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "scene_path": {"type": "string"},
                    "enabled": {"type": "boolean"},
                    "direction": {**_VEC3_SCHEMA, "description": "Horizontal wind direction [x, 0, z] (auto-normalized)."},
                    "speed": {"type": "number", "description": "Wind speed in m/s (0..60)."},
                    "gust_strength": {"type": "number", "description": "Relative gust factor (0..1)."},
                    "gust_frequency": {"type": "number", "description": "Gust frequency in Hz (0..2)."},
                    "air_density": {"type": "number", "description": "Air density in kg/m^3 (0.01..10, default 1.225)."},
                },
                "required": ["scene_path"],
            },
        },
        {
            "name": "camera_configure",
            "description": "Configure (or create) the primary game camera or a specific camera object in a scene, supporting easy `position` + `look_at` aiming.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "scene_path": {"type": "string"},
                    "id": {"type": "string", "description": "Optional specific camera object ID."},
                    "object_name": {"type": "string", "description": "Optional existing camera object name."},
                    "name": {"type": "string", "description": "Name if creating a new camera object."},
                    "primary": {"type": "boolean"},
                    "position": _VEC3_SCHEMA,
                    "look_at": {**_VEC3_SCHEMA, "description": "World point to aim the camera at (computes rotation automatically)."},
                    "rotation": {**_VEC3_SCHEMA, "description": "Explicit Euler rotation in radians (used if look_at omitted)."},
                    "eye_height": {"type": "number"},
                    "fov_degrees": {"type": "number"},
                    "near_clip": {"type": "number"},
                    "far_clip": {"type": "number"},
                },
                "required": ["scene_path"],
            },
        },
        # ---- Run & Visually Verify ----
        {
            "name": "run_game",
            "description": "Run a project for N frames using `afterlight_player` with optional validation, returning exit code, stdout, stderr, and extracted errors/warnings so agents can inspect runtime failures or gameplay output.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "project_path": {"type": "string", "description": "Path to .afterlight.project file."},
                    "frames": {"type": "integer", "description": "Number of frames to simulate/render before exiting (default 30)."},
                    "validation": {"type": "boolean", "description": "Enable GPU/API validation layers (default true)."},
                    "backend": {"type": "string", "enum": ["auto", "metal", "vulkan"]},
                    "dummy_audio": {"type": "boolean", "description": "Use SDL_AUDIO_DRIVER=dummy (default true for headless automation)."},
                    "timeout_seconds": {"type": "number", "description": "Process timeout in seconds (default 60)."},
                },
                "required": ["project_path"],
            },
        },
        {
            "name": "capture_screenshot",
            "description": "Render a scene or running game project on the real GPU (Metal 3 or Vulkan), save a full-resolution PNG to disk, and return BOTH structured render/error/ray-tracing stats AND an inline MCP image/png so the agent can visually inspect what it created.",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "scene_path": {"type": "string", "description": "Path to .afterlight.json scene (optional if project_path is provided)."},
                    "project_path": {"type": "string", "description": "Path to .afterlight.project file (loads startup scene and optionally runs C game module)."},
                    "run_game": {"type": "boolean", "description": "When project_path is set, execute the compiled C game module during capture (default true for project_path, false for scene_path)."},
                    "output_path": {"type": "string", "description": "Optional path to save the full-resolution PNG (auto-generated if omitted)."},
                    "width": {"type": "integer", "description": "Logical window width (default 800)."},
                    "height": {"type": "integer", "description": "Logical window height (default 600)."},
                    "frames": {"type": "integer", "description": "Frames to render before capturing (default 12; use >=20 for TAA/RTGI convergence)."},
                    "backend": {"type": "string", "enum": ["auto", "metal", "vulkan"]},
                    "validation": {"type": "boolean", "description": "Enable GPU validation (default true)."},
                    "debug_view": {"type": "string", "enum": ["shaded", "depth", "motion", "history", "rejection"]},
                    "metalfx": {"type": "boolean", "description": "Enable native MetalFX upscaling when available."},
                    "upscaler_quality": {"type": "integer", "description": "Upscaler quality index when metalfx is true."},
                    "clear_color": _COLOR_SCHEMA,
                    "camera": {
                        "type": "object",
                        "description": "Optional camera override (leaves scene file unmodified).",
                        "properties": {
                            "position": _VEC3_SCHEMA,
                            "look_at": _VEC3_SCHEMA,
                            "yaw": {"type": "number"},
                            "pitch": {"type": "number"},
                            "fov_degrees": {"type": "number"},
                            "near_clip": {"type": "number"},
                            "far_clip": {"type": "number"},
                        },
                    },
                    "max_inline_width": {"type": "integer", "description": "Max width/height for the inline base64 MCP image (default 1024; full-res PNG is always saved to output_path)."},
                    "return_image": {"type": "boolean", "description": "Include inline base64 image/png in MCP response (default true)."},
                },
            },
        },
    ]


# ---- MCP Resources & Prompts ----

RESOURCES: list[tuple[str, str, str, str]] = [
    ("afterlight://docs/guide", "Afterlight User Guide", "text/markdown", "docs/guide.md"),
    ("afterlight://docs/game-code", "C17 Gameplay Module Guide", "text/markdown", "docs/game-code.md"),
    ("afterlight://docs/rendering", "Rendering, PBR, Lighting & Ray Tracing", "text/markdown", "docs/rendering.md"),
    ("afterlight://docs/scene-format", "Scene Format Reference (v30)", "text/markdown", "docs/scene-format.md"),
    ("afterlight://docs/assets", "Assets, Meshes, Textures & glTF", "text/markdown", "docs/assets.md"),
    ("afterlight://docs/projects", "Project Layout & Exporting", "text/markdown", "docs/projects.md"),
    ("afterlight://docs/mcp", "Afterlight MCP Server Guide", "text/markdown", "docs/mcp.md"),
    ("afterlight://headers/game_module.h", "Public Header: game_module.h", "text/x-c", "engine/include/afterlight/game_module.h"),
    ("afterlight://headers/afterlight.h", "Public Header: afterlight.h", "text/x-c", "engine/include/afterlight/afterlight.h"),
    ("afterlight://headers/scene.h", "Public Header: scene.h", "text/x-c", "engine/include/afterlight/scene.h"),
    ("afterlight://headers/material.h", "Public Header: material.h", "text/x-c", "engine/include/afterlight/material.h"),
    ("afterlight://headers/landscape.h", "Public Header: landscape.h", "text/x-c", "engine/include/afterlight/landscape.h"),
    ("afterlight://headers/water.h", "Public Header: water.h", "text/x-c", "engine/include/afterlight/water.h"),
    ("afterlight://headers/physics.h", "Public Header: physics.h", "text/x-c", "engine/include/afterlight/physics.h"),
    ("afterlight://examples/custom_material.frag", "Example Custom Material GLSL 450 Fragment Shader", "text/plain", "tests/render/vulkan/custom_material.frag"),
]

PROMPTS: list[dict[str, Any]] = [
    {
        "name": "create_game",
        "description": "End-to-end agent workflow for building a playable Afterlight game with terrain, foliage, lighting, C gameplay code, and visual screenshot verification.",
        "arguments": [
            {"name": "project_dir", "description": "Target directory for the new project.", "required": True},
            {"name": "concept", "description": "Game idea, visual style, and core mechanics.", "required": True},
        ],
    },
    {
        "name": "visual_iterate",
        "description": "Inspect an existing Afterlight scene from multiple angles, identify visual or gameplay defects, apply fixes, and re-capture screenshots to verify.",
        "arguments": [
            {"name": "scene_path", "description": "Path to the .afterlight.json scene to polish.", "required": True},
            {"name": "goal", "description": "Visual or design improvements to aim for.", "required": False},
        ],
    },
]


# ---- Server Implementation ----


class AfterlightMCPServer:
    def __init__(
        self,
        repo_root: pathlib.Path,
        build_dir: pathlib.Path | None = None,
        agent_bin: pathlib.Path | None = None,
        player_bin: pathlib.Path | None = None,
    ) -> None:
        self.repo_root = repo_root.resolve()
        self.version = _read_engine_version(self.repo_root)
        self.build_dir = build_dir.resolve() if build_dir else self._discover_build_dir()
        self.agent_bin = agent_bin.resolve() if agent_bin else self._discover_agent_bin()
        self.player_bin = player_bin.resolve() if player_bin else self._discover_player_bin()
        self.player_package = self._discover_player_package()
        self.tools = build_tool_definitions()
        self.undo_stack: dict[str, list[bytes]] = {}
        self.capture_dir = pathlib.Path(tempfile.gettempdir()) / "afterlight_mcp_captures"
        self.transport_mode = "ndjson"

    def _exe_name(self, base: str) -> str:
        return f"{base}.exe" if sys.platform == "win32" else base

    def _candidate_build_dirs(self) -> list[pathlib.Path]:
        cands: list[pathlib.Path] = []
        env_bd = os.environ.get("AFTERLIGHT_BUILD_DIR")
        if env_bd:
            cands.append(pathlib.Path(env_bd))
        for rel in ("build/debug", "build/release", "build", "build/metal-release", "build/vulkan-release"):
            cands.append(self.repo_root / rel)
        return cands

    def _discover_build_dir(self) -> pathlib.Path | None:
        exe = self._exe_name("afterlight_agent")
        for d in self._candidate_build_dirs():
            if (d / exe).is_file():
                return d.resolve()
        for d in self._candidate_build_dirs():
            if d.is_dir():
                return d.resolve()
        return None

    def _discover_agent_bin(self) -> pathlib.Path | None:
        env = os.environ.get("AFTERLIGHT_AGENT_BIN")
        if env and pathlib.Path(env).is_file():
            return pathlib.Path(env).resolve()
        exe = self._exe_name("afterlight_agent")
        if self.build_dir and (self.build_dir / exe).is_file():
            return (self.build_dir / exe).resolve()
        for d in self._candidate_build_dirs():
            p = d / exe
            if p.is_file():
                return p.resolve()
        return None

    def _discover_player_bin(self) -> pathlib.Path | None:
        env = os.environ.get("AFTERLIGHT_PLAYER_BIN")
        if env and pathlib.Path(env).is_file():
            return pathlib.Path(env).resolve()
        dirs = [self.build_dir] if self.build_dir else []
        dirs.extend(self._candidate_build_dirs())
        for d in dirs:
            if not d:
                continue
            if sys.platform == "darwin":
                app_bin = d / "afterlight_player.app/Contents/MacOS/afterlight_player"
                if app_bin.is_file():
                    return app_bin.resolve()
            raw_bin = d / self._exe_name("afterlight_player")
            if raw_bin.is_file():
                return raw_bin.resolve()
        return None

    def _discover_player_package(self) -> pathlib.Path | None:
        if not self.player_bin:
            return None
        if sys.platform == "darwin":
            for parent in self.player_bin.parents:
                if parent.suffix == ".app":
                    return parent
        return self.player_bin

    def _require_agent(self) -> pathlib.Path:
        if not self.agent_bin or not self.agent_bin.is_file():
            self.agent_bin = self._discover_agent_bin()
        if not self.agent_bin or not self.agent_bin.is_file():
            raise RuntimeError(
                "afterlight_agent executable not found. Build it with: "
                "cmake --build --preset debug --target afterlight_agent"
            )
        return self.agent_bin

    def _require_player(self) -> pathlib.Path:
        if not self.player_bin or not self.player_bin.is_file():
            self.player_bin = self._discover_player_bin()
            self.player_package = self._discover_player_package()
        if not self.player_bin or not self.player_bin.is_file():
            raise RuntimeError(
                "afterlight_player executable not found. Build it with: "
                "cmake --build --preset debug --target afterlight_player"
            )
        return self.player_bin

    def _snapshot_scene(self, scene_path: str | None) -> str | None:
        if not scene_path:
            return None
        p = pathlib.Path(scene_path).resolve()
        if not p.is_file():
            return None
        key = str(p)
        stack = self.undo_stack.setdefault(key, [])
        stack.append(p.read_bytes())
        if len(stack) > 32:
            del stack[:-32]
        return key

    def _discard_last_snapshot(self, key: str | None) -> None:
        if key and key in self.undo_stack and self.undo_stack[key]:
            self.undo_stack[key].pop()

    def _run_agent(
        self,
        command: str,
        args: dict[str, Any],
        timeout: float = 60.0,
        env_extra: dict[str, str] | None = None,
    ) -> tuple[dict[str, Any], str, str]:
        agent = self._require_agent()
        env = os.environ.copy()
        env.setdefault("SDL_AUDIO_DRIVER", "dummy")
        if env_extra:
            env.update(env_extra)

        with tempfile.TemporaryDirectory(prefix="af_mcp_req_") as td:
            req_file = pathlib.Path(td) / "req.json"
            resp_file = pathlib.Path(td) / "resp.json"
            req_file.write_text(json.dumps({"command": command, "args": args}), encoding="utf-8")
            proc = subprocess.run(
                [str(agent), "--request", str(req_file), "--response", str(resp_file)],
                capture_output=True,
                text=True,
                timeout=timeout,
                env=env,
            )
            if resp_file.is_file():
                payload_text = resp_file.read_text(encoding="utf-8", errors="replace").strip()
            else:
                payload_text = proc.stdout.strip()
            try:
                resp = json.loads(payload_text) if payload_text else {}
            except json.JSONDecodeError as exc:
                raise RuntimeError(
                    f"afterlight_agent exited with code {proc.returncode} and non-JSON response: "
                    f"{exc}. stderr: {proc.stderr.strip()}"
                ) from exc
            return resp, proc.stdout, proc.stderr

    # ---- High-level Tool Handlers ----

    def _tool_project_create(self, args: dict[str, Any]) -> dict[str, Any]:
        directory = str(args["directory"])
        name = str(args["name"])
        script = self.repo_root / "scripts/afterlight.py"
        proc = subprocess.run(
            [sys.executable, str(script), "new", directory, "--name", name],
            capture_output=True,
            text=True,
            timeout=60,
        )
        if proc.returncode != 0:
            raise RuntimeError((proc.stderr or proc.stdout or "Project creation failed.").strip())
        proj_dir = pathlib.Path(directory).resolve()
        return {
            "project_dir": str(proj_dir),
            "project_file": str(proj_dir / "game.afterlight.project"),
            "startup_scene": str(proj_dir / "scenes/start.afterlight.json"),
            "game_source": str(proj_dir / "game/game.c"),
            "assets_dir": str(proj_dir / "assets"),
            "output": proc.stdout.strip(),
        }

    def _resolve_project_file(self, raw_path: str) -> pathlib.Path:
        p = pathlib.Path(raw_path).resolve()
        if p.is_dir():
            cand = p / "game.afterlight.project"
            if cand.is_file():
                return cand
            matches = sorted(p.glob("*.afterlight.project"))
            if matches:
                return matches[0]
            raise FileNotFoundError(f"No .afterlight.project file found in {p}")
        if not p.is_file():
            raise FileNotFoundError(f"Project file not found: {p}")
        return p

    def _tool_project_inspect(self, args: dict[str, Any]) -> dict[str, Any]:
        proj_file = self._resolve_project_file(str(args["project_path"]))
        proj_dir = proj_file.parent
        manifest = json.loads(proj_file.read_text(encoding="utf-8"))
        startup_scene = (proj_dir / manifest.get("startup_scene", "")).resolve()
        game_module = (proj_dir / manifest.get("game_module", "")).resolve()

        def list_rel(sub: str) -> list[str]:
            d = proj_dir / sub
            if not d.is_dir():
                return []
            return sorted(str(f.relative_to(proj_dir)).replace("\\", "/") for f in d.rglob("*") if f.is_file())

        return {
            "project_file": str(proj_file),
            "project_dir": str(proj_dir),
            "manifest": manifest,
            "startup_scene": str(startup_scene),
            "startup_scene_exists": startup_scene.is_file(),
            "game_module": str(game_module),
            "game_module_built": game_module.is_file(),
            "scenes": list_rel("scenes"),
            "game_sources": list_rel("game"),
            "assets": list_rel("assets"),
        }

    def _tool_project_build_module(self, args: dict[str, Any]) -> dict[str, Any]:
        raw = pathlib.Path(str(args["project_dir"])).resolve()
        proj_dir = raw.parent if raw.is_file() else raw
        if not (proj_dir / "CMakeLists.txt").is_file():
            raise FileNotFoundError(f"CMakeLists.txt not found in {proj_dir}")
        build_dir = proj_dir / "build"
        if args.get("clean") and build_dir.exists():
            shutil.rmtree(build_dir)

        cmake = shutil.which("cmake")
        if not cmake:
            raise RuntimeError("cmake executable not found in PATH.")

        cfg_out = ""
        cfg_err = ""
        if not (build_dir / "CMakeCache.txt").is_file():
            cmd = [cmake, "-S", str(proj_dir), "-B", str(build_dir)]
            if shutil.which("ninja"):
                cmd.extend(["-G", "Ninja"])
            cfg = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
            cfg_out, cfg_err = cfg.stdout, cfg.stderr
            if cfg.returncode != 0:
                return {
                    "ok": False,
                    "stage": "configure",
                    "exit_code": cfg.returncode,
                    "stdout": cfg_out,
                    "stderr": cfg_err,
                    "diagnostics": _parse_compiler_diagnostics(cfg_out + "\n" + cfg_err),
                }

        bld = subprocess.run([cmake, "--build", str(build_dir)], capture_output=True, text=True, timeout=180)
        combined = bld.stdout + "\n" + bld.stderr
        mod_name = "game.dll" if sys.platform == "win32" else "game.so"
        mod_path = build_dir / mod_name
        return {
            "ok": bld.returncode == 0,
            "stage": "build",
            "exit_code": bld.returncode,
            "module_path": str(mod_path),
            "module_exists": mod_path.is_file(),
            "diagnostics": _parse_compiler_diagnostics(combined),
            "stdout": (cfg_out + "\n" + bld.stdout).strip(),
            "stderr": (cfg_err + "\n" + bld.stderr).strip(),
        }

    def _tool_project_validate(self, args: dict[str, Any]) -> dict[str, Any]:
        player = self._require_player()
        proj_file = self._resolve_project_file(str(args["project_path"]))
        env = os.environ.copy()
        env["SDL_AUDIO_DRIVER"] = "dummy"
        proc = subprocess.run(
            [str(player), "--project", str(proj_file), "--check"],
            capture_output=True,
            text=True,
            timeout=60,
            env=env,
        )
        return {
            "valid": proc.returncode == 0,
            "exit_code": proc.returncode,
            "project_path": str(proj_file),
            "stdout": proc.stdout.strip(),
            "stderr": proc.stderr.strip(),
            "issues": _extract_engine_issues(proc.stdout + "\n" + proc.stderr),
        }

    def _tool_project_export(self, args: dict[str, Any]) -> dict[str, Any]:
        self._require_player()
        assert self.player_package is not None
        proj_file = self._resolve_project_file(str(args["project_path"]))
        out_dir = pathlib.Path(str(args["output_dir"])).resolve()
        script = self.repo_root / "scripts/afterlight.py"
        proc = subprocess.run(
            [
                sys.executable,
                str(script),
                "export",
                str(proj_file),
                "--output",
                str(out_dir),
                "--player",
                str(self.player_package),
            ],
            capture_output=True,
            text=True,
            timeout=120,
        )
        if proc.returncode != 0:
            raise RuntimeError((proc.stderr or proc.stdout or "Export failed.").strip())
        return {"output_dir": str(out_dir), "stdout": proc.stdout.strip()}

    def _tool_project_bundle(self, args: dict[str, Any]) -> dict[str, Any]:
        self._require_player()
        assert self.player_package is not None
        proj_file = self._resolve_project_file(str(args["project_path"]))
        out_path = pathlib.Path(str(args["output_path"])).resolve()
        script = self.repo_root / "scripts/afterlight.py"
        proc = subprocess.run(
            [
                sys.executable,
                str(script),
                "bundle",
                str(proj_file),
                "--output",
                str(out_path),
                "--player",
                str(self.player_package),
            ],
            capture_output=True,
            text=True,
            timeout=120,
        )
        if proc.returncode != 0:
            raise RuntimeError((proc.stderr or proc.stdout or "Bundle failed.").strip())
        return {"output_path": str(out_path), "stdout": proc.stdout.strip()}

    def _tool_scene_undo(self, args: dict[str, Any]) -> dict[str, Any]:
        p = pathlib.Path(str(args["scene_path"])).resolve()
        key = str(p)
        stack = self.undo_stack.get(key)
        if not stack:
            raise RuntimeError(f"No undo history available for scene: {p}")
        prev = stack.pop()
        p.write_bytes(prev)
        return {"scene_path": key, "restored": True, "remaining_undo_steps": len(stack)}

    def _tool_generate_mesh(self, args: dict[str, Any]) -> dict[str, Any]:
        shape = str(args["shape"])
        out_path = pathlib.Path(str(args["output_path"])).resolve()
        params = args.get("params") if isinstance(args.get("params"), dict) else {}
        obj_text, meta = generate_obj_mesh(shape, params)
        out_path.parent.mkdir(parents=True, exist_ok=True)
        out_path.write_text(obj_text, encoding="utf-8")
        result: dict[str, Any] = {
            "output_path": str(out_path),
            **meta,
        }
        scene_path = args.get("scene_path")
        if scene_path:
            sp = pathlib.Path(str(scene_path)).resolve()
            asset_name = str(args.get("asset_name") or out_path.stem)
            rel = _rel_asset_path(sp, out_path)
            snap = self._snapshot_scene(str(sp))
            resp, _, _ = self._run_agent(
                "asset_import",
                {"scene_path": str(sp), "name": asset_name, "kind": "mesh", "asset_path": rel},
            )
            if not resp.get("ok"):
                self._discard_last_snapshot(snap)
                raise RuntimeError(resp.get("error", "Failed to register generated mesh in scene."))
            result["registered_asset"] = resp.get("data", {})
        return result

    def _tool_generate_texture(self, args: dict[str, Any]) -> dict[str, Any]:
        pattern = str(args["pattern"])
        out_path = pathlib.Path(str(args["output_path"])).resolve()
        width = int(args.get("width", 256))
        height = int(args.get("height", 256))
        params = args.get("params") if isinstance(args.get("params"), dict) else {}
        png_bytes = generate_texture_png(pattern, width, height, params)
        out_path.parent.mkdir(parents=True, exist_ok=True)
        out_path.write_bytes(png_bytes)
        result: dict[str, Any] = {
            "output_path": str(out_path),
            "pattern": pattern,
            "width": max(16, min(1024, width)),
            "height": max(16, min(1024, height)),
            "bytes": len(png_bytes),
        }
        scene_path = args.get("scene_path")
        if scene_path:
            sp = pathlib.Path(str(scene_path)).resolve()
            asset_name = str(args.get("asset_name") or out_path.stem)
            default_kind = "normal_map" if pattern.lower().startswith("normal_") else "texture"
            kind = str(args.get("asset_kind") or default_kind)
            rel = _rel_asset_path(sp, out_path)
            snap = self._snapshot_scene(str(sp))
            resp, _, _ = self._run_agent(
                "asset_import",
                {"scene_path": str(sp), "name": asset_name, "kind": kind, "asset_path": rel},
            )
            if not resp.get("ok"):
                self._discard_last_snapshot(snap)
                raise RuntimeError(resp.get("error", "Failed to register generated texture in scene."))
            result["registered_asset"] = resp.get("data", {})
        return result

    def _tool_compile_shader(self, args: dict[str, Any]) -> dict[str, Any]:
        glslang = shutil.which("glslangValidator")
        if not glslang:
            for cand in (
                os.path.join(os.environ.get("VULKAN_SDK", ""), "bin", "glslangValidator"),
                "/opt/homebrew/bin/glslangValidator",
                "/usr/local/bin/glslangValidator",
            ):
                if cand and os.path.isfile(cand):
                    glslang = cand
                    break
        if not glslang:
            raise RuntimeError("glslangValidator not found in PATH or VULKAN_SDK.")

        out_path = pathlib.Path(str(args["output_path"])).resolve()
        out_path.parent.mkdir(parents=True, exist_ok=True)
        source_path = args.get("source_path")
        source_code = args.get("source_code")
        if not source_path and not source_code:
            raise ValueError("compile_shader requires 'source_path' or 'source_code'.")

        tmp_dir: tempfile.TemporaryDirectory[str] | None = None
        try:
            if source_path:
                src = pathlib.Path(str(source_path)).resolve()
                if source_code is not None:
                    src.parent.mkdir(parents=True, exist_ok=True)
                    src.write_text(str(source_code), encoding="utf-8")
            else:
                tmp_dir = tempfile.TemporaryDirectory(prefix="af_mcp_shader_")
                src = pathlib.Path(tmp_dir.name) / "shader.frag"
                src.write_text(str(source_code), encoding="utf-8")

            proc = subprocess.run(
                [glslang, "--target-env", "vulkan1.1", "--spirv-val", "-V", "-S", "frag", "-o", str(out_path), str(src)],
                capture_output=True,
                text=True,
                timeout=30,
            )
            return {
                "ok": proc.returncode == 0,
                "exit_code": proc.returncode,
                "output_path": str(out_path),
                "bytes": out_path.stat().st_size if out_path.is_file() else 0,
                "stdout": proc.stdout.strip(),
                "stderr": proc.stderr.strip(),
            }
        finally:
            if tmp_dir is not None:
                tmp_dir.cleanup()

    def _tool_run_game(self, args: dict[str, Any]) -> dict[str, Any]:
        player = self._require_player()
        proj_file = self._resolve_project_file(str(args["project_path"]))
        frames = max(1, int(args.get("frames", 30)))
        validation = bool(args.get("validation", True))
        backend = str(args.get("backend", "auto")).lower()
        dummy_audio = bool(args.get("dummy_audio", True))
        timeout = float(args.get("timeout_seconds", 60.0))

        cmd = [str(player), "--project", str(proj_file), "--frames", str(frames)]
        cmd.append("--validation" if validation else "--no-validation")

        env = os.environ.copy()
        if dummy_audio:
            env["SDL_AUDIO_DRIVER"] = "dummy"
        if backend in ("metal", "vulkan"):
            env["AFTERLIGHT_BACKEND"] = backend

        t0 = time.monotonic()
        proc = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout, env=env)
        elapsed = round(time.monotonic() - t0, 3)
        combined = proc.stdout + "\n" + proc.stderr
        return {
            "ok": proc.returncode == 0,
            "exit_code": proc.returncode,
            "elapsed_seconds": elapsed,
            "project_path": str(proj_file),
            "frames": frames,
            "stdout": proc.stdout.strip(),
            "stderr": proc.stderr.strip(),
            "issues": _extract_engine_issues(combined),
        }

    def _tool_capture_screenshot(self, args: dict[str, Any]) -> list[dict[str, Any]]:
        agent_args = dict(args)
        out_arg = agent_args.pop("output_path", None)
        max_inline = int(agent_args.pop("max_inline_width", 1024))
        return_image = bool(agent_args.pop("return_image", True))

        if out_arg:
            png_path = pathlib.Path(str(out_arg)).resolve()
            png_path.parent.mkdir(parents=True, exist_ok=True)
        else:
            self.capture_dir.mkdir(parents=True, exist_ok=True)
            png_path = self.capture_dir / f"capture_{int(time.time() * 1000)}.png"

        with tempfile.TemporaryDirectory(prefix="af_mcp_cap_") as td:
            afra_path = pathlib.Path(td) / "frame.afra"
            agent_args["rgba_path"] = str(afra_path)
            resp, game_stdout, engine_stderr = self._run_agent("capture", agent_args, timeout=120.0)
            if not resp.get("ok"):
                err = resp.get("error", "Capture failed.")
                if engine_stderr.strip():
                    err = f"{err}\nEngine stderr:\n{engine_stderr.strip()}"
                raise RuntimeError(err)

            rgba, w, h = decode_afra(afra_path)
            full_png = encode_png(rgba, w, h)
            png_path.write_bytes(full_png)

            inline_png = full_png
            inline_w, inline_h = w, h
            if return_image and max_inline > 0 and (w > max_inline or h > max_inline):
                small_rgba, inline_w, inline_h = downscale_rgba(rgba, w, h, max_inline)
                inline_png = encode_png(small_rgba, inline_w, inline_h)

        data = resp.get("data", {})
        data["png_path"] = str(png_path)
        data["png_bytes"] = len(full_png)
        data["inline_dimensions"] = [inline_w, inline_h]
        if game_stdout.strip():
            data["game_stdout"] = game_stdout.strip()
        if engine_stderr.strip():
            data["engine_log"] = engine_stderr.strip()

        warnings: list[str] = []
        if data.get("validation_errors", 0) > 0:
            warnings.append(f"GPU validation errors detected: {data['validation_errors']}")
        if data.get("missing_meshes", 0) > 0:
            warnings.append(f"Missing meshes: {data['missing_meshes']} visible object(s) had unresolved mesh names.")
        if data.get("missing_textures", 0) > 0:
            warnings.append(f"Missing textures: {data['missing_textures']} material(s) had unresolved texture names.")
        if warnings:
            data["warnings"] = warnings

        content: list[dict[str, Any]] = [
            {"type": "text", "text": json.dumps(data, indent=2)},
        ]
        if return_image:
            content.append(
                {
                    "type": "image",
                    "data": base64.b64encode(inline_png).decode("ascii"),
                    "mimeType": "image/png",
                }
            )
        return content

    def call_tool(self, name: str, arguments: dict[str, Any] | None) -> dict[str, Any]:
        args = arguments if isinstance(arguments, dict) else {}
        try:
            if name == "capture_screenshot":
                content = self._tool_capture_screenshot(args)
                return {"content": content, "isError": False}

            py_handlers = {
                "project_create": self._tool_project_create,
                "project_inspect": self._tool_project_inspect,
                "project_build_module": self._tool_project_build_module,
                "project_validate": self._tool_project_validate,
                "project_export": self._tool_project_export,
                "project_bundle": self._tool_project_bundle,
                "scene_undo": self._tool_scene_undo,
                "generate_mesh": self._tool_generate_mesh,
                "generate_texture": self._tool_generate_texture,
                "compile_shader": self._tool_compile_shader,
                "run_game": self._tool_run_game,
            }
            if name in py_handlers:
                result = py_handlers[name](args)
                return {
                    "content": [{"type": "text", "text": json.dumps(result, indent=2)}],
                    "isError": False,
                }

            # Delegate directly to native C17 afterlight_agent.
            snap_key = self._snapshot_scene(args.get("scene_path")) if name in MUTATING_AGENT_COMMANDS else None
            resp, _, stderr = self._run_agent(name, args)
            if not resp.get("ok"):
                self._discard_last_snapshot(snap_key)
                err_msg = resp.get("error", f"Tool '{name}' failed.")
                if stderr.strip():
                    err_msg = f"{err_msg}\n{stderr.strip()}"
                return {"content": [{"type": "text", "text": err_msg}], "isError": True}

            return {
                "content": [{"type": "text", "text": json.dumps(resp.get("data", {}), indent=2)}],
                "isError": False,
            }
        except Exception as exc:  # noqa: BLE001
            return {
                "content": [{"type": "text", "text": f"Error in {name}: {exc}"}],
                "isError": True,
            }

    # ---- MCP Resources & Prompts Handlers ----

    def list_resources(self) -> list[dict[str, Any]]:
        out: list[dict[str, Any]] = []
        for uri, title, mime, rel in RESOURCES:
            if (self.repo_root / rel).is_file():
                out.append({"uri": uri, "name": title, "mimeType": mime})
        return out

    def read_resource(self, uri: str) -> dict[str, Any]:
        for r_uri, _, mime, rel in RESOURCES:
            if r_uri == uri:
                path = self.repo_root / rel
                if not path.is_file():
                    raise FileNotFoundError(f"Resource file missing: {rel}")
                return {
                    "contents": [
                        {
                            "uri": uri,
                            "mimeType": mime,
                            "text": path.read_text(encoding="utf-8", errors="replace"),
                        }
                    ]
                }
        raise KeyError(f"Unknown resource URI: {uri}")

    def get_prompt(self, name: str, arguments: dict[str, Any] | None) -> dict[str, Any]:
        args = arguments or {}
        if name == "create_game":
            proj_dir = args.get("project_dir", "./my_game")
            concept = args.get("concept", "a playable 3D adventure game")
            text = (
                f"Create a playable Afterlight Engine game at `{proj_dir}` based on this concept: {concept}\n\n"
                "Follow this closed-loop workflow:\n"
                "1. Call `project_create` to scaffold the project, then `scene_inspect` on `scenes/start.afterlight.json`.\n"
                "2. Generate bespoke geometry and textures (`generate_mesh`, `generate_texture`) or import external glTF/OBJ/HDR/WAV files (`asset_import`).\n"
                "3. For outdoor environments, call `terrain_create` (with `procedural_hills` and `auto_paint_slopes`), sculpt key landmarks (`terrain_sculpt`), query heights (`terrain_query`) to ground objects accurately, and scatter instanced vegetation (`foliage_add_layer` + `foliage_populate`).\n"
                "4. Populate entities (`object_create`) with PBR materials, colliders, rigid bodies, joints, or water volumes.\n"
                "5. Configure sun/local lights (`lighting_set_sun`, `lighting_add_local`, `lighting_set_shadows`), post-processing/skybox/RT (`render_set_settings`), wind (`wind_set`), and primary camera (`camera_configure`).\n"
                "6. Edit `game/game.c` for custom mechanics (consult `afterlight://docs/game-code` and `afterlight://headers/game_module.h`), compile with `project_build_module`, validate with `project_validate`, and run with `run_game` to inspect any runtime errors.\n"
                "7. Call `capture_screenshot` from multiple angles (and with `run_game: true` on the project) to visually inspect your work. Iterate until framing, lighting, materials, physics, and zero errors are verified."
            )
            return {
                "description": "Guided Afterlight game creation workflow",
                "messages": [{"role": "user", "content": {"type": "text", "text": text}}],
            }
        if name == "visual_iterate":
            scene_path = args.get("scene_path", "scenes/start.afterlight.json")
            goal = args.get("goal", "improve visual composition, lighting, materials, and grounding")
            text = (
                f"Visually inspect and iterate on `{scene_path}` to {goal}.\n\n"
                "1. Call `scene_inspect` to understand all objects, lighting, terrain, and render settings.\n"
                "2. Call `capture_screenshot` from the primary game camera AND an elevated overview camera (`camera: {position, look_at}`).\n"
                "3. Examine the returned screenshots and render stats (missing_meshes, missing_textures, validation_errors, draw_calls). Look for floating/clipping objects, flat lighting, harsh exposure, or empty framing.\n"
                "4. Apply targeted fixes with `object_update`, `terrain_sculpt`, `lighting_*`, `render_set_settings`, or `camera_configure` (use `scene_undo` if a change looks worse).\n"
                "5. Re-capture screenshots to verify every improvement."
            )
            return {
                "description": "Visual inspection and polish workflow",
                "messages": [{"role": "user", "content": {"type": "text", "text": text}}],
            }
        raise KeyError(f"Unknown prompt: {name}")

    # ---- JSON-RPC 2.0 Dispatch & Stdio Transport ----

    def handle_message(self, msg: dict[str, Any]) -> dict[str, Any] | None:
        method = msg.get("method")
        msg_id = msg.get("id")
        params = msg.get("params") if isinstance(msg.get("params"), dict) else {}

        # Notifications have no 'id' and must not elicit a response.
        if msg_id is None:
            return None

        def ok(result: Any) -> dict[str, Any]:
            return {"jsonrpc": "2.0", "id": msg_id, "result": result}

        def err(code: int, message: str) -> dict[str, Any]:
            return {"jsonrpc": "2.0", "id": msg_id, "error": {"code": code, "message": message}}

        try:
            if method == "initialize":
                req_ver = str(params.get("protocolVersion", DEFAULT_PROTOCOL_VERSION))
                proto = req_ver if req_ver in SUPPORTED_PROTOCOL_VERSIONS else DEFAULT_PROTOCOL_VERSION
                return ok(
                    {
                        "protocolVersion": proto,
                        "capabilities": {
                            "tools": {"listChanged": False},
                            "resources": {"subscribe": False, "listChanged": False},
                            "prompts": {"listChanged": False},
                        },
                        "serverInfo": {
                            "name": "afterlight-engine",
                            "version": self.version,
                        },
                        "instructions": (
                            "Afterlight Engine MCP server. Build scenes, terrain, foliage, PBR materials, lighting, "
                            "physics, water, and C17 gameplay modules, then visually inspect real Metal/Vulkan GPU "
                            "renders via capture_screenshot and run_game."
                        ),
                    }
                )
            if method == "ping":
                return ok({})
            if method == "tools/list":
                return ok({"tools": self.tools})
            if method == "tools/call":
                tool_name = params.get("name")
                if not isinstance(tool_name, str) or not tool_name:
                    return err(-32602, "tools/call requires string 'name'.")
                return ok(self.call_tool(tool_name, params.get("arguments")))
            if method == "resources/list":
                return ok({"resources": self.list_resources()})
            if method == "resources/read":
                uri = params.get("uri")
                if not isinstance(uri, str) or not uri:
                    return err(-32602, "resources/read requires string 'uri'.")
                return ok(self.read_resource(uri))
            if method == "prompts/list":
                return ok({"prompts": PROMPTS})
            if method == "prompts/get":
                pname = params.get("name")
                if not isinstance(pname, str) or not pname:
                    return err(-32602, "prompts/get requires string 'name'.")
                return ok(self.get_prompt(pname, params.get("arguments")))
            return err(-32601, f"Method not found: {method}")
        except KeyError as exc:
            return err(-32602, str(exc))
        except Exception as exc:  # noqa: BLE001
            return err(-32603, f"Internal server error: {exc}")

    def _read_message(self) -> dict[str, Any] | None:
        buf = sys.stdin.buffer
        while True:
            line = buf.readline()
            if not line:
                return None
            stripped = line.strip()
            if not stripped:
                continue
            if stripped.startswith(b"{") or stripped.startswith(b"["):
                self.transport_mode = "ndjson"
                return json.loads(stripped.decode("utf-8"))
            if stripped.lower().startswith(b"content-length:"):
                self.transport_mode = "content-length"
                length = int(stripped.split(b":", 1)[1].strip())
                while True:
                    hdr = buf.readline()
                    if not hdr or hdr in (b"\r\n", b"\n"):
                        break
                payload = buf.read(length)
                if not payload:
                    return None
                return json.loads(payload.decode("utf-8"))
            # Skip unexpected header lines cleanly.

    def _write_message(self, payload: dict[str, Any]) -> None:
        raw = json.dumps(payload, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
        out = sys.stdout.buffer
        if self.transport_mode == "content-length":
            out.write(f"Content-Length: {len(raw)}\r\n\r\n".encode("ascii"))
            out.write(raw)
        else:
            out.write(raw + b"\n")
        out.flush()

    def serve_forever(self) -> int:
        while True:
            try:
                msg = self._read_message()
            except json.JSONDecodeError as exc:
                self._write_message(
                    {"jsonrpc": "2.0", "id": None, "error": {"code": -32700, "message": f"Parse error: {exc}"}}
                )
                continue
            if msg is None:
                return 0
            if isinstance(msg, dict):
                resp = self.handle_message(msg)
                if resp is not None:
                    self._write_message(resp)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Afterlight Engine MCP stdio server")
    parser.add_argument("--repo-root", type=pathlib.Path, default=pathlib.Path(__file__).resolve().parents[2])
    parser.add_argument("--build-dir", type=pathlib.Path, default=None, help="CMake build directory containing afterlight_agent & afterlight_player")
    parser.add_argument("--agent", type=pathlib.Path, default=None, help="Explicit path to afterlight_agent binary")
    parser.add_argument("--player", type=pathlib.Path, default=None, help="Explicit path to afterlight_player binary")
    parser.add_argument("--list-tools", action="store_true", help="Print exposed MCP tool names and exit")
    args = parser.parse_args(argv)

    server = AfterlightMCPServer(
        repo_root=args.repo_root,
        build_dir=args.build_dir,
        agent_bin=args.agent,
        player_bin=args.player,
    )
    if args.list_tools:
        for t in server.tools:
            print(f"{t['name']}: {t['description']}")
        return 0
    return server.serve_forever()


if __name__ == "__main__":
    raise SystemExit(main())
