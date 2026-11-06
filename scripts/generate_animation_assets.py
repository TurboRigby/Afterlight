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
