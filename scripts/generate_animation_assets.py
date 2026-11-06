# SPDX-License-Identifier: MPL-2.0
"""Generate original, dependency-free glTF animation regression assets."""
import base64
import json
from pathlib import Path
import struct

root = Path(__file__).resolve().parents[1]
data = bytearray()
views, accessors = [], []

def accessor(values, components, kind, component=5126):
    while len(data) % 4:
        data.append(0)
    start = len(data)
    for value in values:
        data.extend(struct.pack('<' + {5126: 'f', 5123: 'H', 5121: 'B'}[component] * components, *value))
    views.append({'buffer': 0, 'byteOffset': start, 'byteLength': len(data) - start})
    accessors.append({'bufferView': len(views) - 1, 'componentType': component, 'count': len(values), 'type': kind})
    return len(accessors) - 1

positions = accessor([(-.4, 0, 0), (.4, 0, 0), (.4, 2, 0), (-.4, 2, 0)], 3, 'VEC3')
accessors[positions].update(min=[-.4, 0, 0], max=[.4, 2, 0])
indices = accessor([(0,), (1,), (2,), (0,), (2,), (3,)], 1, 'SCALAR', 5123)
times = accessor([(0,), (1,), (2,)], 1, 'SCALAR')
accessors[times].update(min=[0], max=[2])
translation = accessor([(0, 0, 0), (2, 0, 0), (0, 0, 0)], 3, 'VEC3')
rotation = accessor([(0, 0, 0, 1), (0, 0, .707106781, .707106781), (0, 0, 0, 1)], 4, 'VEC4')
scales = accessor([(1, 1, 1), (2, 1, 1), (1, 1, 1)], 3, 'VEC3')
cubic = accessor([(0, 0, 0), (0, 0, 0), (2, 0, 0), (0, 0, 0), (2, 0, 0), (0, 0, 0), (-2, 0, 0), (0, 0, 0), (0, 0, 0)], 3, 'VEC3')
gltf = {'asset': {'version': '2.0', 'generator': 'Afterlight original animation fixtures'},
        'scene': 0, 'scenes': [{'nodes': [1]}],
        'nodes': [{'name': 'Arm', 'mesh': 0}, {'name': 'Mechanism', 'children': [0], 'translation': [0, 1, 0]}],
        'meshes': [{'primitives': [{'attributes': {'POSITION': positions}, 'indices': indices}]}],
        'animations': [
            {'name': 'Slide and turn', 'samplers': [{'input': times, 'output': translation}, {'input': times, 'output': rotation}, {'input': times, 'output': scales}],
             'channels': [{'sampler': 0, 'target': {'node': 0, 'path': 'translation'}}, {'sampler': 1, 'target': {'node': 0, 'path': 'rotation'}}, {'sampler': 2, 'target': {'node': 0, 'path': 'scale'}}]},
            {'name': 'Step', 'samplers': [{'input': times, 'output': translation, 'interpolation': 'STEP'}], 'channels': [{'sampler': 0, 'target': {'node': 0, 'path': 'translation'}}]},
            {'name': 'Cubic', 'samplers': [{'input': times, 'output': cubic, 'interpolation': 'CUBICSPLINE'}], 'channels': [{'sampler': 0, 'target': {'node': 0, 'path': 'translation'}}]}],
        'buffers': [{'byteLength': len(data), 'uri': 'data:application/octet-stream;base64,' + base64.b64encode(data).decode()}],
        'bufferViews': views, 'accessors': accessors}
(root / 'tests/assets/animated.gltf').write_text(json.dumps(gltf, indent=2) + '\n')
# Invalid key order must fail without retaining a partially allocated model.
bad = json.loads(json.dumps(gltf))
bad['animations'][0]['channels'].append(bad['animations'][0]['channels'][0])
(root / 'tests/assets/animated-invalid.gltf').write_text(json.dumps(bad, indent=2) + '\n')

