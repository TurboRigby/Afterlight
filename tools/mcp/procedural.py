# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Zero-dependency PNG encoding, AFRA decoding, and procedural mesh/texture generators."""
from __future__ import annotations

import array
import math
import pathlib
import struct
import zlib
from typing import Any


# ---- PNG & AFRA Image Helpers ----


def _png_chunk(chunk_type: bytes, data: bytes) -> bytes:
    crc = zlib.crc32(chunk_type)
    crc = zlib.crc32(data, crc) & 0xFFFFFFFF
    return struct.pack(">I", len(data)) + chunk_type + data + struct.pack(">I", crc)


def encode_png(rgba: bytes, width: int, height: int, level: int = 4) -> bytes:
    """Encode tightly packed RGBA8 bytes into a valid PNG byte stream."""
    if width <= 0 or height <= 0:
        raise ValueError(f"Invalid PNG dimensions: {width}x{height}")
    stride = width * 4
    expected = stride * height
    if len(rgba) != expected:
        raise ValueError(f"RGBA length mismatch: got {len(rgba)}, expected {expected}")

    mv = memoryview(rgba)
    raw = bytearray((stride + 1) * height)
    dst_stride = stride + 1
    for y in range(height):
        dst_off = y * dst_stride
        raw[dst_off] = 0  # Filter type 0 (None)
        src_off = y * stride
        raw[dst_off + 1 : dst_off + dst_stride] = mv[src_off : src_off + stride]

    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)  # 8-bit RGBA
    idat = zlib.compress(bytes(raw), level=level)
    return (
        b"\x89PNG\r\n\x1a\n"
        + _png_chunk(b"IHDR", ihdr)
        + _png_chunk(b"IDAT", idat)
        + _png_chunk(b"IEND", b"")
    )


def decode_afra(path: pathlib.Path) -> tuple[bytes, int, int]:
    """Decode Afterlight Agent raw RGBA capture format ('AFRA' + u32 w + u32 h + RGBA8)."""
    data = path.read_bytes()
    if len(data) < 12 or data[:4] != b"AFRA":
        raise ValueError(f"Invalid AFRA capture file: {path}")
    width, height = struct.unpack("<II", data[4:12])
    expected = width * height * 4
    if len(data) != 12 + expected:
        raise ValueError(f"Corrupted AFRA payload: expected {expected} bytes, got {len(data) - 12}")
    return data[12:], width, height


def downscale_rgba(rgba: bytes, width: int, height: int, max_dim: int) -> tuple[bytes, int, int]:
    """Fast integer-step downscale keeping aspect ratio when image exceeds max_dim."""
    if max_dim <= 0 or (width <= max_dim and height <= max_dim):
        return rgba, width, height
    step = max(2, math.ceil(max(width, height) / max_dim))
    pixels = array.array("I")
    pixels.frombytes(rgba)
    out = array.array("I")
    new_h = 0
    new_w = 0
    for y in range(0, height, step):
        row = pixels[y * width : (y + 1) * width : step]
        if new_w == 0:
            new_w = len(row)
        out.extend(row)
        new_h += 1
    return out.tobytes(), new_w, new_h


# ---- Procedural Wavefront OBJ Mesh Generator ----


