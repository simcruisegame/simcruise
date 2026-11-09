"""Compare 16 fitted directions against a finer city survey query, using remaining angles.
Dependencies are Python, NumPy and Shapely. The game and its helper do not use these.
"""
import argparse, json, math, subprocess, urllib.parse, urllib.request
from pathlib import Path
import numpy as np
from shapely.geometry import Polygon, LineString
from shapely import get_coordinates
parser=argparse.ArgumentParser()
parser.add_argument('cache',type=Path);parser.add_argument('output',type=Path);parser.add_argument('--bun',default='bun')
args=parser.parse_args();args.cache.mkdir(exist_ok=True,parents=True)
P=Path(__file__).resolve().parents[2];W=40075016.68557849/65536
endpoint='https://services1.arcgis.com/WnzC35krSYGuYov4/arcgis/rest/services/Tree_Canopy_2024/FeatureServer/0/query';samples=[]
for x,y in [(19822,24236),(19823,24236),(19824,24237)]:
 out=subprocess.check_output([args.bun,str(P/'helper'/'atlas.mjs'),'canopy',str(args.cache),str(x),str(y)])
 rows=[r for r in json.loads(out)['trees'] if len(r)==13 and r[12]['source']=='cambridge-2024']
 if not rows:raise RuntimeError('Municipal source unavailable')
 q=dict(where='1=1',objectIds=','.join(str(r[12]['objectId']) for r in rows),returnGeometry='true',outFields='OBJECTID',outSR=3857,maxAllowableOffset='.09',geometryPrecision=2,f='json')
 with urllib.request.urlopen(endpoint+'?'+urllib.parse.urlencode(q),timeout=12) as response:
  body=response.read(512*1024+1);assert len(body)<512*1024
 fine={f['attributes']['OBJECTID']:f['geometry']['rings'] for f in json.loads(body)['features']}
 scale=1/math.cosh(math.pi*(1-2*(y+.5)/65536));angles=np.arange(16)*math.tau/16;unit=np.c_[np.cos(angles),np.sin(angles)]
 for row in rows:
  profile=row[12];rings=fine.get(profile['objectId'])
  if not rings or len(rings)!=1:continue
  anchor=[W*(x-32768+row[0]),W*(32768-y-row[1])];poly=Polygon((np.array(rings[0])-anchor)*[scale,-scale])
  if not poly.is_valid or poly.area<1:continue
  radius=np.asarray(profile['radii']);survey=Polygon(unit*radius[:,None]);nominal=Polygon(unit*(row[3]*.5))
  held=[]
  for a in angles+math.pi/16:
   cut=poly.boundary.intersection(LineString([(0,0),(50*math.cos(a),50*math.sin(a))]));held.append(np.linalg.norm(get_coordinates(cut),axis=1).min())
  iou=lambda g:g.intersection(poly).area/g.union(poly).area
  samples.append(dict(tile=[x,y],id=profile['objectId'],baseline_iou=iou(nominal),survey_iou=iou(survey),baseline_abs_error_m=float(np.abs(np.asarray(held)-row[3]*.5).mean()),survey_abs_error_m=float(np.abs(np.asarray(held)-.5*(radius+np.roll(radius,-1))).mean())))
summary={k:float(np.mean([r[k] for r in samples])) for k in ('baseline_iou','survey_iou','baseline_abs_error_m','survey_abs_error_m')};summary.update(count=len(samples),improved=sum(r['survey_iou']>r['baseline_iou'] for r in samples),records=samples)
args.output.write_text(json.dumps(summary,indent=2));print({k:v for k,v in summary.items() if k!='records'})