# Two-joint ribbon, authored from scratch. Four influences use normalized weights.
joints = accessor([(0, 0, 0, 0), (0, 0, 0, 0), (1, 0, 0, 0), (1, 0, 0, 0)], 4, 'VEC4', 5123)
weights = accessor([(1, 0, 0, 0)] * 4, 4, 'VEC4')
inverse = accessor([(1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1), (1,0,0,0,0,1,0,0,0,0,1,0,0,-1,0,1)], 16, 'MAT4')
wave = accessor([(0,0,0,1), (0,0,.5,.866025404), (0,0,0,1)], 4, 'VEC4')
skinned = {'asset': {'version': '2.0', 'generator': 'Afterlight original two-joint ribbon'},
           'scene': 0, 'scenes': [{'nodes': [3]}],
           'nodes': [{'name': 'Ribbon', 'mesh': 0, 'skin': 0}, {'name': 'Hip', 'children': [2]}, {'name': 'Tip', 'translation': [0,1,0]}, {'name': 'Character', 'children': [0,1]}],
           'skins': [{'joints': [1,2], 'inverseBindMatrices': inverse, 'skeleton': 1}],
           'meshes': [{'primitives': [{'attributes': {'POSITION': positions, 'JOINTS_0': joints, 'WEIGHTS_0': weights}, 'indices': indices}]}],
           'animations': [{'name': 'Wave', 'samplers': [{'input': times, 'output': wave}], 'channels': [{'sampler': 0, 'target': {'node': 2, 'path': 'rotation'}}]}],
           'buffers': [{'byteLength': len(data), 'uri': 'data:application/octet-stream;base64,' + base64.b64encode(data).decode()}], 'bufferViews': views, 'accessors': accessors}
(root / 'tests/assets/skinned.gltf').write_text(json.dumps(skinned, indent=2) + '\n')
normalized = json.loads(json.dumps(skinned))
normalized_weights = accessor([(255,0,0,0)] * 4, 4, 'VEC4', 5121)
accessors[normalized_weights]['normalized'] = True
normalized['meshes'][0]['primitives'][0]['attributes']['WEIGHTS_0'] = normalized_weights
normalized.update(buffers=[{'byteLength': len(data), 'uri': 'data:application/octet-stream;base64,' + base64.b64encode(data).decode()}], bufferViews=views, accessors=accessors)
(root / 'tests/assets/skinned-normalized.gltf').write_text(json.dumps(normalized, indent=2) + '\n')

# The first demo uses the analytically tested node animation fixture.
(root / 'sandbox/assets/mechanism.gltf').write_text((root / 'tests/assets/animated.gltf').read_text())

# Harder original model: nine joints, mixed torso weights, three clips and colors.
import math
positions_robot, normals_robot, colors_robot, joints_robot, weights_robot, indices_robot = [], [], [], [], [], []
centers = [(0,1.1,0),(0,1.6,0),(0,2.4,0),(-.8,2,0),(-1.3,2,0),(.8,2,0),(1.3,2,0),(-.23,.4,0),(.23,.4,0)]
scales_robot = [(.4,.22,.25),(.42,.45,.25),(.28,.28,.28),(.25,.12,.14),(.25,.11,.13),(.25,.12,.14),(.25,.11,.13),(.15,.4,.18),(.15,.4,.18)]
colors = [(.16,.28,.42,1),(.16,.65,.8,1),(.9,.72,.28,1),(.22,.48,.6,1),(.8,.48,.18,1),(.22,.48,.6,1),(.8,.48,.18,1),(.18,.3,.45,1),(.18,.3,.45,1)]
faces = [((1,0,0),[(1,-1,-1),(1,1,-1),(1,1,1),(1,-1,1)]),((-1,0,0),[(-1,-1,1),(-1,1,1),(-1,1,-1),(-1,-1,-1)]),((0,1,0),[(-1,1,-1),(-1,1,1),(1,1,1),(1,1,-1)]),((0,-1,0),[(-1,-1,1),(-1,-1,-1),(1,-1,-1),(1,-1,1)]),((0,0,1),[(-1,-1,1),(1,-1,1),(1,1,1),(-1,1,1)]),((0,0,-1),[(1,-1,-1),(-1,-1,-1),(-1,1,-1),(1,1,-1)])]
for bone, (center, size, color) in enumerate(zip(centers, scales_robot, colors)):
    for normal, corners in faces:
        base = len(positions_robot)
        for corner in corners:
            position = tuple(center[i] + corner[i] * size[i] for i in range(3))
            positions_robot.append(position);normals_robot.append(normal);colors_robot.append(color)
            joints_robot.append((bone,0,0,0))
            weights_robot.append((.8,.2,0,0) if bone == 1 and corner[1] < 0 else (1,0,0,0))
        indices_robot.extend([(base,),(base+1,),(base+2,),(base,),(base+2,),(base+3,)])