class _ObjBuilder:
    def __init__(self) -> None:
        self.v: list[tuple[float, float, float]] = []
        self.vt: list[tuple[float, float]] = []
        self.vn: list[tuple[float, float, float]] = []
        self.faces: list[tuple[tuple[int, int, int], tuple[int, int, int], tuple[int, int, int]]] = []

    def add_vert(
        self,
        pos: tuple[float, float, float],
        uv: tuple[float, float],
        norm: tuple[float, float, float],
    ) -> tuple[int, int, int]:
        self.v.append(pos)
        self.vt.append(uv)
        nx, ny, nz = norm
        length = math.sqrt(nx * nx + ny * ny + nz * nz)
        if length > 1e-6:
            nx, ny, nz = nx / length, ny / length, nz / length
        else:
            nx, ny, nz = 0.0, 1.0, 0.0
        self.vn.append((nx, ny, nz))
        idx = len(self.v)
        return (idx, idx, idx)

    def add_tri(
        self,
        a: tuple[int, int, int],
        b: tuple[int, int, int],
        c: tuple[int, int, int],
    ) -> None:
        self.faces.append((a, b, c))

    def add_quad(
        self,
        a: tuple[int, int, int],
        b: tuple[int, int, int],
        c: tuple[int, int, int],
        d: tuple[int, int, int],
    ) -> None:
        self.faces.append((a, b, c))
        self.faces.append((a, c, d))

    def add_flat_tri(
        self,
        p0: tuple[float, float, float],
        p1: tuple[float, float, float],
        p2: tuple[float, float, float],
        uv0: tuple[float, float] = (0.0, 0.0),
        uv1: tuple[float, float] = (1.0, 0.0),
        uv2: tuple[float, float] = (0.5, 1.0),
    ) -> None:
        ux, uy, uz = p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]
        vx, vy, vz = p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2]
        n = (uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx)
        a = self.add_vert(p0, uv0, n)
        b = self.add_vert(p1, uv1, n)
        c = self.add_vert(p2, uv2, n)
        self.add_tri(a, b, c)

    def add_flat_quad(
        self,
        p0: tuple[float, float, float],
        p1: tuple[float, float, float],
        p2: tuple[float, float, float],
        p3: tuple[float, float, float],
        u_scale: float = 1.0,
        v_scale: float = 1.0,
    ) -> None:
        ux, uy, uz = p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]
        vx, vy, vz = p3[0] - p0[0], p3[1] - p0[1], p3[2] - p0[2]
        n = (uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx)
        a = self.add_vert(p0, (0.0, 0.0), n)
        b = self.add_vert(p1, (u_scale, 0.0), n)
        c = self.add_vert(p2, (u_scale, v_scale), n)
        d = self.add_vert(p3, (0.0, v_scale), n)
        self.add_quad(a, b, c, d)

    def add_box(
        self,
        cx: float,
        cy: float,
        cz: float,
        sx: float,
        sy: float,
        sz: float,
    ) -> None:
        hx, hy, hz = sx * 0.5, sy * 0.5, sz * 0.5
        x0, x1 = cx - hx, cx + hx
        y0, y1 = cy - hy, cy + hy
        z0, z1 = cz - hz, cz + hz
        # +Z front
        self.add_flat_quad((x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1), sx, sy)
        # -Z back
        self.add_flat_quad((x1, y0, z0), (x0, y0, z0), (x0, y1, z0), (x1, y1, z0), sx, sy)
        # +X right
        self.add_flat_quad((x1, y0, z1), (x1, y0, z0), (x1, y1, z0), (x1, y1, z1), sz, sy)
        # -X left
        self.add_flat_quad((x0, y0, z0), (x0, y0, z1), (x0, y1, z1), (x0, y1, z0), sz, sy)
        # +Y top
        self.add_flat_quad((x0, y1, z1), (x1, y1, z1), (x1, y1, z0), (x0, y1, z0), sx, sz)
        # -Y bottom
        self.add_flat_quad((x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1), sx, sz)

    def add_frustum(
        self,
        y0: float,
        y1: float,
        r0: float,
        r1: float,
        segments: int,
        cap_bottom: bool = True,
        cap_top: bool = True,
    ) -> None:
        seg = max(3, segments)
        h = y1 - y0
        slope_y = (r0 - r1) / max(1e-5, abs(h))
        ring0: list[tuple[int, int, int]] = []
        ring1: list[tuple[int, int, int]] = []
        for i in range(seg + 1):
            u = i / seg
            ang = u * 2.0 * math.pi
            ca, sa = math.cos(ang), math.sin(ang)
            n = (ca, slope_y, sa)
            ring0.append(self.add_vert((ca * r0, y0, sa * r0), (u, 0.0), n))
            ring1.append(self.add_vert((ca * r1, y1, sa * r1), (u, 1.0), n))
        for i in range(seg):
            self.add_quad(ring0[i], ring0[i + 1], ring1[i + 1], ring1[i])

        if cap_bottom and r0 > 1e-5:
            center = self.add_vert((0.0, y0, 0.0), (0.5, 0.5), (0.0, -1.0, 0.0))
            pts = [
                self.add_vert(
                    (math.cos(i * 2.0 * math.pi / seg) * r0, y0, math.sin(i * 2.0 * math.pi / seg) * r0),
                    (0.5 + 0.5 * math.cos(i * 2.0 * math.pi / seg), 0.5 + 0.5 * math.sin(i * 2.0 * math.pi / seg)),
                    (0.0, -1.0, 0.0),
                )
                for i in range(seg)
            ]
            for i in range(seg):
                self.add_tri(center, pts[(i + 1) % seg], pts[i])

        if cap_top and r1 > 1e-5:
            center = self.add_vert((0.0, y1, 0.0), (0.5, 0.5), (0.0, 1.0, 0.0))
            pts = [
                self.add_vert(
                    (math.cos(i * 2.0 * math.pi / seg) * r1, y1, math.sin(i * 2.0 * math.pi / seg) * r1),
                    (0.5 + 0.5 * math.cos(i * 2.0 * math.pi / seg), 0.5 + 0.5 * math.sin(i * 2.0 * math.pi / seg)),
                    (0.0, 1.0, 0.0),
                )
                for i in range(seg)
            ]
            for i in range(seg):
                self.add_tri(center, pts[i], pts[(i + 1) % seg])

    def to_obj(self, name: str) -> str:
        lines = [f"# Afterlight procedural mesh: {name}", f"o {name}"]
        for x, y, z in self.v:
            lines.append(f"v {x:.6f} {y:.6f} {z:.6f}")
        for u, v in self.vt:
            lines.append(f"vt {u:.6f} {v:.6f}")
        for nx, ny, nz in self.vn:
            lines.append(f"vn {nx:.6f} {ny:.6f} {nz:.6f}")
        for a, b, c in self.faces:
            lines.append(f"f {a[0]}/{a[1]}/{a[2]} {b[0]}/{b[1]}/{b[2]} {c[0]}/{c[1]}/{c[2]}")
        return "\n".join(lines) + "\n"


