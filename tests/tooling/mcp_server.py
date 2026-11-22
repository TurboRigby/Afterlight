#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""End-to-end integration test for the Afterlight Engine MCP server."""
from __future__ import annotations

import argparse
import base64
import json
import os
import pathlib
import subprocess
import sys
import tempfile
import zlib
from typing import Any


class MCPClient:
    """Minimal stdio MCP JSON-RPC 2.0 test client supporting ndjson or Content-Length."""

    def __init__(self, cmd: list[str], framed: bool = False) -> None:
        self.framed = framed
        self.next_id = 1
        env = os.environ.copy()
        env["SDL_AUDIO_DRIVER"] = "dummy"
        self.proc = subprocess.Popen(
            cmd,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            env=env,
        )

    def _send(self, msg: dict[str, Any]) -> None:
        assert self.proc.stdin is not None
        raw = json.dumps(msg, separators=(",", ":")).encode("utf-8")
        if self.framed:
            self.proc.stdin.write(f"Content-Length: {len(raw)}\r\n\r\n".encode("ascii"))
            self.proc.stdin.write(raw)
        else:
            self.proc.stdin.write(raw + b"\n")
        self.proc.stdin.flush()

    def _recv(self) -> dict[str, Any]:
        assert self.proc.stdout is not None
        if self.framed:
            length = None
            while True:
                line = self.proc.stdout.readline()
                assert line, f"Unexpected EOF from MCP server. stderr={self._read_stderr()}"
                if line in (b"\r\n", b"\n"):
                    break
                if line.lower().startswith(b"content-length:"):
                    length = int(line.split(b":", 1)[1].strip())
            assert length is not None, "Missing Content-Length header"
            payload = self.proc.stdout.read(length)
            return json.loads(payload.decode("utf-8"))
        line = self.proc.stdout.readline()
        assert line, f"Unexpected EOF from MCP server. stderr={self._read_stderr()}"
        return json.loads(line.decode("utf-8"))

    def _read_stderr(self) -> str:
        if self.proc.stderr is None:
            return ""
        try:
            return self.proc.stderr.read().decode("utf-8", errors="replace")
        except Exception:
            return ""

    def request(self, method: str, params: dict[str, Any] | None = None) -> dict[str, Any]:
        rid = self.next_id
        self.next_id += 1
        msg: dict[str, Any] = {"jsonrpc": "2.0", "id": rid, "method": method}
        if params is not None:
            msg["params"] = params
        self._send(msg)
        resp = self._recv()
        assert resp.get("id") == rid, f"Mismatched JSON-RPC id: {resp}"
        assert "error" not in resp, f"JSON-RPC error for {method}: {resp['error']}"
        return resp["result"]

    def notify(self, method: str, params: dict[str, Any] | None = None) -> None:
        msg: dict[str, Any] = {"jsonrpc": "2.0", "method": method}
        if params is not None:
            msg["params"] = params
        self._send(msg)

    def call_tool(self, name: str, arguments: dict[str, Any], expect_error: bool = False) -> tuple[dict[str, Any], list[dict[str, Any]]]:
        res = self.request("tools/call", {"name": name, "arguments": arguments})
        is_err = bool(res.get("isError", False))
        content = res.get("content", [])
        if expect_error:
            assert is_err, f"Expected error from {name}, got success: {content}"
            return {}, content
        assert not is_err, f"Tool {name} failed unexpectedly: {content}"
        assert content and content[0]["type"] == "text", f"Missing text block from {name}: {content}"
        return json.loads(content[0]["text"]), content

    def close(self) -> None:
        if self.proc.stdin:
            self.proc.stdin.close()
        rc = self.proc.wait(timeout=15)
        assert rc == 0, f"MCP server exited with code {rc}: {self._read_stderr()}"


