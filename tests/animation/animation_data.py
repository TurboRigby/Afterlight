# SPDX-License-Identifier: MPL-2.0
"""Reject malformed animation buffers before they can reach renderer matrices."""
import base64
import copy
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

original = json.loads(Path(sys.argv[2]).read_text())
def buffer_value(gltf, accessor, index, value):
    data = bytearray(base64.b64decode(gltf['buffers'][0]['uri'].split(',')[1]))
    offset = gltf['bufferViews'][gltf['accessors'][accessor]['bufferView']]['byteOffset']
    struct.pack_into('<f', data, offset + index * 4, value)
    gltf['buffers'][0]['uri'] = 'data:application/octet-stream;base64,' + base64.b64encode(data).decode()

with tempfile.TemporaryDirectory(prefix='afterlight-animation-data-') as directory:
    path = Path(directory) / 'bad.gltf'
    for failure in ('nan_time', 'duplicate_time', 'nonunit_rotation', 'nan_translation', 'wrong_input_type', 'wrong_cubic_count', 'matrix_target','event_negative','event_end','event_name','event_count','event_type'):
        gltf = copy.deepcopy(original)
        samplers = gltf['animations'][0]['samplers']
        if failure == 'nan_time': buffer_value(gltf, samplers[0]['input'], 1, float('nan'))
        elif failure == 'duplicate_time': buffer_value(gltf, samplers[0]['input'], 1, 0)
        elif failure == 'nonunit_rotation': buffer_value(gltf, samplers[1]['output'], 3, 0)
        elif failure == 'nan_translation': buffer_value(gltf, samplers[0]['output'], 0, float('nan'))
        elif failure == 'wrong_input_type': gltf['accessors'][samplers[0]['input']]['type'] = 'VEC2'
        elif failure == 'wrong_cubic_count': gltf['animations'][2]['samplers'][0]['output'] = samplers[0]['output']
        elif failure.startswith('event_'):
            events=[{'name':'foot','time':.5}]
            if failure=='event_negative':events[0]['time']=-1
            elif failure=='event_end':events[0]['time']=2
            elif failure=='event_name':events[0]['name']='x'*32
            elif failure=='event_count':events*=65
            else:events={}
            gltf['animations'][0]['extras']={'afterlight_events':events}
        else: gltf['nodes'][0]['matrix'] = [1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1]
        path.write_text(json.dumps(gltf))
        result = subprocess.run([sys.argv[1], '--reject', str(path)], capture_output=True, text=True, timeout=10)
        assert result.returncode == 0, f'{failure}: {result.stdout}\n{result.stderr}'
print('Animation data: malformed event metadata, time, quaternion, interpolation, accessor and matrix-track inputs rejected.')
