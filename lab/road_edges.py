from pathlib import Path
import math,json,requests,io,zlib
from collections import Counter
from PIL import Image,ImageFilter
import numpy as np
from rasterio.features import rasterize
from rasterio.transform import from_bounds
from shapely.geometry import box,shape,mapping
import os
CACHE=Path(os.environ.get('SIMCRUISE_CACHE','.cache'))
CACHE.mkdir(exist_ok=True,parents=True)
D=Path('game/data');counts=Counter();catalog={}
url='https://services3.arcgis.com/TsynfzBSE6sXfoLq/arcgis/rest/services/Planimetric_ProdA/FeatureServer/1/query'
for f in D.glob('pave-*.png'):
 _,x,y=f.stem.split('-');x,y=int(x),int(y);w=40075016.68557849/65536;left=(x-32768)*w;top=(32768-y)*w;e=(left,top-w,left+w,top)
 t=json.loads((D/f'{x}_{y}.json').read_text())
 ch=Image.open(f).convert('RGB');red,green,_=ch.split();alpha=np.full((768,768),127,np.uint8)
 if t['region']=='Raven':
  req=dict(f='geojson',geometry=','.join(map(str,e)),geometryType='esriGeometryEnvelope',inSR=3857,outSR=3857,spatialRel='esriSpatialRelIntersects',outFields='OBJECTID,AreaType',where='AreaType in (1,2,3,4,5,6,7,9)',returnGeometry='true',maxAllowableOffset=.28)
  file=CACHE/f'edges-{x}-{y}.json'
  if not file.exists():
   r=requests.get(url,params=req,timeout=18);r.raise_for_status();j=r.json();assert 'features' in j and not j.get('exceededTransferLimit');file.write_text(json.dumps(j))
  features=json.loads(file.read_text())['features'];draw=[]
  for ft in features:
   g=shape(ft['geometry']).intersection(box(*e));k=ft['properties']['AreaType'];catalog[ft['properties']['OBJECTID']]=k
   if not g.is_empty:draw.append((mapping(g),k))
  if draw:
   a=rasterize(draw,out_shape=(768,768),fill=0,transform=from_bounds(*e,768,768),dtype='uint8')
   if np.any(a==1):red=Image.fromarray((np.isin(a,[1,3])*255).astype('uint8')).filter(ImageFilter.GaussianBlur(.45))
   alpha[np.isin(a,[2,6,9])]=191;alpha[np.isin(a,[4,5,7])]=255
 cloudfile=CACHE/f'c2-{x}-{y}'
 if cloudfile.exists():
  elev=np.frombuffer(zlib.decompress(cloudfile.read_bytes()),np.uint8).reshape((512,512))
  blue=Image.fromarray(np.uint8(np.clip((elev.astype(float)-1)/5,0,1)*255)).resize(ch.size,Image.Resampling.BILINEAR).filter(ImageFilter.GaussianBlur(2))
 else:
  base=Image.new('L',(192,192));from PIL import ImageDraw
  brush=ImageDraw.Draw(base)
  for tree in t['trees']:
   u,v,h=tree[:3];r=h*.15;brush.ellipse((u*192-r,v*192-r,u*192+r,v*192+r),fill=255)
  blue=base.resize(ch.size,Image.Resampling.BILINEAR).filter(ImageFilter.GaussianBlur(3))
 Image.merge('RGBA',(red,green,blue,Image.fromarray(alpha))).save(f)
print('Unique county surface polygons',Counter(catalog.values()),flush=True)
j=requests.get('https://api.polyhaven.com/files/forest_leaves_03',timeout=8).json()
im=Image.open(io.BytesIO(requests.get(j['Diffuse']['1k']['jpg']['url'],timeout=15).content)).convert('RGB');im.thumbnail((768,768));im.save(D/'leaf-litter.jpg',quality=88)