def verify_png_non_uniform(png_bytes: bytes) -> None:
    """Validate PNG signature/chunks and confirm rendered pixels are not blank/single-color."""
    assert png_bytes.startswith(b"\x89PNG\r\n\x1a\n"), "Missing PNG signature"
    off = 8
    idat_parts: list[bytes] = []
    width = height = 0
    while off + 8 <= len(png_bytes):
        length = int.from_bytes(png_bytes[off : off + 4], "big")
        ctype = png_bytes[off + 4 : off + 8]
        data = png_bytes[off + 8 : off + 8 + length]
        off += 12 + length
        if ctype == b"IHDR":
            width = int.from_bytes(data[0:4], "big")
            height = int.from_bytes(data[4:8], "big")
        elif ctype == b"IDAT":
            idat_parts.append(data)
        elif ctype == b"IEND":
            break
    assert width >= 64 and height >= 64, f"Unexpected PNG dimensions: {width}x{height}"
    raw = zlib.decompress(b"".join(idat_parts))
    stride = width * 4 + 1
    samples = set()
    for y in range(0, height, max(1, height // 24)):
        row = y * stride + 1
        for x in range(0, width, max(1, width // 24)):
            p = row + x * 4
            samples.add((raw[p] >> 4, raw[p + 1] >> 4, raw[p + 2] >> 4))
    assert len(samples) >= 4, f"Rendered screenshot looks uniform/blank (unique quantized colors={len(samples)})"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=pathlib.Path, required=True)
    parser.add_argument("--agent", type=pathlib.Path, required=True)
    parser.add_argument("--player", type=pathlib.Path, required=True)
    args = parser.parse_args()

    server_py = args.root / "tools/mcp/afterlight_mcp.py"
    base_cmd = [
        sys.executable,
        str(server_py),
        "--repo-root",
        str(args.root),
        "--agent",
        str(args.agent),
        "--player",
        str(args.player),
    ]

    # 1. Verify Content-Length framed transport handshake + ping.
    framed_client = MCPClient(base_cmd, framed=True)
    try:
        init = framed_client.request(
            "initialize",
            {"protocolVersion": "2024-11-05", "capabilities": {}, "clientInfo": {"name": "test", "version": "1.0"}},
        )
        assert init["serverInfo"]["name"] == "afterlight-engine"
        framed_client.notify("notifications/initialized")
        assert framed_client.request("ping") == {}
    finally:
        framed_client.close()

    # 2. Full end-to-end game creation & visual verification over newline-delimited stdio MCP.
    client = MCPClient(base_cmd, framed=False)
    try:
        init = client.request(
            "initialize",
            {"protocolVersion": "2025-03-26", "capabilities": {}, "clientInfo": {"name": "e2e", "version": "1.0"}},
        )
        assert init["protocolVersion"] == "2025-03-26"
        client.notify("notifications/initialized")

        tools = client.request("tools/list")["tools"]
        tool_names = {t["name"] for t in tools}
        assert len(tools) >= 35, f"Expected >= 35 tools, got {len(tools)}"
        for expected in (
            "project_create",
            "project_build_module",
            "project_validate",
            "scene_create",
            "scene_inspect",
            "scene_undo",
            "object_create",
            "asset_import",
            "generate_mesh",
            "generate_texture",
            "terrain_create",
            "terrain_sculpt",
            "terrain_query",
            "foliage_add_layer",
            "foliage_populate",
            "lighting_set_sun",
            "lighting_add_local",
            "render_set_settings",
            "camera_configure",
            "run_game",
            "capture_screenshot",
        ):
            assert expected in tool_names, f"Missing tool: {expected}"

        resources = client.request("resources/list")["resources"]
        assert any(r["uri"] == "afterlight://docs/guide" for r in resources)
        guide = client.request("resources/read", {"uri": "afterlight://docs/guide"})
        assert "Afterlight" in guide["contents"][0]["text"]

        prompts = client.request("prompts/list")["prompts"]
        assert any(p["name"] == "create_game" for p in prompts)
        prompt_res = client.request("prompts/get", {"name": "create_game", "arguments": {"project_dir": "/tmp/demo", "concept": "island"}})
        assert "capture_screenshot" in prompt_res["messages"][0]["content"]["text"]

        with tempfile.TemporaryDirectory(prefix="af_mcp_e2e_") as td:
            work = pathlib.Path(td)
            proj_dir = work / "meadow_game"

            # Create & inspect project.
            proj, _ = client.call_tool("project_create", {"directory": str(proj_dir), "name": "Meadow MCP Test"})
            proj_file = proj["project_file"]
            scene_path = proj["startup_scene"]
            inspected_proj, _ = client.call_tool("project_inspect", {"project_path": proj_file})
            assert inspected_proj["startup_scene_exists"] is True

            # Generate procedural meshes & tileable textures + normal map.
            tree_res, _ = client.call_tool(
                "generate_mesh",
                {
                    "shape": "tree",
                    "output_path": str(proj_dir / "assets/pine.obj"),
                    "params": {"trunk_height": 1.6, "canopy_radius": 1.1, "tiers": 3},
                    "scene_path": scene_path,
                    "asset_name": "pine_tree",
                },
            )
            assert tree_res["triangles"] > 20

            client.call_tool(
                "generate_mesh",
                {
                    "shape": "arch",
                    "output_path": str(proj_dir / "assets/arch.obj"),
                    "params": {"width": 4.0, "height": 4.5, "depth": 0.9},
                    "scene_path": scene_path,
                    "asset_name": "stone_arch",
                },
            )
            client.call_tool(
                "generate_texture",
                {
                    "pattern": "grass",
                    "output_path": str(proj_dir / "assets/grass.png"),
                    "width": 128,
                    "height": 128,
                    "scene_path": scene_path,
                    "asset_name": "grass_tex",
                },
            )
            client.call_tool(
                "generate_texture",
                {
                    "pattern": "brick",
                    "output_path": str(proj_dir / "assets/brick.png"),
                    "width": 128,
                    "height": 128,
                    "scene_path": scene_path,
                    "asset_name": "brick_tex",
                },
            )
            client.call_tool(
                "generate_texture",
                {
                    "pattern": "normal_brick",
                    "output_path": str(proj_dir / "assets/brick_n.png"),
                    "width": 128,
                    "height": 128,
                    "scene_path": scene_path,
                    "asset_name": "brick_norm",
                },
            )

            # Import a real glTF/GLB model & HDR environment from sandbox assets.
            beacon = args.root / "sandbox/assets/beacon.glb"
            if beacon.is_file():
                model_res, _ = client.call_tool(
                    "asset_import",
                    {
                        "scene_path": scene_path,
                        "name": "beacon",
                        "kind": "model",
                        "asset_path": str(beacon),
                        "position": [3.5, 2.0, -4.0],
                    },
                )
                assert int(model_res["root_id"]) > 0

            # Generate terrain, sculpt, query heights, add & populate foliage.
            terr, _ = client.call_tool(
                "terrain_create",
                {
                    "scene_path": scene_path,
                    "chunks_x": 2,
                    "chunks_z": 2,
                    "vertex_spacing": 1.0,
                    "origin": [-16.0, 0.0, -16.0],
                    "procedural_hills": {"scale": 0.06, "amplitude": 4.0, "seed": 99},
                    "layers": [
                        {"name": "Grass", "diffuse_texture": "grass_tex", "color": [0.35, 0.52, 0.25, 1.0], "roughness": 0.85, "uv_scale": 6.0},
                        {"name": "Rock", "color": [0.46, 0.44, 0.42, 1.0], "roughness": 0.9, "uv_scale": 8.0},
                        {"name": "Sand", "color": [0.76, 0.68, 0.50, 1.0], "roughness": 0.8, "uv_scale": 8.0},
                    ],
                    "auto_paint_slopes": True,
                },
            )
            assert terr["world_size_x"] == 32.0

            client.call_tool(
                "terrain_sculpt",
                {
                    "scene_path": scene_path,
                    "operation": "flatten",
                    "center": [0.0, 0.0, 0.0],
                    "radius": 6.0,
                    "target_height": 1.5,
                    "strength": 1.0,
                },
            )
            tq, _ = client.call_tool("terrain_query", {"scene_path": scene_path, "points": [[0.0, 0.0], [10.0, -8.0]]})
            assert abs(tq["samples"][0]["height"] - 1.5) < 0.25

            fl, _ = client.call_tool(
                "foliage_add_layer",
                {
                    "scene_path": scene_path,
                    "name": "Pines",
                    "mesh": "pine_tree",
                    "material": {"physically_based": True, "base_color": [0.18, 0.42, 0.16, 1.0], "roughness": 0.82},
                    "rules": {"density": 0.18, "min_spacing": 2.5, "scale_min": [0.8, 0.8, 0.8], "scale_max": [1.4, 1.4, 1.4]},
                },
            )
            fpop, _ = client.call_tool(
                "foliage_populate",
                {"scene_path": scene_path, "mode": "populate", "layer_index": fl["layer_index"], "seed": 77},
            )
            assert fpop["total_foliage_instances"] > 0

            # Place textured arch, physics crate, water pool, compound collider, and joint.
            arch_obj, _ = client.call_tool(
                "object_create",
                {
                    "scene_path": scene_path,
                    "name": "PortalArch",
                    "mesh": "stone_arch",
                    "position": [0.0, 1.5, -5.0],
                    "material": {
                        "physically_based": True,
                        "texture": "brick_tex",
                        "normal_map": "brick_norm",
                        "roughness": 0.72,
                        "base_color": [0.95, 0.9, 0.85, 1.0],
                    },
                    "collider": {"type": "box", "half_extents": [2.0, 2.25, 0.45]},
                },
            )
            crate_a, _ = client.call_tool(
                "object_create",
                {
                    "scene_path": scene_path,
                    "name": "PhysicsCrate",
                    "mesh": "cube",
                    "position": [-2.0, 3.0, -2.0],
                    "material": {"physically_based": True, "base_color": [0.88, 0.42, 0.14, 1.0], "metallic": 0.2, "roughness": 0.35},
                    "collider": {"type": "box", "half_extents": [0.5, 0.5, 0.5]},
                    "body": {"enabled": True, "type": "dynamic", "mass": 4.0},
                },
            )
            crate_b, _ = client.call_tool(
                "object_duplicate",
                {"scene_path": scene_path, "id": crate_a["id"], "new_name": "CrateB", "offset": [1.5, 0.0, 0.0]},
            )
            jres, _ = client.call_tool(
                "joint_add",
                {"scene_path": scene_path, "name": "Link", "type": "distance", "object_a": crate_a["id"], "object_b": crate_b["id"], "distance": 1.6},
            )
            assert int(jres["joint_id"]) > 0

            client.call_tool(
                "object_create",
                {
                    "scene_path": scene_path,
                    "name": "Pond",
                    "mesh": "__node",
                    "visible": False,
                    "position": [0.0, 1.2, 4.0],
                    "water": {"mode": "pool", "half_size": [4.0, 4.0], "depth": 1.8},
                },
            )

            # Configure sun, local warm lantern, shadows, post-processing, wind & camera.
            client.call_tool("lighting_set_sun", {"scene_path": scene_path, "direction": [-0.45, -0.75, -0.48], "color": [1.0, 0.94, 0.84], "intensity": 3.0, "ambient": 0.22})
            client.call_tool("lighting_add_local", {"scene_path": scene_path, "name": "Lantern", "type": "point", "position": [0.0, 3.5, -4.5], "color": [1.0, 0.65, 0.25], "intensity": 12.0, "range": 14.0})
            client.call_tool("lighting_set_shadows", {"scene_path": scene_path, "enabled": True, "mode": "cascaded", "resolution": 1024, "distance": 60.0})
            client.call_tool("render_set_settings", {"scene_path": scene_path, "anti_aliasing": "fxaa", "tone_mapping": True, "tone_mapper": "aces", "bloom": True, "bloom_strength": 0.25, "skybox": True, "environment_enabled": True})
            client.call_tool("wind_set", {"scene_path": scene_path, "enabled": True, "direction": [1.0, 0.0, 0.3], "speed": 4.0, "gust_strength": 0.35})
            client.call_tool("camera_configure", {"scene_path": scene_path, "position": [0.0, 5.0, 11.0], "look_at": [0.0, 2.5, -4.0], "fov_degrees": 60.0})

            # Raycast against terrain/arch.
            rc, _ = client.call_tool("scene_raycast", {"scene_path": scene_path, "origin": [0.0, 20.0, 0.0], "direction": [0.0, -1.0, 0.0]})
            assert rc["hit"] is True

            # Verify scene_inspect round-trip.
            insp, _ = client.call_tool("scene_inspect", {"scene_path": scene_path})
            assert insp["object_count"] >= 6
            assert insp["local_light_count"] == 1
            assert insp["joint_count"] == 1
            assert insp["water_count"] == 1
            assert insp["landscape"]["total_foliage_instances"] > 0

            # Verify scene_undo rolls back cleanly.
            before_count = insp["object_count"]
            client.call_tool("object_create", {"scene_path": scene_path, "name": "TemporaryCube", "mesh": "cube"})
            assert client.call_tool("scene_inspect", {"scene_path": scene_path})[0]["object_count"] == before_count + 1
            client.call_tool("scene_undo", {"scene_path": scene_path})
            assert client.call_tool("scene_inspect", {"scene_path": scene_path})[0]["object_count"] == before_count

            # Build C game module, validate project, and run game!
            build_ok, _ = client.call_tool("project_build_module", {"project_dir": str(proj_dir)})
            assert build_ok["ok"] is True and build_ok["module_exists"] is True

            val_ok, _ = client.call_tool("project_validate", {"project_path": proj_file})
            assert val_ok["valid"] is True, f"Project validation failed: {val_ok}"

            run_ok, _ = client.call_tool("run_game", {"project_path": proj_file, "frames": 6, "validation": True})
            assert run_ok["ok"] is True, f"run_game failed: {run_ok}"

            # Verify compiler error inspection works without crashing MCP server.
            game_c = proj_dir / "game/game.c"
            orig_game_c = game_c.read_text(encoding="utf-8")
            game_c.write_text(orig_game_c + "\nvoid broken_fn(void) { int x = ; }\n", encoding="utf-8")
            build_bad, _ = client.call_tool("project_build_module", {"project_dir": str(proj_dir)})
            assert build_bad["ok"] is False and build_bad["exit_code"] != 0
            assert len(build_bad["diagnostics"]) >= 1
            game_c.write_text(orig_game_c, encoding="utf-8")
            assert client.call_tool("project_build_module", {"project_dir": str(proj_dir)})[0]["ok"] is True

            # Capture real GPU screenshots (static scene + live running project)!
            shot_path = work / "scene_shot.png"
            cap_data, cap_content = client.call_tool(
                "capture_screenshot",
                {
                    "scene_path": scene_path,
                    "output_path": str(shot_path),
                    "width": 640,
                    "height": 480,
                    "frames": 6,
                    "camera": {"position": [6.0, 6.5, 11.0], "look_at": [0.0, 2.5, -4.0], "fov_degrees": 60.0},
                },
            )
            assert cap_data["validation_errors"] == 0, f"Validation errors: {cap_data}"
            assert cap_data["missing_meshes"] == 0, f"Missing meshes: {cap_data}"
            assert cap_data["missing_textures"] == 0, f"Missing textures: {cap_data}"
            assert cap_data["draw_calls"] > 0
            assert shot_path.is_file()
            verify_png_non_uniform(shot_path.read_bytes())
            assert len(cap_content) == 2 and cap_content[1]["type"] == "image" and cap_content[1]["mimeType"] == "image/png"
            verify_png_non_uniform(base64.b64decode(cap_content[1]["data"]))

            proj_shot = work / "project_shot.png"
            pcap_data, _ = client.call_tool(
                "capture_screenshot",
                {
                    "project_path": proj_file,
                    "run_game": True,
                    "output_path": str(proj_shot),
                    "width": 480,
                    "height": 360,
                    "frames": 5,
                    "return_image": False,
                },
            )
            assert pcap_data["validation_errors"] == 0
            assert proj_shot.is_file()
            verify_png_non_uniform(proj_shot.read_bytes())

            # Verify invalid tool request returns structured isError without crashing server.
            client.call_tool("scene_inspect", {"scene_path": str(work / "does_not_exist.afterlight.json")}, expect_error=True)

            # Verify custom SPIR-V shader compilation (valid + syntax error diagnostics).
            spv_ok_path = work / "custom_ok.spv"
            sh_ok, _ = client.call_tool(
                "compile_shader",
                {
                    "source_path": str(args.root / "tests/render/vulkan/custom_material.frag"),
                    "output_path": str(spv_ok_path),
                },
            )
            assert sh_ok["ok"] is True and sh_ok["bytes"] > 100 and spv_ok_path.is_file()
            sh_bad, _ = client.call_tool(
                "compile_shader",
                {
                    "source_code": "#version 450\nvoid main() { BROKEN_SYNTAX }\n",
                    "output_path": str(work / "custom_bad.spv"),
                },
            )
            assert sh_bad["ok"] is False and sh_bad["exit_code"] != 0

            print(
                f"MCP server E2E passed: {len(tools)} tools, backend={cap_data['active_backend']}, "
                f"draw_calls={cap_data['draw_calls']}, foliage={fpop['total_foliage_instances']}, "
                f"shot={cap_data['width']}x{cap_data['height']}."
            )
    finally:
        client.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