def _hash3(x: float, y: float, z: float, seed: int) -> float:
    h = math.sin(x * 12.9898 + y * 78.233 + z * 37.719 + seed * 19.19) * 43758.5453
    return h - math.floor(h)


def _value_noise3(x: float, y: float, z: float, seed: int) -> float:
    ix, iy, iz = math.floor(x), math.floor(y), math.floor(z)
    fx, fy, fz = x - ix, y - iy, z - iz
    ux = fx * fx * (3.0 - 2.0 * fx)
    uy = fy * fy * (3.0 - 2.0 * fy)
    uz = fz * fz * (3.0 - 2.0 * fz)

    def c(dx: int, dy: int, dz: int) -> float:
        return _hash3(ix + dx, iy + dy, iz + dz, seed)

    x00 = c(0, 0, 0) * (1.0 - ux) + c(1, 0, 0) * ux
    x10 = c(0, 1, 0) * (1.0 - ux) + c(1, 1, 0) * ux
    x01 = c(0, 0, 1) * (1.0 - ux) + c(1, 0, 1) * ux
    x11 = c(0, 1, 1) * (1.0 - ux) + c(1, 1, 1) * ux
    y0 = x00 * (1.0 - uy) + x10 * uy
    y1 = x01 * (1.0 - uy) + x11 * uy
    return y0 * (1.0 - uz) + y1 * uz