robot_position = accessor(positions_robot, 3, 'VEC3')
accessors[robot_position].update(min=[min(v[i] for v in positions_robot) for i in range(3)], max=[max(v[i] for v in positions_robot) for i in range(3)])
robot_normal = accessor(normals_robot, 3, 'VEC3')
robot_color = accessor(colors_robot, 4, 'VEC4')
robot_joint = accessor(joints_robot, 4, 'VEC4', 5123)
robot_weight = accessor(weights_robot, 4, 'VEC4')
robot_index = accessor(indices_robot, 1, 'SCALAR', 5123)
bone_world = [(0,1.1,0),(0,1.6,0),(0,2.4,0),(-.55,2,0),(-1.1,2,0),(.55,2,0),(1.1,2,0),(-.23,.6,0),(.23,.6,0)]
robot_inverse = accessor([(1,0,0,0,0,1,0,0,0,0,1,0,-p[0],-p[1],-p[2],1) for p in bone_world], 16, 'MAT4')
robot_times = accessor([(0,),(.5,),(1,),(1.5,),(2,)], 1, 'SCALAR')
accessors[robot_times].update(min=[0], max=[2])
def quat(x=0,y=0,z=0):
    sx,cx,sy,cy,sz,cz=math.sin(x/2),math.cos(x/2),math.sin(y/2),math.cos(y/2),math.sin(z/2),math.cos(z/2)
    return (sx*cy*cz-cx*sy*sz,cx*sy*cz+sx*cy*sz,cx*cy*sz-sx*sy*cz,cx*cy*cz+sx*sy*sz)
def clip(name, tracks):
    samplers, channels = [], []
    for node, path, values in tracks:
        output = accessor(values, 4 if path == 'rotation' else 3, 'VEC4' if path == 'rotation' else 'VEC3')
        channels.append({'sampler': len(samplers), 'target': {'node': node, 'path': path}})
        samplers.append({'input': robot_times, 'output': output})
    return {'name': name, 'samplers': samplers, 'channels': channels}
