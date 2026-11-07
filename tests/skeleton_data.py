# SPDX-License-Identifier: MPL-2.0
import base64
import copy
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

original = json.loads(Path(sys.argv[2]).read_text())
with tempfile.TemporaryDirectory(prefix='afterlight-skin-data-') as temporary:
    path = Path(temporary) / 'bad.gltf'
    for failure in ('bad_joint', 'zero_weights', 'nan_weight', 'duplicate_joint', 'nonaffine_inverse', 'too_many_joints'):
        gltf = copy.deepcopy(original)
        primitive = gltf['meshes'][0]['primitives'][0]
        data = bytearray(base64.b64decode(gltf['buffers'][0]['uri'].split(',')[1]))
        def offset(accessor): return gltf['bufferViews'][gltf['accessors'][accessor]['bufferView']]['byteOffset']
        mode = '--reject-model'
        if failure == 'bad_joint': struct.pack_into('<H', data, offset(primitive['attributes']['JOINTS_0']), 2)
        elif failure in ('zero_weights', 'nan_weight'): struct.pack_into('<f', data, offset(primitive['attributes']['WEIGHTS_0']), 0 if failure == 'zero_weights' else float('nan'))
        else:
            mode = '--reject-skin'
            if failure == 'duplicate_joint': gltf['skins'][0]['joints'] = [1,1]
            elif failure == 'nonaffine_inverse': struct.pack_into('<f', data, offset(gltf['skins'][0]['inverseBindMatrices']) + 15 * 4, 0)
            else: gltf['skins'][0]['joints'] = [1] * 257
        gltf['buffers'][0]['uri'] = 'data:application/octet-stream;base64,' + base64.b64encode(data).decode()
        path.write_text(json.dumps(gltf))
        result = subprocess.run([sys.argv[1], mode, str(path)], capture_output=True, text=True, timeout=10)
        assert result.returncode == 0, f'{failure}: {result.stdout}\n{result.stderr}'

    gltf = copy.deepcopy(original)
    data = bytearray(base64.b64decode(gltf['buffers'][0].pop('uri').split(',')[1]))

    def view(payload, stride=None):
        data.extend(bytes(-len(data) % 4))
        result = len(gltf['bufferViews'])
        entry = {'buffer': 0, 'byteOffset': len(data), 'byteLength': len(payload)}
        if stride is not None:
            entry['byteStride'] = stride
        gltf['bufferViews'].append(entry)
        data.extend(payload)
        return result

    def write_glb(path):
        gltf['buffers'][0]['byteLength'] = len(data)
        document = json.dumps(gltf).encode()
        document += b' ' * (-len(document) % 4)
        binary = bytes(data) + bytes(-len(data) % 4)
        chunks = struct.pack('<I4s', len(document), b'JSON') + document
        chunks += struct.pack('<I4s', len(binary), b'BIN\0') + binary
        path.write_bytes(struct.pack('<4sII', b'glTF', 2, 12 + len(chunks)) + chunks)

    skin = gltf['skins'][0]
    extras = list(range(len(gltf['nodes']), len(gltf['nodes']) + 78))
    gltf['nodes'].extend({'name': f'Joint {index}'} for index in extras)
    gltf['nodes'][1]['children'].extend(extras)
    skin['joints'].extend(extras)
    gltf['animations'][0]['channels'].append({'sampler': 0, 'target': {'node': extras[-1], 'path': 'rotation'}})
    bind = gltf['accessors'][skin['inverseBindMatrices']]
    old_bind = gltf['bufferViews'][bind['bufferView']]
    matrices = bytes(data[old_bind['byteOffset']:old_bind['byteOffset'] + old_bind['byteLength']])
    identity = struct.pack('<16f', *[int(i % 5 == 0) for i in range(16)])
    bind.update(bufferView=view(matrices + identity * len(extras)), count=80)

    attributes = gltf['meshes'][0]['primitives'][0]['attributes']
    gltf['accessors'][attributes['JOINTS_0']]['bufferView'] = view(struct.pack('<4H', 0, 1, 2, 3) * 4)
    gltf['accessors'][attributes['WEIGHTS_0']]['bufferView'] = view(
        struct.pack('<4f', *[value / 255 for value in (13, 46, 8, 15)]) * 4)
    sparse_indices = view(bytes((1, 2)))
    sparse_joints = view(struct.pack('<8H', 74, 73, 72, 71, 79, 78, 77, 76))
    for name, kind, base, stride, sparse, normalized in (
        ('JOINTS_1', 5123, struct.pack('<4H4x', 75, 74, 73, 72), 12, sparse_joints, False),
        ('WEIGHTS_1', 5121, bytes((87, 50, 25, 11, 0, 0, 0, 0)), 8,
         view(bytes((85, 52, 27, 9, 86, 51, 26, 10))), True),
    ):
        attributes[name] = len(gltf['accessors'])
        gltf['accessors'].append({
            'bufferView': view(base * 4, stride), 'componentType': kind, 'count': 4,
            'type': 'VEC4', 'normalized': normalized,
            'sparse': {'count': 2, 'indices': {'bufferView': sparse_indices, 'componentType': 5121},
                       'values': {'bufferView': sparse}},
        })
    path = Path(temporary) / 'wide-rig.glb'
    write_glb(path)
    subprocess.run([sys.argv[1], '--wide-glb', str(path)], check=True, timeout=10)

    weights = attributes.pop('WEIGHTS_1')
    write_glb(path)
    subprocess.run([sys.argv[1], '--reject-model', str(path)], check=True, timeout=10)
    attributes['WEIGHTS_1'] = weights
    # Even a discarded influence must refer to an existing joint.
    struct.pack_into('<H', data, gltf['bufferViews'][sparse_joints]['byteOffset'] + 14, 99)
    write_glb(path)
    subprocess.run([sys.argv[1], '--reject-model', str(path)], check=True, timeout=10)
print('Skeleton data: bad joints, zero/nonfinite weights, inverse binds and palette limits rejected.')
