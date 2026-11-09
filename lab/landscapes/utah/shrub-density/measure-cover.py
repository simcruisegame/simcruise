"""Measure projected shrub crowns on the same 86 m ground disk before and after."""
import argparse,json,math
from pathlib import Path
from PIL import Image,ImageDraw
import numpy as np
from shapely.geometry import Polygon
from shapely.ops import unary_union
def canopy(file):
 v=np.fromfile(file,dtype='<f4').reshape(-1,3,3)[:,:,[0,2]]
 shape=unary_union([p for tri in v if (p:=Polygon(tri)).is_valid and p.area>1e-9])
 return [(np.array(poly.exterior.coords),[np.array(h.coords) for h in poly.interiors]) for poly in getattr(shape,'geoms',[shape])]
def samples(lat,lon,rows,near,far,cache):
 old=len(rows[0])==6; meters=.05;span=86.;edge=int(2*span/meters)+1
 tile_x=(lon+180)/360*65536;tile_y=(1-math.asinh(math.tan(math.radians(lat)))/math.pi)*32768
 side=40075016.68557849*math.cos(math.radians(lat))/65536
 canvas=Image.new('1',(edge,edge));draw=ImageDraw.Draw(canvas)
 occupied=0
 for item in rows:
  x,y,u,v,h,r=item[:6]; yaw=item[6] if not old else 0.;d=item[7] if not old else 0.
  center=np.array([(x+u-tile_x)*side,(y+v-tile_y)*side])
  if np.linalg.norm(center)>span+r:continue
  sh=far if d>23 else near;angle=np.array([[math.cos(yaw),-math.sin(yaw)],[math.sin(yaw),math.cos(yaw)]])
  for patch,holes in sh:
   ext=(patch@angle*r+center+span)/meters
   # Shrub crowns overlap. Rasterize the holes for this individual shrub before compositing.
   if holes:
    lo=ext.min(0).astype(int)-1;hi=ext.max(0).astype(int)+2
    mask=Image.new('1',tuple(hi-lo));d2=ImageDraw.Draw(mask)
    d2.polygon([tuple(z) for z in ext-lo],fill=1)
    for hole in holes:d2.polygon([tuple(z) for z in (hole@angle*r+center+span)/meters-lo],fill=0)
    canvas.paste(1,tuple(lo),mask)
   else:draw.polygon([tuple(z) for z in ext],fill=1)
  occupied+=int(np.linalg.norm(center)<=span)
 bits=np.asarray(canvas);total=covered=target=0
 region=(int(tile_x)//16,int(tile_y)//16);survey=np.asarray(Image.open(cache/f'shrub-mask-v1-2025-{region[0]}-{region[1]}.png'))
 xs=np.arange(edge)*meters-span;px=np.floor((tile_x+xs/side-region[0]*16)*16).astype(int)
 for iy in range(edge):
  y=iy*meters-span;disc=xs**2+y*y<=span**2
  py=math.floor((tile_y+y/side-region[1]*16)*16)
  total+=int(disc.sum());covered+=int(bits[iy,disc].sum());target+=int(survey[py,px[disc],0].sum())
 return dict(count=occupied,grid_m=meters,field_area_m2=round(total*meters**2,1),crown_area_m2=round(covered*meters**2,1),crown_percent=round(covered*100/total,2),mean_model_percent=round(target/total,2))
if __name__=='__main__':
 a=argparse.ArgumentParser();a.add_argument('stems',type=Path);a.add_argument('near_mesh',type=Path);a.add_argument('far_mesh',type=Path);a.add_argument('cache',type=Path);a.add_argument('latitude',type=float);a.add_argument('longitude',type=float);v=a.parse_args()
 print(json.dumps(samples(v.latitude,v.longitude,json.loads(v.stems.read_text()),canopy(v.near_mesh),canopy(v.far_mesh),v.cache)))
