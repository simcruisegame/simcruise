"""Bundle sourced ground-layer geometry and a bark material; run from project root."""
import json,struct,io
from pathlib import Path
import requests,numpy as np
from PIL import Image
D=Path('game/data')
def get(url):
 r=requests.get(url,timeout=18);r.raise_for_status();return r.content
def spec(name):return json.loads(get('https://api.polyhaven.com/files/'+name))
def image(source):
 variants=source['1k'];uri=(variants.get('jpg') or variants.get('png'))['url'];return Image.open(io.BytesIO(get(uri)))
shrub=spec('shrub_03');s=shrub['gltf']['1k']['gltf'];d=json.loads(get(s['url']));binfile=get(s['include']['shrub_03.bin']['url'])
im=image(shrub['Diffuse']).convert('RGBA');im.putalpha(image(shrub['Alpha']).convert('L'));im.thumbnail((512,512))
im.save(D/'shrub-color.png')
d['nodes']=[d['nodes'][0]];d['scenes']=[{'nodes':[0]}];d['scene']=0
d['meshes']=d['meshes'][:1];d['materials']=[{'doubleSided':True}];d['images']=[];d['textures']=[];d.pop('samplers',None);d['buffers']=[dict(byteLength=len(binfile))]
p=d['meshes'][0]['primitives'][0];a=d['accessors'][p['attributes']['POSITION']];print('plant extent',a['min'],a['max'])
j=json.dumps(d,separators=(',',':')).encode();j+=b' '*(-len(j)%4);binfile+=bytes(-len(binfile)%4)
(D/'roadside-shrub.meshdata').write_bytes(struct.pack('<4sII',b'glTF',2,28+len(j)+len(binfile))+struct.pack('<I4s',len(j),b'JSON')+j+struct.pack('<I4s',len(binfile),b'BIN\0')+binfile)
bark=spec('jolcham_oak_bark_01');im=image(bark['Diffuse']).convert('RGB');im.thumbnail((512,512));im.save(D/'tree-bark.jpg',quality=89)
