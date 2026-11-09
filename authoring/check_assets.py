"""Run from the source root: python3 authoring/check_assets.py. Standard library only."""
import json, struct,itertools,sys
from pathlib import Path
root=Path(__file__).resolve().parents[1]
def inspect(path):
 raw=path.read_bytes()
 assert raw[:4]==b'glTF' and struct.unpack_from('<I',raw,4)[0]==2, 'Needs glTF 2.0'
 n=struct.unpack_from('<I',raw,12)[0]
 g=json.loads(raw[20:20+n]);nodes={x['name']:x for x in g['nodes'] if 'mesh' in x};return g,nodes
def bounds(g,node):
 assert all(k not in node for k in ['matrix','translation','rotation','scale']), 'Bake wheel part transforms to chassis space'
 bounds=[g['accessors'][p['attributes']['POSITION']] for p in g['meshes'][node['mesh']]['primitives']]
 return [min(b['min'][i] for b in bounds) for i in range(3)],[max(b['max'][i] for b in bounds) for i in range(3)]
s=json.loads((root/'authoring/car-assets.json').read_text())
g,n=inspect(root/s['mesh']);fixed=set(s['fixed_stop_lights']);dynamic=set()
for wheel,center in s['wheel_centers'].items():
 for part in s['rotating_parts']:
  name=wheel+'_'+part;dynamic.add(name);node=n[name];lo,hi=bounds(g,node)
  assert abs((hi[1]+lo[1])/2-center[1])<.025 and abs((hi[2]+lo[2])/2-center[2])<.025, name+' is off-center'
  if part=='rubber':
   assert abs((hi[0]+lo[0])/2-center[0])<.015 and abs((hi[1]-lo[1])/2-s['wheel_radius_m'])<.015
for name in fixed:
 assert name in n and name not in dynamic
 lo,hi=bounds(g,n[name]);assert lo[2]>1.4 and lo[1]>.4, name+' is not mounted to the rear body'
# Check the active scene tree, as removed glTF nodes can remain in the file.
active = {g["nodes"][i]["name"] for i in g["scenes"][g.get("scene", 0)]["nodes"]}
assert fixed <= active
assert active.isdisjoint({"body_lamp", "body_amber"})
for side in (-1, 1):
    name = f"Headlamp {side}"
    assert name in active and f"Bumper side marker {side}" in active
    part = g["meshes"][n[name]["mesh"]]["primitives"][0]
    material = g["materials"][part["material"]]["pbrMetallicRoughness"]
    assert "baseColorTexture" in material and "TEXCOORD_0" in part["attributes"]
    lo, hi = bounds(g, n[name])
    assert hi[2] < -1.4 and hi[0]-lo[0] > .4 and hi[1]-lo[1] > .12
assert {"Upper bumper", "Lower bumper", "Rolled intake throat", "Radiator core"} <= active
cabin, cabin_nodes = inspect(root/s["close_cockpit"]["mesh"])
assert {"Wheel", "TachNeedle", "SpeedNeedle", "OilNeedle"} <= cabin_nodes.keys()
assert {"Speed", "Tach", "Pointer", "LCD"} <= {m.get("name") for m in cabin["materials"]}
mask=(root/s['brake_mask']).read_bytes();assert mask[:8]==b'\x89PNG\r\n\x1a\n' and struct.unpack_from('>II',mask,16)==(384,160)
levels=[]
for i in range(3):
 g,n=inspect(root/('game/art/oak%d.glb'%i));prims=[p for m in g['meshes'] for p in m['primitives']]
 assert all(p.get('mode',4)==4 and 'TEXCOORD_0' in p['attributes'] and 'POSITION' in p['attributes'] for p in prims)
 b=g['accessors'][prims[0]['attributes']['POSITION']];dims=[mx-mn for mx,mn in zip(b['max'],b['min'])];assert min(dims)>6 and abs(b['min'][1])<.15
 levels.append(sum(g['accessors'][p['indices']]['count']//3 for p in prims))
assert levels[0]>levels[1]>levels[2]
print('PASS:',len(dynamic),'rotating wheel parts;',len(fixed),'fixed stop lights; oak triangle counts near/mid/far:',levels)
