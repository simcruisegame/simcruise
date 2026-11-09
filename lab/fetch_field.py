from pathlib import Path
import json, requests, math,time,concurrent.futures
from PIL import Image
from io import BytesIO
from pyproj import Transformer
D=Path('lab/field');D.mkdir(exist_ok=True)
v='https://vginmaps.vdem.virginia.gov/arcgis/rest/services/'
w=40075016.68557849/65536; half=20037508.342789244
areas={'Chula':(18584,25420,18588,25425),'Raven':(18635,25418,18638,25422)}
def tiles():
 return [(n,x,y) for n,(x0,y0,x1,y1) in areas.items() for x in range(x0,x1+1) for y in range(y0,y1+1)]
def extent(x,y,X,Y):
 return (x*w-half,half-(Y+1)*w,(X+1)*w-half,half-y*w)
def fetch(u,params,dst,mode):
 if dst.exists():return
 for attempt in range(3):
  try:
   res=requests.get(u,params=params,timeout=25);res.raise_for_status()
   if mode=='json':
    val=res.json();assert 'error' not in val,val.get('error');assert not val.get('exceededTransferLimit'),dst
   else:Image.open(BytesIO(res.content)).verify()
   dst.write_bytes(res.content);return
  except Exception as e:
   if attempt==2: print('FAIL',dst,str(e)[:180],flush=True)
def geom(name,ext,k,layer):
 u=v+'VA_Base_Layers/'+('VA_Building_Footprints' if k=='buildings' else 'VBMP_RCL')+f'/MapServer/{layer}/query'
 params=dict(f='geojson',where='1=1',geometry=','.join(map(str,ext)),geometryType='esriGeometryEnvelope',inSR=3857,spatialRel='esriSpatialRelIntersects',returnGeometry='true',outSR=3857,outFields='OBJECTID,BLDGHEIGHT,NUMSTORIES,BUILDINGCLASS,DATASOURCENAME' if k=='buildings' else 'RCL_ID,ST_FULL,ST_NAME,ST_SUFFIX,ONE_WAY,MTFCC,SPEED_MPH,VDOT_RST,VDOT_LC,VDOT_PWM,VDOT_FC')
 fetch(u,params,D/f'{name}-{k}-{layer}.json','json')
def raster(x,y,layer):
 ext=extent(x,y,x,y);size=768 if layer=='ortho' else (256 if layer=='cover' else 65);ext=list(ext)
 if layer=='dem':
  pad=w/(size-1)/2;ext=[ext[0]-pad,ext[1]-pad,ext[2]+pad,ext[3]+pad]
 urls={'ortho':v+'VBMP_Imagery/MostRecentImagery_WGS/MapServer/export','cover':v+'VA_Base_Layers/VA_Land_Cover_Land_Use_2021/MapServer/export','dem':'https://elevation.nationalmap.gov/arcgis/rest/services/3DEPElevation/ImageServer/exportImage'}
 fmt={'ortho':'jpg','cover':'png32','dem':'tiff'}[layer]
 params=dict(f='image',bbox=','.join(map(str,ext)),bboxSR=3857,imageSR=3857,size=f'{size},{size}',format=fmt)
 if layer=='dem':params.update(pixelType='F32',interpolation='RSP_BilinearInterpolation',renderingRule='{"rasterFunction":"None"}',compression='LZ77')
 fetch(urls[layer],params,D/f'{layer}-{x}-{y}.{fmt}','image')
if __name__=='__main__':
 tasks=[]
 with concurrent.futures.ThreadPoolExecutor(7) as pool:
  for n,(x,y,X,Y) in areas.items():
   for k,l in [('buildings',0),('roads',1),('roads',2),('roads',4),('roads',5)]:tasks.append(pool.submit(geom,n,extent(x,y,X,Y),k,l))
  for n,x,y in sorted(tiles(),key=lambda it:min(abs(it[1]-18585)+abs(it[2]-25424),abs(it[1]-18636)+abs(it[2]-25420))):
   for layer in ['ortho','cover','dem']:tasks.append(pool.submit(raster,x,y,layer))
  for f in concurrent.futures.as_completed(tasks):f.result()
 print('complete',len(list(D.glob('*'))),flush=True)
