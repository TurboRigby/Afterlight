# SPDX-License-Identifier: MPL-2.0
import bpy
import bmesh
import math
import os
import sys

def clear_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)

def export_obj(filepath):
    # Select all mesh objects
    bpy.ops.object.select_all(action='SELECT')
    # Export OBJ
    bpy.ops.wm.obj_export(
        filepath=filepath,
        export_selected_objects=True,
        export_materials=True,
        export_normals=True,
        export_uv=True,
        apply_modifiers=True,
        forward_axis='NEGATIVE_Z',
        up_axis='Y'
    )

def create_reactor_core(output_path):
    clear_scene()
    
    # Base cylinder
    bpy.ops.mesh.primitive_cylinder_add(radius=2.0, depth=1.2, vertices=32, location=(0, 0, 0.6))
    base = bpy.context.active_object
    base.name = "Core_Base"
    
    # Cooling fins around base
    for i in range(8):
        angle = i * (2.0 * math.pi / 8.0)
        x = math.cos(angle) * 2.2
        y = math.sin(angle) * 2.2
        bpy.ops.mesh.primitive_cube_add(size=1.0, location=(x, y, 0.7))
        fin = bpy.context.active_object
        fin.scale = (0.1, 0.7, 0.8)
        fin.rotation_euler = (0, 0, angle + math.pi/2)
        bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
    
    # Core mid section (plasma containment cylinder)
    bpy.ops.mesh.primitive_cylinder_add(radius=1.4, depth=3.5, vertices=32, location=(0, 0, 3.0))
    mid = bpy.context.active_object
    mid.name = "Core_PlasmaChamber"
    
    # Magnetic rings around plasma cylinder
    for z in [2.0, 3.0, 4.0]:
        bpy.ops.mesh.primitive_torus_add(major_radius=1.65, minor_radius=0.15, major_segments=32, minor_segments=12, location=(0, 0, z))
        ring = bpy.context.active_object
        ring.name = f"Core_Ring_{z}"
        
    # Upper collar
    bpy.ops.mesh.primitive_cylinder_add(radius=1.8, depth=0.8, vertices=32, location=(0, 0, 5.1))
    collar = bpy.context.active_object
    collar.name = "Core_Collar"
    
    # 4 Stabilizer pylons
    for i in range(4):
        angle = i * (math.pi / 2.0)
        x = math.cos(angle) * 1.5
        y = math.sin(angle) * 1.5
        bpy.ops.mesh.primitive_cylinder_add(radius=0.18, depth=4.2, vertices=16, location=(x, y, 3.2))
        pylon = bpy.context.active_object
        pylon.name = f"Core_Pylon_{i}"
        
    # Top emitter spire
    bpy.ops.mesh.primitive_cone_add(radius1=1.5, radius2=0.3, depth=1.8, vertices=32, location=(0, 0, 6.4))
    spire = bpy.context.active_object
    spire.name = "Core_Spire"
    
    # Crown emitter tip
    bpy.ops.mesh.primitive_cylinder_add(radius=0.45, depth=0.6, vertices=16, location=(0, 0, 7.4))
    crown = bpy.context.active_object
    crown.name = "Core_EmitterTip"
    
    # Join all into single mesh
    bpy.ops.object.select_all(action='SELECT')
    bpy.context.view_layer.objects.active = base
    bpy.ops.object.join()
    base.name = "ReactorCore"
    
    # Add bevel modifier for crisp specular highlights
    bev = base.modifiers.new(name="Bevel", type='BEVEL')
    bev.width = 0.03
    bev.segments = 2
    
    export_obj(output_path)
    print(f"Exported reactor core to {output_path}")