def generate_obj_mesh(shape: str, params: dict[str, Any]) -> tuple[str, dict[str, Any]]:
    """Generate a Wavefront .obj mesh string and summary metadata."""
    b = _ObjBuilder()
    shape_l = shape.lower().strip()

    if shape_l == "box":
        sz = params.get("size", [1.0, 1.0, 1.0])
        sx, sy, sz_ = float(sz[0]), float(sz[1]), float(sz[2])
        b.add_box(0.0, 0.0, 0.0, sx, sy, sz_)

    elif shape_l == "plane":
        sz = params.get("size", [4.0, 4.0])
        sx = float(sz[0])
        sz_ = float(sz[1] if len(sz) >= 2 else sz[0])
        sub = max(1, min(64, int(params.get("subdivisions", 1))))
        grid: list[list[tuple[int, int, int]]] = []
        for iz in range(sub + 1):
            row: list[tuple[int, int, int]] = []
            vz = iz / sub
            z = (vz - 0.5) * sz_
            for ix in range(sub + 1):
                ux = ix / sub
                x = (ux - 0.5) * sx
                row.append(b.add_vert((x, 0.0, z), (ux * sx, vz * sz_), (0.0, 1.0, 0.0)))
            grid.append(row)
        for iz in range(sub):
            for ix in range(sub):
                b.add_quad(grid[iz + 1][ix], grid[iz + 1][ix + 1], grid[iz][ix + 1], grid[iz][ix])

    elif shape_l == "sphere":
        r = max(0.01, float(params.get("radius", 0.5)))
        seg = max(6, min(96, int(params.get("segments", 24))))
        rings = max(4, min(64, int(params.get("rings", 16))))
        grid = []
        for iy in range(rings + 1):
            v = iy / rings
            phi = v * math.pi
            sp, cp = math.sin(phi), math.cos(phi)
            row = []
            for ix in range(seg + 1):
                u = ix / seg
                theta = u * 2.0 * math.pi
                nx, ny, nz = sp * math.cos(theta), cp, sp * math.sin(theta)
                row.append(b.add_vert((nx * r, ny * r, nz * r), (u, 1.0 - v), (nx, ny, nz)))
            grid.append(row)
        for iy in range(rings):
            for ix in range(seg):
                b.add_quad(grid[iy][ix], grid[iy + 1][ix], grid[iy + 1][ix + 1], grid[iy][ix + 1])

    elif shape_l == "cylinder":
        r_bot = max(0.01, float(params.get("radius_bottom", params.get("radius", 0.5))))
        r_top = max(0.0, float(params.get("radius_top", params.get("radius", 0.5))))
        h = max(0.01, float(params.get("height", 1.0)))
        seg = max(3, min(96, int(params.get("segments", 24))))
        b.add_frustum(-h * 0.5, h * 0.5, r_bot, r_top, seg, True, r_top > 1e-5)

    elif shape_l == "cone":
        r = max(0.01, float(params.get("radius", 0.5)))
        h = max(0.01, float(params.get("height", 1.0)))
        seg = max(3, min(96, int(params.get("segments", 24))))
        b.add_frustum(-h * 0.5, h * 0.5, r, 0.0, seg, True, False)

    elif shape_l == "ramp":
        w = max(0.05, float(params.get("width", 2.0)))
        h = max(0.05, float(params.get("height", 1.0)))
        d = max(0.05, float(params.get("depth", 3.0)))
        x0, x1 = -w * 0.5, w * 0.5
        y0, y1 = 0.0, h
        z0, z1 = -d * 0.5, d * 0.5
        # Bottom
        b.add_flat_quad((x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1), w, d)
        # Back vertical wall (-Z)
        b.add_flat_quad((x1, y0, z0), (x0, y0, z0), (x0, y1, z0), (x1, y1, z0), w, h)
        # Sloped top from +Z bottom to -Z top
        b.add_flat_quad((x0, y0, z1), (x1, y0, z1), (x1, y1, z0), (x0, y1, z0), w, math.hypot(d, h))
        # Left & right triangles
        b.add_flat_tri((x0, y0, z0), (x0, y0, z1), (x0, y1, z0))
        b.add_flat_tri((x1, y0, z1), (x1, y0, z0), (x1, y1, z0))

    elif shape_l == "stairs":
        w = max(0.1, float(params.get("width", 2.0)))
        h = max(0.1, float(params.get("height", 2.0)))
        d = max(0.1, float(params.get("depth", 4.0)))
        steps = max(2, min(64, int(params.get("steps", 8))))
        step_h = h / steps
        step_d = d / steps
        for i in range(steps):
            top_y = (i + 1) * step_h
            z_front = d * 0.5 - i * step_d
            z_back = -d * 0.5
            depth_i = z_front - z_back
            b.add_box(0.0, top_y * 0.5, (z_front + z_back) * 0.5, w, top_y, depth_i)

    elif shape_l == "arch":
        w = max(0.4, float(params.get("width", 3.0)))
        h = max(0.4, float(params.get("height", 3.5)))
        d = max(0.1, float(params.get("depth", 0.6)))
        thick = max(0.05, min(w * 0.35, float(params.get("thickness", 0.45))))
        seg = max(6, min(32, int(params.get("segments", 12))))
        outer_r = w * 0.5
        inner_r = max(0.1, outer_r - thick)
        pillar_h = max(0.2, h - outer_r)
        # Left & right pillars
        pillar_x = inner_r + thick * 0.5
        b.add_box(-pillar_x, pillar_h * 0.5, 0.0, thick, pillar_h, d)
        b.add_box(pillar_x, pillar_h * 0.5, 0.0, thick, pillar_h, d)
        # Curved arch segments
        z0, z1 = -d * 0.5, d * 0.5
        for i in range(seg):
            a0 = math.pi * (i / seg)
            a1 = math.pi * ((i + 1) / seg)
            c0, s0 = math.cos(a0), math.sin(a0)
            c1, s1 = math.cos(a1), math.sin(a1)
            # Inner & outer points (front z1, back z0)
            i0f = (c0 * inner_r, pillar_h + s0 * inner_r, z1)
            o0f = (c0 * outer_r, pillar_h + s0 * outer_r, z1)
            o1f = (c1 * outer_r, pillar_h + s1 * outer_r, z1)
            i1f = (c1 * inner_r, pillar_h + s1 * inner_r, z1)
            i0b = (c0 * inner_r, pillar_h + s0 * inner_r, z0)
            o0b = (c0 * outer_r, pillar_h + s0 * outer_r, z0)
            o1b = (c1 * outer_r, pillar_h + s1 * outer_r, z0)
            i1b = (c1 * inner_r, pillar_h + s1 * inner_r, z0)
            b.add_flat_quad(i0f, o0f, o1f, i1f)
            b.add_flat_quad(o0b, i0b, i1b, o1b)
            b.add_flat_quad(o0f, o0b, o1b, o1f)
            b.add_flat_quad(i0b, i0f, i1f, i1b)

    elif shape_l == "rock":
        r = max(0.05, float(params.get("radius", 0.8)))
        detail = max(4, min(20, int(params.get("detail", 8))))
        rough = max(0.0, min(0.6, float(params.get("roughness", 0.28))))
        seed = int(params.get("seed", 17))
        squash_y = max(0.3, min(2.0, float(params.get("squash_y", 0.72))))
        pts: list[list[tuple[float, float, float]]] = []
        for iy in range(detail + 1):
            phi = (iy / detail) * math.pi
            sp, cp = math.sin(phi), math.cos(phi)
            row_p: list[tuple[float, float, float]] = []
            for ix in range(detail * 2):
                theta = (ix / (detail * 2)) * 2.0 * math.pi
                dx, dy, dz = sp * math.cos(theta), cp, sp * math.sin(theta)
                n1 = _value_noise3(dx * 2.2 + 3.1, dy * 2.2 + 1.7, dz * 2.2 + 5.3, seed)
                n2 = _value_noise3(dx * 4.5 + 7.2, dy * 4.5 + 2.1, dz * 4.5 + 0.9, seed + 13)
                disp = 1.0 + rough * ((n1 * 0.7 + n2 * 0.3) * 2.0 - 1.0)
                row_p.append((dx * r * disp, dy * r * disp * squash_y, dz * r * disp))
            pts.append(row_p)
        cols = detail * 2
        for iy in range(detail):
            for ix in range(cols):
                nx_i = (ix + 1) % cols
                p00 = pts[iy][ix]
                p10 = pts[iy + 1][ix]
                p11 = pts[iy + 1][nx_i]
                p01 = pts[iy][nx_i]
                if iy > 0:
                    b.add_flat_tri(p00, p10, p01)
                if iy < detail - 1:
                    b.add_flat_tri(p10, p11, p01)

    elif shape_l == "tree":
        trunk_h = max(0.2, float(params.get("trunk_height", 1.8)))
        trunk_r = max(0.03, float(params.get("trunk_radius", 0.16)))
        canopy_r = max(0.2, float(params.get("canopy_radius", 1.15)))
        canopy_h = max(0.4, float(params.get("canopy_height", 2.8)))
        tiers = max(1, min(6, int(params.get("tiers", 3))))
        seg = max(5, min(24, int(params.get("segments", 9))))
        # Tapered trunk rooted at y=0
        b.add_frustum(0.0, trunk_h + canopy_h * 0.25, trunk_r, trunk_r * 0.45, seg, True, False)
        # Layered pine tiers
        for t in range(tiers):
            frac = t / tiers
            y_bot = trunk_h * 0.75 + frac * canopy_h * 0.65
            tier_h = canopy_h * (0.58 - frac * 0.12)
            r_bot = canopy_r * (1.0 - frac * 0.28)
            b.add_frustum(y_bot, y_bot + tier_h, r_bot, 0.0, seg, True, False)

    else:
        raise ValueError(
            f"Unsupported shape '{shape}'. Choose from: box, plane, sphere, cylinder, cone, ramp, stairs, arch, rock, tree."
        )

    text = b.to_obj(shape_l)
    meta = {
        "shape": shape_l,
        "vertices": len(b.v),
        "triangles": len(b.faces),
    }
    return text, meta


