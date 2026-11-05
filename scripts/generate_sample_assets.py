# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Regenerate Afterlight's original GLB beacon and glTF regression fixtures."""
from pathlib import Path
import base64, copy, json, math, struct
ROOT=Path(__file__).resolve().parents[1]
def glb(path, document, data):
    document=copy.deepcopy(document);document['buffers'][0].pop('uri',None)
    text=json.dumps(document,separators=(',',':')).encode();text+=b' '*((-len(text))%4);data+=b'\0'*((-len(data))%4)
    result=struct.pack('<III',0x46546c67,2,12+8+len(text)+8+len(data))+struct.pack('<II',len(text),0x4e4f534a)+text+struct.pack('<II',len(data),0x004e4942)+data
    path.write_bytes(result)
# Analytic triangle with nested transforms, a mirrored instance, UV0, normals,
# normalized byte colors and ushort indices. Same data in three container forms.
data=struct.pack('<9f',0,0,0,1,0,0,0,1,0)+struct.pack('<9f',*( [0,0,1]*3 ))+struct.pack('<6f',0,0,1,0,0,1)+bytes([255,0,0,255,0,255,0,255,0,0,255,255])+struct.pack('<3H',0,1,2)
doc={'asset':{'version':'2.0'},'scene':0,'scenes':[{'nodes':[0,2]}],'nodes':[{'translation':[1,2,3],'children':[1]},{'mesh':0,'scale':[2,1,1]},{'mesh':0,'scale':[-1,1,1]}],
 'buffers':[{'uri':'triangle.bin','byteLength':len(data)}],'bufferViews':[{'buffer':0,'byteOffset':offset,'byteLength':length} for offset,length in [(0,36),(36,36),(72,24),(96,12),(108,6)]],
 'accessors':[{'bufferView':i,'componentType':kind,'count':3,'type':typ,**({'normalized':True} if i==3 else {})} for i,kind,typ in [(0,5126,'VEC3'),(1,5126,'VEC3'),(2,5126,'VEC2'),(3,5121,'VEC4'),(4,5123,'SCALAR')]],
 'materials':[{'pbrMetallicRoughness':{'baseColorFactor':[.5,.6,.7,1]}}],'meshes':[{'primitives':[{'attributes':{'POSITION':0,'NORMAL':1,'TEXCOORD_0':2,'COLOR_0':3},'indices':4,'material':0}]}]}
doc['accessors'][0].update(min=[0,0,0],max=[1,1,0])
fixtures=ROOT/'tests/assets';(fixtures/'triangle.bin').write_bytes(data);(fixtures/'triangle.gltf').write_text(json.dumps(doc,indent=2)+'\n');glb(fixtures/'triangle.glb',doc,data)
embedded=copy.deepcopy(doc);embedded['buffers'][0]['uri']='data:application/octet-stream;base64,'+base64.b64encode(data).decode();(fixtures/'triangle-embedded.gltf').write_text(json.dumps(embedded,indent=2)+'\n')
# A stepped 16-sided beacon built from static colored triangle rings.
positions=[];colors=[]
levels=[(0,.9),(.2,.9),(.2,.65),(1.6,.65),(1.6,.85),(1.85,.85),(1.85,.4),(2.7,.4),(3.15,0)]
for ring,((y0,r0),(y1,r1)) in enumerate(zip(levels,levels[1:])):
    color=[.18,.38,.55] if ring<3 else [.95,.64,.18]
    for i in range(16):
        a=i*math.tau/16;b=(i+1)*math.tau/16
        points=[(r0*math.cos(a),y0,r0*math.sin(a)),(r1*math.cos(a),y1,r1*math.sin(a)),(r1*math.cos(b),y1,r1*math.sin(b)),(r0*math.cos(b),y0,r0*math.sin(b))]
        for corner in [0,1,2,0,2,3]:positions.extend(points[corner]);colors.extend(color)