def create_conduit_pillar(output_path):
    clear_scene()
    
    # Heavy base
    bpy.ops.mesh.primitive_cube_add(size=1.0, location=(0, 0, 0.4))
    base = bpy.context.active_object
    base.scale = (2.2, 2.2, 0.8)
    bpy.ops.object.transform_apply(scale=True)
    
    # Main pillar shaft
    bpy.ops.mesh.primitive_cube_add(size=1.0, location=(0, 0, 4.0))
    shaft = bpy.context.active_object
    shaft.scale = (1.6, 1.6, 6.4)
    bpy.ops.object.transform_apply(scale=True)
    
    # Capital top
    bpy.ops.mesh.primitive_cube_add(size=1.0, location=(0, 0, 7.4))
    cap = bpy.context.active_object
    cap.scale = (2.2, 2.2, 0.6)
    bpy.ops.object.transform_apply(scale=True)
    
    # Conduit pipes along the 4 recessed faces
    pipes = []
    offsets = [(0, 0.95), (0, -0.95), (0.95, 0), (-0.95, 0)]
    for ox, oy in offsets:
        bpy.ops.mesh.primitive_cylinder_add(radius=0.12, depth=6.8, vertices=16, location=(ox, oy, 4.0))
        p = bpy.context.active_object
        pipes.append(p)
        # Mounting collars
        for cz in [2.0, 4.0, 6.0]:
            bpy.ops.mesh.primitive_cube_add(size=1.0, location=(ox * 0.9, oy * 0.9, cz))
            c = bpy.context.active_object
            c.scale = (0.35, 0.35, 0.15)
            bpy.ops.object.transform_apply(scale=True)
            pipes.append(c)
            
    # Join everything
    bpy.ops.object.select_all(action='SELECT')
    bpy.context.view_layer.objects.active = shaft
    bpy.ops.object.join()
    shaft.name = "ConduitPillar"
    
    bev = shaft.modifiers.new(name="Bevel", type='BEVEL')
    bev.width = 0.04
    bev.segments = 2
    
    export_obj(output_path)
    print(f"Exported conduit pillar to {output_path}")

def create_catwalk(output_path):
    clear_scene()
    
    # Deck slab (3m wide, 6m long, 0.25m thick)
    bpy.ops.mesh.primitive_cube_add(size=1.0, location=(0, 0, 0.125))
    deck = bpy.context.active_object
    deck.scale = (3.0, 6.0, 0.25)
    bpy.ops.object.transform_apply(scale=True)
    
    # Under-deck longitudinal I-beams (2 beams)
    for bx in [-1.2, 1.2]:
        bpy.ops.mesh.primitive_cube_add(size=1.0, location=(bx, 0, -0.2))
        beam = bpy.context.active_object
        beam.scale = (0.2, 6.0, 0.4)
        bpy.ops.object.transform_apply(scale=True)
        
    # Cross beams (3 cross beams)
    for by in [-2.5, 0, 2.5]:
        bpy.ops.mesh.primitive_cube_add(size=1.0, location=(0, by, -0.25))
        cbeam = bpy.context.active_object
        cbeam.scale = (2.6, 0.2, 0.3)
        bpy.ops.object.transform_apply(scale=True)
        
    # Handrail stanchions (posts) along both sides
    rail_x = [-1.45, 1.45]
    posts_y = [-2.8, -1.4, 0.0, 1.4, 2.8]
    for rx in rail_x:
        for py in posts_y:
            bpy.ops.mesh.primitive_cylinder_add(radius=0.04, depth=1.1, vertices=12, location=(rx, py, 0.75))
            bpy.context.active_object
        # Top handrail
        bpy.ops.mesh.primitive_cylinder_add(radius=0.04, depth=5.8, vertices=12, location=(rx, 0, 1.25))
        top_rail = bpy.context.active_object
        top_rail.rotation_euler = (math.pi/2, 0, 0)
        bpy.ops.object.transform_apply(rotation=True)
        # Mid rail
        bpy.ops.mesh.primitive_cylinder_add(radius=0.03, depth=5.8, vertices=12, location=(rx, 0, 0.75))
        mid_rail = bpy.context.active_object
        mid_rail.rotation_euler = (math.pi/2, 0, 0)
        bpy.ops.object.transform_apply(rotation=True)
        # Kick plate (toe board)
        bpy.ops.mesh.primitive_cube_add(size=1.0, location=(rx, 0, 0.3))
        plate = bpy.context.active_object
        plate.scale = (0.04, 5.8, 0.15)
        bpy.ops.object.transform_apply(scale=True)
        
    # Join into single catwalk mesh
    bpy.ops.object.select_all(action='SELECT')
    bpy.context.view_layer.objects.active = deck
    bpy.ops.object.join()
    deck.name = "CatwalkPlatform"
    
    export_obj(output_path)
    print(f"Exported catwalk to {output_path}")

