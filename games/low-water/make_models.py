# SPDX-License-Identifier: MPL-2.0
"""Generate the Low Water props with Blender 5's glTF exporter."""
import math
from pathlib import Path

import bpy

OUT = Path(globals().get("TIDE_OUTPUT", Path(__file__).resolve().parent / "assets"))
OUT.mkdir(parents=True, exist_ok=True)


def material(name, color, metal=0.0, roughness=0.45, glow=0.0):
    m = bpy.data.materials.new("LW " + name)
    m.diffuse_color = (*color, 1)
    m.use_nodes = True
    m.use_backface_culling = True
    shader = m.node_tree.nodes.get("Principled BSDF")
    shader.inputs["Base Color"].default_value = (*color, 1)
    shader.inputs["Metallic"].default_value = metal
    shader.inputs["Roughness"].default_value = roughness
    shader.inputs["Emission Color"].default_value = (*color, 1)
    shader.inputs["Emission Strength"].default_value = glow
    return m


def finish(name, surface):
    ob = bpy.context.object
    ob.name = name
    ob.data.materials.append(surface)
    return ob


def box(name, p, size, surface, bevel=0.035):
    bpy.ops.mesh.primitive_cube_add(size=1, location=p)
    ob = finish(name, surface)
    ob.scale = size
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    if bevel:
        mod = ob.modifiers.new("Machined edges", "BEVEL")
        mod.width = bevel
        mod.segments = 2
        ob.modifiers.new("Weighted corner normals", "WEIGHTED_NORMAL")
    return ob


def disc(name, p, radius, depth, surface):
    bpy.ops.mesh.primitive_cylinder_add(vertices=32, radius=radius, depth=depth, location=p,
                                       rotation=(math.pi / 2, 0, 0))
    return finish(name, surface)


def ring(name, p, radius, tube, surface):
    bpy.ops.mesh.primitive_torus_add(location=p, rotation=(math.pi / 2, 0, 0),
                                    major_radius=radius, minor_radius=tube,
                                    major_segments=40, minor_segments=8)
    ob = finish(name, surface)
    for face in ob.data.polygons:
        face.use_smooth = True
    return ob


def legend(body, p, size, surface, inverted=False):
    bpy.ops.object.text_add(location=p, rotation=(math.pi / 2, 0, 0))
    ob = finish(body, surface)
    ob.data.body = body
    ob.data.align_x = "CENTER"
    ob.data.align_y = "CENTER"
    ob.data.size = size
    ob.data.extrude = 0.008
    if inverted:
        ob.scale.y = -1
    bpy.ops.object.convert(target="MESH")


def make():
    previous = bpy.context.window.scene
    work = bpy.data.scenes.new("Low Water prop workshop")
    bpy.context.window.scene = work
    steel = material("oiled steel", (0.22, 0.27, 0.29), 0.78, 0.26)
    enamel = material("aged teal enamel", (0.028, 0.15, 0.16), 0.28, 0.37)
    ceramic = material("bone ceramic", (0.43, 0.43, 0.34), 0.06, 0.56)
    rubber = material("carbon", (0.027, 0.032, 0.035), 0.0, 0.65)
    amber = material("warning amber", (1.0, 0.46, 0.09), 0.0, 0.4, 1.8)
    ice = material("ice diode", (0.16, 0.76, 0.95), 0.0, 0.28, 1.6)
    white = material("calibration phosphor", (0.82, 0.95, 1.0), 0.0, 0.4, 5.0)
    copper = material("oxide red", (0.39, 0.12, 0.046), 0.36, 0.46)

    def export(name):
        bpy.ops.export_scene.gltf(filepath=str(OUT / (name + ".glb")), export_format="GLB",
                                  use_active_scene=True, export_apply=True,
                                  export_animations=False, export_cameras=False, export_lights=False)
        for ob in list(work.objects):
            bpy.data.objects.remove(ob, do_unlink=True)

    try:
        box("Valve housing", (0, 0.14, 0), (1.0, .45, .86), enamel)
        disc("Spindle", (0, -.12, 0), .19, .48, steel)
        ring("Cast handwheel", (0, -.34, 0), .48, .046, copper)
        ring("Machined hub", (0, -.38, 0), .136, .04, steel)
        for step in range(6):
            a = step * math.tau / 6
            bar = box("Radial spoke", (.29 * math.sin(a), -.34, .29 * math.cos(a)),
                      (.076, .085, .32), steel, .016)
            bar.rotation_euler.y = a
        disc("Center bolt", (0, -.435, 0), .081, .07, ceramic)
        export("valve")

        box("Welded pedestal", (0, .08, .48), (.70, .63, .94), enamel)
        box("Key station", (0, 0, 1.03), (1.08, .62, .55), steel, .06)
        box("Recessed panel", (0, -.33, 1.05), (.91, .065, .45), rubber, .025)
        box("Readout slot", (-.15, -.376, 1.15), (.45, .045, .105), ice, .017)
        for r in range(2):
            for c in range(4):
                box("Ceramic key", (-.34 + .14 * c, -.38, 1.026 - .092 * r),
                    (.091, .044, .065), ceramic, .012)
        disc("Interlock", (.31, -.399, 1.01), .086, .06, amber)
        legend("LOCK 03", (0, -.376, .892), .078, ceramic)
        export("keypad")

        box("Board enclosure", (0, .09, 0), (3.6, .16, .99), rubber, .046)
        for i, digit in enumerate("629"):
            x = (i - 1) * 1.12
            box("Ceramic bezel", (x, -.005, 0), (.98, .08, .83), steel, .042)
            box("Number recess", (x, -.062, 0), (.83, .075, .69), rubber, .023)
            legend(digit, (x, -.118, 0), .83, white, inverted=True)
        for side in (-1, 1):
            box("Optical pilot", (side * 1.69, -.044, 0), (.052, .05, .68), ice, .013)
        export("calibration")

        box("Carrier footing", (0, 0, .16), (2.2, 1.0, .3), steel)
        box("Archive housing", (0, .02, .92), (1.7, .86, 1.4), enamel, .078)
        box("Tape transport", (0, -.438, 1.16), (1.51, .1, .74), rubber, .035)
        for x in (-.41, .41):
            disc("Tape reel", (x, -.535, 1.19), .29, .092, steel)
            ring("Reel flange", (x, -.598, 1.19), .25, .026, copper)
            for step in range(5):
                a = step * math.tau / 5
                disc("Reel aperture", (x + .155 * math.cos(a), -.593, 1.19 + .155 * math.sin(a)),
                     .052, .026, rubber)
            disc("Drive boss", (x, -.625, 1.19), .074, .065, ceramic)
        box("Archive label", (0, -.465, .60), (1.22, .042, .176), ceramic)
        legend("WARD 09 / MASTER ROLL", (0, -.496, .60), .073, rubber)
        box("Record lamp", (.62, -.449, .83), (.18, .08, .06), amber)
        export("archive")
    finally:
        bpy.context.window.scene = previous
        bpy.data.scenes.remove(work)
        for m in (steel, enamel, ceramic, rubber, amber, ice, white, copper):
            if m.users == 0:
                bpy.data.materials.remove(m)


make()
