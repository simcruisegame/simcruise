"""Measure radial support rays through the shipped leaf-cluster triangles of each tree LOD."""
from pathlib import Path
import json,struct
import numpy as np
import shapely
from shapely.geometry import LineString
from shapely import Polygon
from shapely.ops import unary_union
import argparse
parser=argparse.ArgumentParser();parser.add_argument('output',type=Path);options=parser.parse_args()
P=Path(__file__).resolve().parents[2]/'game';base={}
def gltf(path):
 data=path.read_bytes();n=struct.unpack_from('<I',data,12)[0];doc=json.loads(data[20:20+n]);buf=memoryview(data)[28+n:]
 def get(i):
  a=doc['accessors'][i];b=doc['bufferViews'][a['bufferView']];k={'VEC3':3,'SCALAR':1}[a['type']];t={5126:'<f4',5123:'<u2',5125:'<u4'}[a['componentType']];return np.frombuffer(buf,dtype=t,count=a['count']*k,offset=b.get('byteOffset',0)+a.get('byteOffset',0)).reshape(-1,k)
 mats=[]
 for prim in doc['meshes'][0]['primitives']:
  vertices=get(prim['attributes']['POSITION']);triangles=get(prim['indices']).reshape(-1,3);mats.append((vertices,triangles))
 return mats
for sp in ['oak','pine','leaf']:
 for lod in range(3):
  vs,idx=gltf(P/'data'/f'crown-{sp}-{lod}.meshdata')[-1]
  flat=vs[:,[0,2]]
  polygons=shapely.polygons(flat[idx]);outer=unary_union(polygons)
  angle=np.arange(72)*np.pi/36
  rad=[]
  for a in angle:
   ray=LineString([[0.,0.],[1.5*np.cos(a),1.5*np.sin(a)]])
   pts=shapely.get_coordinates(outer.intersection(ray));rad.append(np.round(np.linalg.norm(pts,axis=1).max(),4))
  base[f'{sp}-{lod}']=list(rad)
options.output.write_text(json.dumps(base))