def create_energy_cell(output_path):
    clear_scene()
    
    # Hexagonal central crystal body
    bpy.ops.mesh.primitive_cylinder_add(radius=0.18, depth=0.6, vertices=6, location=(0, 0, 0.35))
    core = bpy.context.active_object
    core.name = "Cell_Core"
    
    # End caps (top and bottom)
    for cz in [0.06, 0.64]:
        bpy.ops.mesh.primitive_cylinder_add(radius=0.22, depth=0.12, vertices=12, location=(0, 0, cz))
        cap = bpy.context.active_object
        cap.name = f"Cell_Cap_{cz}"
        
    # Support cage struts (3 vertical bars)
    for i in range(3):
        angle = i * (2.0 * math.pi / 3.0)
        x = math.cos(angle) * 0.20
        y = math.sin(angle) * 0.20
        bpy.ops.mesh.primitive_cylinder_add(radius=0.02, depth=0.55, vertices=8, location=(x, y, 0.35))
        
    # Handle on top
    bpy.ops.mesh.primitive_torus_add(major_radius=0.12, minor_radius=0.025, location=(0, 0, 0.75))
    handle = bpy.context.active_object
    handle.rotation_euler = (math.pi/2, 0, 0)
    bpy.ops.object.transform_apply(rotation=True)
    
    bpy.ops.object.select_all(action='SELECT')
    bpy.context.view_layer.objects.active = core
    bpy.ops.object.join()
    core.name = "EnergyCell"
    
    bev = core.modifiers.new(name="Bevel", type='BEVEL')
    bev.width = 0.015
    bev.segments = 2
    
    export_obj(output_path)
    print(f"Exported energy cell to {output_path}")

def create_relay_pedestal(output_path):
    clear_scene()
    
    # Base octagonal plinth
    bpy.ops.mesh.primitive_cylinder_add(radius=0.8, depth=0.3, vertices=8, location=(0, 0, 0.15))
    base = bpy.context.active_object
    
    # Pedestal body (slight taper)
    bpy.ops.mesh.primitive_cone_add(radius1=0.7, radius2=0.55, depth=0.7, vertices=8, location=(0, 0, 0.65))
    body = bpy.context.active_object
    
    # Top collar rim
    bpy.ops.mesh.primitive_cylinder_add(radius=0.6, depth=0.15, vertices=8, location=(0, 0, 1.05))
    rim = bpy.context.active_object
    
    # Docking receptacle socket (hollow cavity in center)
    bpy.ops.mesh.primitive_cylinder_add(radius=0.25, depth=0.2, vertices=6, location=(0, 0, 1.15))
    socket = bpy.context.active_object
    
    # Console terminal readout screen (angled on one side)
    bpy.ops.mesh.primitive_cube_add(size=1.0, location=(0, 0.45, 1.15))
    screen = bpy.context.active_object
    screen.scale = (0.4, 0.1, 0.25)
    screen.rotation_euler = (-math.pi/6, 0, 0)
    bpy.ops.object.transform_apply(rotation=True, scale=True)
    
    bpy.ops.object.select_all(action='SELECT')
    bpy.context.view_layer.objects.active = base
    bpy.ops.object.join()
    base.name = "RelayPedestal"
    
    bev = base.modifiers.new(name="Bevel", type='BEVEL')
    bev.width = 0.02
    bev.segments = 2
    
    export_obj(output_path)
    print(f"Exported relay pedestal to {output_path}")