# ---- Procedural Tileable PNG Texture Generator ----


def _hash2(ix: int, iy: int, seed: int) -> float:
    n = (ix * 374761393 + iy * 668265263 + seed * 1442695041) & 0xFFFFFFFF
    n = ((n ^ (n >> 13)) * 1274126177) & 0xFFFFFFFF
    return ((n ^ (n >> 16)) & 0xFFFFFF) / 16777216.0


def _tile_noise(u: float, v: float, period: int, seed: int) -> float:
    x = u * period
    y = v * period
    ix, iy = int(math.floor(x)), int(math.floor(y))
    fx, fy = x - ix, y - iy
    ux = fx * fx * (3.0 - 2.0 * fx)
    uy = fy * fy * (3.0 - 2.0 * fy)
    x0, x1 = ix % period, (ix + 1) % period
    y0, y1 = iy % period, (iy + 1) % period
    a = _hash2(x0, y0, seed)
    b = _hash2(x1, y0, seed)
    c = _hash2(x0, y1, seed)
    d = _hash2(x1, y1, seed)
    return (a * (1.0 - ux) + b * ux) * (1.0 - uy) + (c * (1.0 - ux) + d * ux) * uy


def _fbm_tile(u: float, v: float, base_period: int, octaves: int, seed: int) -> float:
    val = 0.0
    amp = 0.5
    norm = 0.0
    per = max(2, base_period)
    for o in range(octaves):
        val += amp * _tile_noise(u, v, per, seed + o * 31)
        norm += amp
        amp *= 0.5
        per *= 2
    return val / max(1e-6, norm)


