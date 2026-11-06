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
            else: gltf['skins'][0]['joints'] = [1] * 65
        gltf['buffers'][0]['uri'] = 'data:application/octet-stream;base64,' + base64.b64encode(data).decode()
        path.write_text(json.dumps(gltf))
        result = subprocess.run([sys.argv[1], mode, str(path)], capture_output=True, text=True, timeout=10)
        assert result.returncode == 0, f'{failure}: {result.stdout}\n{result.stderr}'
print('Skeleton data: bad joints, zero/nonfinite weights, inverse binds and palette limits rejected.')