idle = clip('Idle', [(1,'translation',[(0,1.1,0),(0,1.14,0),(0,1.1,0),(0,1.14,0),(0,1.1,0)]),(4,'rotation',[quat(z=1.1)]*5),(6,'rotation',[quat(z=-1.1)]*5),(3,'rotation',[quat(y=v) for v in [0,.12,0,-.12,0]])])
walk = clip('Walk', [(8,'rotation',[quat(x=v) for v in [-.6,.6,-.6,.6,-.6]]),(9,'rotation',[quat(x=v) for v in [.6,-.6,.6,-.6,.6]]),(4,'rotation',[quat(x=v,z=1.1) for v in [.4,-.4,.4,-.4,.4]]),(6,'rotation',[quat(x=v,z=-1.1) for v in [-.4,.4,-.4,.4,-.4]]),(1,'translation',[(0,1.1,0),(0,1.18,0),(0,1.1,0),(0,1.18,0),(0,1.1,0)])])
wave_clip = clip('Wave', [(4,'rotation',[quat(z=1.1)]*5),(6,'rotation',[quat(z=.7)]*5),(7,'rotation',[quat(z=v) for v in [.2,.9,.2,-.3,.2]]),(3,'rotation',[quat(y=.2)]*5)])
robot = {'asset': {'version':'2.0','generator':'Afterlight original nine-joint character'}, 'scene':0,'scenes':[{'nodes':[10]}],
         'nodes':[{'name':'Body','mesh':0,'skin':0}, {'name':'Hips','translation':[0,1.1,0],'children':[2,8,9]}, {'name':'Torso','translation':[0,.5,0],'children':[3,4,6]}, {'name':'Head','translation':[0,.8,0]}, {'name':'Left arm','translation':[-.55,.4,0],'children':[5]}, {'name':'Left forearm','translation':[-.55,0,0]}, {'name':'Right arm','translation':[.55,.4,0],'children':[7]}, {'name':'Right forearm','translation':[.55,0,0]}, {'name':'Left leg','translation':[-.23,-.5,0]}, {'name':'Right leg','translation':[.23,-.5,0]}, {'name':'Rig','children':[0,1]}],
         'skins':[{'name':'Robot rig','joints':list(range(1,10)),'inverseBindMatrices':robot_inverse,'skeleton':1}],
         'materials':[{'name':'Brushed color','pbrMetallicRoughness':{'metallicFactor':.35,'roughnessFactor':.42}}],
         'meshes':[{'primitives':[{'attributes':{'POSITION':robot_position,'NORMAL':robot_normal,'COLOR_0':robot_color,'JOINTS_0':robot_joint,'WEIGHTS_0':robot_weight},'indices':robot_index,'material':0}]}],
         'animations':[idle,walk,wave_clip], 'buffers':[{'byteLength':len(data),'uri':'data:application/octet-stream;base64,'+base64.b64encode(data).decode()}], 'bufferViews':views,'accessors':accessors}
(root / 'sandbox/assets/character.gltf').write_text(json.dumps(robot, indent=2)+'\n')

# Playable variant keeps the 0.10 display asset and its clip indices unchanged.
run = clip('Run', [(8,'rotation',[quat(x=v) for v in [-1,1,-1,1,-1]]),(9,'rotation',[quat(x=v) for v in [1,-1,1,-1,1]]),(4,'rotation',[quat(x=v,z=1.1) for v in [.8,-.8,.8,-.8,.8]]),(6,'rotation',[quat(x=v,z=-1.1) for v in [-.8,.8,-.8,.8,-.8]]),(1,'translation',[(0,1.1,0),(0,1.28,0),(0,1.1,0),(0,1.28,0),(0,1.1,0)])])
jump = clip('Jump', [(8,'rotation',[quat(x=-.6)]*5),(9,'rotation',[quat(x=.35)]*5),(4,'rotation',[quat(z=.6)]*5),(6,'rotation',[quat(z=-.6)]*5)])
fall = clip('Fall', [(8,'rotation',[quat(x=.2)]*5),(9,'rotation',[quat(x=-.2)]*5),(4,'rotation',[quat(z=.1)]*5),(6,'rotation',[quat(z=-.1)]*5)])
# Rig node 10 moves forward at 2/5 m/s. The driver strips XZ when extraction is enabled.
for animation, distance in [(walk,4),(run,10)]:
    output=accessor([(0,0,-distance*t/2) for t in [0,.5,1,1.5,2]],3,'VEC3')
    animation['channels'].append({'sampler':len(animation['samplers']),'target':{'node':10,'path':'translation'}})
    animation['samplers'].append({'input':robot_times,'output':output})
playable = json.loads(json.dumps(robot))
playable['animations'] = [idle,walk,run,jump,fall]
for animation in playable['animations'][1:3]:
    animation['extras'] = {'afterlight_events': [{'name': 'foot_left', 'time': .25}, {'name': 'foot_right', 'time': .75}, {'name': 'foot_left', 'time': 1.25}, {'name': 'foot_right', 'time': 1.75}]}

playable.update(buffers=[{'byteLength':len(data),'uri':'data:application/octet-stream;base64,'+base64.b64encode(data).decode()}],bufferViews=views,accessors=accessors)
(root / 'sandbox/assets/locomotion.gltf').write_text(json.dumps(playable, indent=2)+'\n')