def create_barrier_gate(output_path):
    clear_scene()
    
    # Left and right vertical door pillars
    for x in [-1.8, 1.8]:
        bpy.ops.mesh.primitive_cube_add(size=1.0, location=(x, 0, 2.2))
        pillar = bpy.context.active_object
        pillar.scale = (0.6, 0.8, 4.4)
        bpy.ops.object.transform_apply(scale=True)
        
    # Top header beam
    bpy.ops.mesh.primitive_cube_add(size=1.0, location=(0, 0, 4.5))
    header = bpy.context.active_object
    header.scale = (4.2, 0.8, 0.6)
    bpy.ops.object.transform_apply(scale=True)
    
    # Blast door slab
    bpy.ops.mesh.primitive_cube_add(size=1.0, location=(0, 0, 2.0))
    door = bpy.context.active_object
    door.scale = (3.0, 0.3, 3.8)
    bpy.ops.object.transform_apply(scale=True)
    
    # Horizontal reinforcement ribs
    for z in [0.8, 1.6, 2.4, 3.2]:
        bpy.ops.mesh.primitive_cube_add(size=1.0, location=(0, 0.2, z))
        rib = bpy.context.active_object
        rib.scale = (2.8, 0.15, 0.15)
        bpy.ops.object.transform_apply(scale=True)
        
    bpy.ops.object.select_all(action='SELECT')
    bpy.context.view_layer.objects.active = door
    bpy.ops.object.join()
    door.name = "BarrierGate"
    
    export_obj(output_path)
    print(f"Exported barrier gate to {output_path}")

def create_industrial_lamp(output_path):
    clear_scene()
    
    # Base mount plate
    bpy.ops.mesh.primitive_cube_add(size=1.0, location=(0, 0, 0.05))
    plate = bpy.context.active_object
    plate.scale = (0.5, 0.5, 0.1)
    bpy.ops.object.transform_apply(scale=True)
    
    # Support bracket arm
    bpy.ops.mesh.primitive_cylinder_add(radius=0.06, depth=0.4, vertices=12, location=(0, 0.15, 0.25))
    arm = bpy.context.active_object
    arm.rotation_euler = (math.pi/4, 0, 0)
    bpy.ops.object.transform_apply(rotation=True)
    
    # Lamp housing hood
    bpy.ops.mesh.primitive_cone_add(radius1=0.35, radius2=0.2, depth=0.3, vertices=16, location=(0, 0.35, 0.4))
    hood = bpy.context.active_object
    hood.rotation_euler = (math.pi/4, 0, 0)
    bpy.ops.object.transform_apply(rotation=True)
    
    # Bulb / lens
    bpy.ops.mesh.primitive_uv_sphere_add(radius=0.18, segments=16, ring_count=12, location=(0, 0.35, 0.35))
    bulb = bpy.context.active_object
    
    bpy.ops.object.select_all(action='SELECT')
    bpy.context.view_layer.objects.active = plate
    bpy.ops.object.join()
    plate.name = "IndustrialLamp"
    
    export_obj(output_path)
    print(f"Exported industrial lamp to {output_path}")

def main():
    dest_dir = sys.argv[-1] if len(sys.argv) > 1 and not sys.argv[-1].startswith("-") else "assets"
    os.makedirs(dest_dir, exist_ok=True)
    
    create_reactor_core(os.path.join(dest_dir, "reactor_core.obj"))
    create_conduit_pillar(os.path.join(dest_dir, "conduit_pillar.obj"))
    create_catwalk(os.path.join(dest_dir, "catwalk.obj"))
    create_energy_cell(os.path.join(dest_dir, "energy_cell.obj"))
    create_relay_pedestal(os.path.join(dest_dir, "relay_pedestal.obj"))
    create_barrier_gate(os.path.join(dest_dir, "barrier_gate.obj"))
    create_industrial_lamp(os.path.join(dest_dir, "industrial_lamp.obj"))
    print("All models successfully built and exported!")

if __name__ == "__main__":
    main()