data=struct.pack('<%sf'%len(positions),*positions)+struct.pack('<%sf'%len(colors),*colors);length=len(positions)*4
beacon={'asset':{'version':'2.0','generator':'Afterlight original procedural beacon'},'scene':0,'scenes':[{'nodes':[0]}],'nodes':[{'mesh':0}],
 'buffers':[{'byteLength':len(data)}],'bufferViews':[{'buffer':0,'byteLength':length},{'buffer':0,'byteOffset':length,'byteLength':len(colors)*4}],
 'accessors':[{'bufferView':i,'componentType':5126,'count':len(positions)//3,'type':'VEC3'} for i in [0,1]],'meshes':[{'primitives':[{'attributes':{'POSITION':0,'COLOR_0':1}}]}]}
beacon['accessors'][0].update(min=[-.9,0,-.9],max=[.9,3.15,.9])
glb(ROOT/'sandbox/assets/beacon.glb',beacon,data)

# Original short WAV cues; regenerating does not require an audio device.
import wave
for name,duration,tones in [('jump',.16,[330,660]),('land',.12,[75,50]),('checkpoint',.3,[523.25,659.25]),('finish',.65,[523.25,659.25,783.99,1046.5])]:
    rate=22050;count=int(rate*duration);samples=[]
    for i in range(count):
        t=i/rate;phase=i/count;frequency=tones[min(len(tones)-1,int(phase*len(tones)))];envelope=min(1,t/.008)*max(0,1-phase)**1.4
        samples.append(struct.pack('<h',int(14000*envelope*math.sin(2*math.pi*frequency*t))))
    with wave.open(str(ROOT/'sandbox/assets'/(name+'.wav')),'wb') as output:
        output.setnchannels(1);output.setsampwidth(2);output.setframerate(rate);output.writeframes(b''.join(samples))

# Rich static model: two materials/primitives, parent transforms and three maps.
import zlib

def png(rgba):
    def chunk(kind, payload):return struct.pack('>I',len(payload))+kind+payload+struct.pack('>I',zlib.crc32(kind+payload)&0xffffffff)
    return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',1,1,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(b'\0'+bytes(rgba)))+chunk(b'IEND',b'')

rich=copy.deepcopy(doc)
rich['nodes']=[{'name':'Assembly','translation':[1,2,3],'children':[1,2]},{'name':'Twin panels','mesh':0,'scale':[2,1,1]},{'name':'Mirrored panels','mesh':0,'translation':[3,0,0],'scale':[-1,1,1]}]
rich['scenes']=[{'nodes':[0]}]
rich['images']=[{'uri':'base.png'},{'uri':'normal.png'},{'uri':'surface.png'}];rich['textures']=[{'source':i} for i in range(3)]
rich['materials']=[{'name':'Paint','pbrMetallicRoughness':{'baseColorFactor':[.8,.6,.4,1],'roughnessFactor':.8,'metallicFactor':.5,'baseColorTexture':{'index':0},'metallicRoughnessTexture':{'index':2}},'normalTexture':{'index':1,'scale':.75}}, {'name':'Trim','pbrMetallicRoughness':{'baseColorFactor':[.2,.8,.4,1],'roughnessFactor':.3,'metallicFactor':.9}}]
rich['meshes'][0]['primitives'].append(copy.deepcopy(rich['meshes'][0]['primitives'][0]));rich['meshes'][0]['primitives'][1]['material']=1
for name,pixel in [('base',[220,120,40,255]),('normal',[128,128,255,255]),('surface',[255,128,64,255])]:
    (fixtures/(name+'.png')).write_bytes(png(pixel))
(fixtures/'rich.gltf').write_text(json.dumps(rich,indent=2)+'\n')
embedded=copy.deepcopy(rich)
for image in embedded['images']:image['uri']='data:image/png;base64,'+base64.b64encode((fixtures/image['uri']).read_bytes()).decode()
triangle_data=(fixtures/'triangle.bin').read_bytes()
glb(fixtures/'rich.glb',embedded,triangle_data)

(fixtures/'green.png').write_bytes(png([20,245,50,255]))
