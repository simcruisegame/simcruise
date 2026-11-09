from pathlib import Path
import json,laspy,numpy as np
import shapely
from shapely.geometry import shape,Polygon
import argparse
p=argparse.ArgumentParser();p.add_argument('work',type=Path);N=p.parse_args().work
header,*records=[json.loads(l) for l in next((N/'output').glob('*.jsonl')).read_text().splitlines()]
tr=header['transform'];points=np.load(N/'held.npz');X,Y,Z=points['x'],points['y'],points['z'];laser=laspy.read(N/'train.las')
polys={r['properties']['bid']:shape(r['geometry']) for r in json.loads((N/'footprints.geojson').read_text())['features']}
res={}
for r in records:
 v=np.array(r['vertices'])*np.array(tr['scale'])+np.array(tr['translate']);ident=r['id'];roof=polys[ident].buffer(-.45)
 traininside=shapely.contains_xy(roof,laser.x,laser.y)&(np.asarray(laser.classification)==1)
 ground=shapely.contains_xy(roof.buffer(8),laser.x,laser.y)&(np.asarray(laser.classification)==2)
 groundlevel=np.median(np.array(laser.z)[ground]);traininside&=np.array(laser.z)>groundlevel+2
 flat=np.median(np.array(laser.z)[traininside])
 inside=shapely.contains_xy(roof,X,Y)&(points['cls']==1)&(Z>groundlevel+2)
 pred=np.full(X.shape,np.nan);n=0;peaks=[]
 for obj in r['CityObjects'].values():
  if obj['type']!='BuildingPart':continue
  for g in obj['geometry']:
   if g['lod']!='2.2':continue
   for shell,attrs in zip(g['boundaries'],g['semantics']['values']):
    for loops,idx in zip(shell,attrs):
     if g['semantics']['surfaces'][idx]['type']!='RoofSurface':continue
     outer=v[loops[0]];poly=Polygon(outer[:,:2],[v[h][:,:2] for h in loops[1:]])
     if poly.is_empty or not poly.is_valid:continue
     n+=1;peaks.append(np.max(outer[:,2]));dx,dy=outer[:,:2].mean(axis=0)
     a,b,c=np.linalg.lstsq(np.column_stack([outer[:,0]-dx,outer[:,1]-dy,np.ones(len(outer))]),outer[:,2],rcond=None)[0]
     keep=shapely.contains_xy(poly,X,Y)&inside;pred[keep]=a*(X[keep]-dx)+b*(Y[keep]-dy)+c
 valid=np.isfinite(pred);actual=Z[valid];automatic=np.abs(pred[valid]-actual);control=np.abs(actual-flat)
 metric=lambda v:[round(x,3) for x in np.percentile(v,[50,90,95])]
 res[ident]={'held_returns':int(inside.sum()),'covered':int(valid.sum()),'roof_planes':n,'flat_median_p90_p95_m':metric(control),'automatic_median_p90_p95_m':metric(automatic),'automatic_peak_m_agl':round(max(peaks)-groundlevel,2),'surveyed_99th_m_agl':round(np.percentile(actual,99)-groundlevel,2)}
print(json.dumps(res,indent=2));(N/'results.json').write_text(json.dumps(res,indent=2))