def _clamp_u8(v: float) -> int:
    return 0 if v <= 0.0 else (255 if v >= 1.0 else int(v * 255.0 + 0.5))


def _lerp_rgb(c0: tuple[float, float, float], c1: tuple[float, float, float], t: float) -> tuple[float, float, float]:
    t = max(0.0, min(1.0, t))
    return (c0[0] + (c1[0] - c0[0]) * t, c0[1] + (c1[1] - c0[1]) * t, c0[2] + (c1[2] - c0[2]) * t)


def generate_texture_png(pattern: str, width: int, height: int, params: dict[str, Any]) -> bytes:
    """Generate a tileable procedural RGBA8 PNG texture (albedo or tangent-space normal map)."""
    w = max(16, min(1024, int(width)))
    h = max(16, min(1024, int(height)))
    pat = pattern.lower().strip()
    seed = int(params.get("seed", 7))
    scale = max(1, int(params.get("scale", 8)))

    def col(key: str, default: tuple[float, float, float]) -> tuple[float, float, float]:
        raw = params.get(key)
        if isinstance(raw, list) and len(raw) >= 3:
            return (float(raw[0]), float(raw[1]), float(raw[2]))
        return default

    # Heightfield-driven normal maps
    if pat in ("normal_noise", "normal_brick", "normal_grid"):
        strength = float(params.get("strength", 2.5))
        hf = [0.0] * (w * h)
        for y in range(h):
            v = y / h
            for x in range(w):
                u = x / w
                if pat == "normal_noise":
                    val = _fbm_tile(u, v, scale, 4, seed)
                elif pat == "normal_brick":
                    row = int(v * scale)
                    bu = (u * (scale * 0.5) + (0.5 if row % 2 else 0.0)) % 1.0
                    bv = (v * scale) % 1.0
                    mortar = 0.07
                    in_brick = 1.0 if (bu > mortar and bu < 1.0 - mortar and bv > mortar and bv < 1.0 - mortar) else 0.0
                    val = in_brick * (0.85 + 0.15 * _fbm_tile(u, v, scale * 2, 2, seed))
                else:  # normal_grid
                    gu = (u * scale) % 1.0
                    gv = (v * scale) % 1.0
                    val = 0.0 if (gu < 0.06 or gv < 0.06) else 1.0
                hf[y * w + x] = val

        rgba = bytearray(w * h * 4)
        for y in range(h):
            ym = ((y - 1) % h) * w
            yp = ((y + 1) % h) * w
            yc = y * w
            for x in range(w):
                xm = (x - 1) % w
                xp = (x + 1) % w
                dx = (hf[yc + xp] - hf[yc + xm]) * strength
                dy = (hf[yp + x] - hf[ym + x]) * strength
                nx, ny, nz = -dx, -dy, 1.0
                inv = 1.0 / math.sqrt(nx * nx + ny * ny + nz * nz)
                off = (yc + x) * 4
                rgba[off] = _clamp_u8(nx * inv * 0.5 + 0.5)
                rgba[off + 1] = _clamp_u8(ny * inv * 0.5 + 0.5)
                rgba[off + 2] = _clamp_u8(nz * inv * 0.5 + 0.5)
                rgba[off + 3] = 255
        return encode_png(bytes(rgba), w, h)

    c1 = col("color_a", (0.82, 0.82, 0.82))
    c2 = col("color_b", (0.25, 0.25, 0.28))
    rgba = bytearray(w * h * 4)

    for y in range(h):
        v = y / h
        for x in range(w):
            u = x / w
            if pat == "checker":
                odd = (int(u * scale) + int(v * scale)) & 1
                r, g, b_ = c2 if odd else c1
            elif pat == "grid":
                gu = (u * scale) % 1.0
                gv = (v * scale) % 1.0
                line = gu < 0.05 or gv < 0.05
                r, g, b_ = c2 if line else c1
            elif pat == "brick":
                brick_a = col("color_a", (0.58, 0.26, 0.19))
                mortar_c = col("color_b", (0.68, 0.66, 0.62))
                row = int(v * scale)
                bu = (u * (scale * 0.5) + (0.5 if row % 2 else 0.0)) % 1.0
                bv = (v * scale) % 1.0
                if bu < 0.05 or bu > 0.95 or bv < 0.08 or bv > 0.92:
                    r, g, b_ = mortar_c
                else:
                    tint = 0.86 + 0.28 * _hash2(int(u * scale * 0.5), row, seed)
                    n = 0.92 + 0.16 * _tile_noise(u, v, scale * 2, seed)
                    r, g, b_ = brick_a[0] * tint * n, brick_a[1] * tint * n, brick_a[2] * tint * n
            elif pat == "wood":
                wood_a = col("color_a", (0.48, 0.31, 0.18))
                wood_b = col("color_b", (0.31, 0.18, 0.09))
                grain = math.sin((u * scale * 2.0 + _fbm_tile(u, v, scale, 3, seed) * 1.4) * math.pi) * 0.5 + 0.5
                r, g, b_ = _lerp_rgb(wood_b, wood_a, grain)
            elif pat == "grass":
                ga = col("color_a", (0.22, 0.44, 0.16))
                gb = col("color_b", (0.34, 0.55, 0.21))
                n = _fbm_tile(u, v, scale, 4, seed)
                r, g, b_ = _lerp_rgb(ga, gb, n)
            elif pat == "stone":
                sa = col("color_a", (0.42, 0.43, 0.44))
                sb = col("color_b", (0.62, 0.61, 0.59))
                n = _fbm_tile(u, v, scale, 4, seed)
                r, g, b_ = _lerp_rgb(sa, sb, n)
            elif pat == "sand":
                sa = col("color_a", (0.76, 0.67, 0.48))
                sb = col("color_b", (0.85, 0.77, 0.58))
                ripple = 0.5 + 0.5 * math.sin((v * scale + _fbm_tile(u, v, scale, 2, seed) * 0.8) * 2.0 * math.pi)
                n = ripple * 0.4 + _fbm_tile(u, v, scale * 2, 3, seed) * 0.6
                r, g, b_ = _lerp_rgb(sa, sb, n)
            elif pat == "metal":
                ma = col("color_a", (0.65, 0.67, 0.70))
                mb = col("color_b", (0.78, 0.80, 0.83))
                streak = _tile_noise(u * 0.25, v, scale * 4, seed)
                r, g, b_ = _lerp_rgb(ma, mb, streak)
            elif pat == "noise":
                n = _fbm_tile(u, v, scale, 4, seed)
                r, g, b_ = _lerp_rgb(c2, c1, n)
            elif pat == "gradient":
                r, g, b_ = _lerp_rgb(c1, c2, v)
            else:
                raise ValueError(
                    f"Unsupported texture pattern '{pattern}'. Choose from: checker, grid, brick, wood, grass, "
                    "stone, sand, metal, noise, gradient, normal_noise, normal_brick, normal_grid."
                )
            off = (y * w + x) * 4
            rgba[off] = _clamp_u8(r)
            rgba[off + 1] = _clamp_u8(g)
            rgba[off + 2] = _clamp_u8(b_)
            rgba[off + 3] = 255
    return encode_png(bytes(rgba), w, h)
