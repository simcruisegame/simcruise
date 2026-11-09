from fetch_field import areas,tiles,extent,w,D
from shapely.geometry import shape,box,Point,MultiPoint,LineString
from shapely import contains_xy
from scipy.ndimage import distance_transform_edt,gaussian_filter,maximum_filter
from scipy.spatial import cKDTree
from PIL import Image
import numpy as np,mapbox_earcut as earcut,json,math
from pathlib import Path
OUT=Path('game/data');BUN=Path('lab/detail-manifest.json')
suffix={'Rd':'Road','Dr':'Drive','Ln':'Lane','Ct':'Court','Cir':'Circle','Hwy':'Highway','Trl':'Trail','Pl':'Place','Ter':'Terrace','Way':'Way'}
def full(s):return ' '.join(suffix.get(x.rstrip('.'),x) for x in s.title().split())
def unroll(g):
 if g.is_empty:return []
 return list(g.geoms) if g.geom_type.startswith(('Multi','GeometryCollection')) else [g]
def load(n,t,l):
 return json.loads((D/f'{n}-{t}-{l}.json').read_text())['features']
def area(name):
 roads={}
 for l in [4,5,2,1]:
  for f in load(name,'roads',l):
   a=f['properties'];s=shape(f['geometry']);rd=full(a.get('ST_FULL') or '')
   cls='trunk' if 'Patrick Henry Highway'==rd else ('secondary' if a.get('MTFCC','').startswith('S1200') else 'minor')
   if a.get('ST_FULL') is None:continue
   width=a.get('VDOT_PWM');measured=width and 7<=width<=90;width=float(width)*.3048 if measured else (6.15 if cls=='minor' else 7.)
   roads[a['RCL_ID']]=(s,[cls,{'FT':1,'TF':-1}.get(a.get('ONE_WAY'),0),rd,width],a['RCL_ID'])
 footprints=[(shape(f['geometry']),f['properties']) for f in load(name,'buildings',0)]
 return list(roads.values()),footprints,np.load(D/(name+'-grid.npy'))
def geometry(x,y,n,roads,buildings,g,manifest):
 e=extent(x,y,x,y);mask=box(*e);scale=math.cos(math.atan(math.sinh(math.pi*(1-2*(y+.5)/65536))));side=w*scale
 def uv(pt):return [round((pt[0]-e[0])/w,6),round((e[3]-pt[1])/w,6)]
 def h(u,v):return float(g[round(np.clip(v,0,1)*64),round(np.clip(u,0,1)*64)])
 paths=[];rlines=[]
 for road,(cls,one,title,width),id in roads:
  if not mask.intersects(road):continue
  for part in unroll(road.intersection(mask)):
   if part.geom_type!='LineString':continue
   points=[uv(c) for c in part.coords]
   paths.append([cls,one,title,points,round(width,2)]);rlines.append((LineString(np.array(points)*256),width))
 houses=[];footmask=np.zeros((256,256),bool);ortho=np.asarray(Image.open(D/f'ortho-{x}-{y}.jpg').convert('RGB'))
 lidar_file=D/f'cloud-{x}-{y}.npy';crown=np.load(lidar_file) if lidar_file.exists() else None
 for building,attr in buildings:
  c=building.centroid
  if not mask.covers(c):continue
  for b in unroll(building):
   if b.geom_type!='Polygon' or b.area*scale**2<5:continue
   b=b.simplify(.18,preserve_topology=True);rings=[[uv(a) for a in list(r.coords)[:-1]] for r in [b.exterior,*b.interiors]]
   vert=np.array([a for r in rings for a in r]);ind=earcut.triangulate_float64(vert,np.array(np.cumsum([len(r) for r in rings]),dtype=np.uint32)).tolist();u,v=uv((c.x,c.y));poly=shape({'type':'Polygon','coordinates':rings})
   lo=np.maximum(np.floor(vert.min(0)*256).astype(int),0);hi=np.minimum(np.ceil(vert.max(0)*256).astype(int)+1,256)
   if np.any(hi<=lo):continue
   yy,xx=np.mgrid[lo[1]:hi[1],lo[0]:hi[0]];inside=contains_xy(poly,(xx+.5)/256,(yy+.5)/256);footmask[yy,xx]|=inside
   rgb=ortho[np.clip((yy[inside]+.5).astype(float)*3,0,767).astype(int),np.clip((xx[inside]+.5)*3,0,767).astype(int)]
   roof=np.median(rgb,axis=0).astype(int).tolist() if len(rgb) else [85,81,78]
   high=4.3 if b.area*scale**2>94 else 2.7
   height_from='estimated'
   if crown is not None and inside.sum()>3:
    samples=crown[yy[inside],xx[inside]];samples=samples[(samples>2)&(samples<17)]
    if len(samples)>=5:high=float(np.percentile(samples,82));height_from='lidar'
   houses.append([rings,ind,round(high,2),0,u,v,'survey',roof,height_from])
 cover=np.asarray(Image.open(D/f'class-{x}-{y}.png').convert('RGB'),dtype=np.int32)
 colors=np.asarray([[38,115,0],[56,168,0],[115,115,0],[138,138,136]])
 dist=np.min([np.sum((cover-c)**2,axis=2) for c in colors],axis=0)
 canopy=(dist<32**2)&~footmask
 # Enforce the actual carriageway opening but keep woodland tight to its outer shoulder.
 for line,width in rlines:
  rr=width/2/side*256+1.1
  for t in np.arange(0,line.length+1,1.4):
   p=line.interpolate(t);cx=round(p.x);cy=round(p.y);rad=math.ceil(rr)
   x0=max(0,cx-rad);x1=min(256,cx+rad+1);y0=max(0,cy-rad);y1=min(256,cy+rad+1)
   if x1<=x0 or y1<=y0:continue
   ys,xs=np.mgrid[y0:y1,x0:x1];canopy[y0:y1,x0:x1]&=(xs-p.x)**2+(ys-p.y)**2>rr**2
 rng=np.random.default_rng(x*65536+y);trees=[]
 optical=ortho.reshape(256,3,256,3,3).mean(axis=(1,3));green=gaussian_filter(optical[:,:,1],1)
 # Local crown centres use lidar height peaks where available, aerial brightness in the mapped canopy elsewhere.
 value=green+rng.random((256,256))*.04
 if crown is not None:value=np.where(crown>3,gaussian_filter(crown,.65)*8+green*.05,value)
 value[~canopy]=-1000
 yy,xx=np.where(canopy&(maximum_filter(value,size=7)==value))
 for j,i in zip(yy,xx):
  height=float(crown[j,i]) if crown is not None and 3<crown[j,i]<42 else float(rng.uniform(10.5,19.5))
  trees.append([round((i+.5)/256,5),round((j+.5)/256,5),round(height,1),round(height*.022,2),''])
 quality='lidar' if crown is not None else 'landcover'
 tile={'id':[x,y],'h':g.round(2).ravel().tolist(),'grid':64,'roads':paths,'buildings':houses,'trees':trees,'land':[],'pois':[],'lamps':[],'requests':0,'field':True,'region':n,'canopy':quality}
 dst=OUT/f'{x}_{y}.json';dst.write_text(json.dumps(tile,separators=(',',':')));pic=f'data/va-{x}-{y}.jpg';path=Path('game')/pic
 path.write_bytes((D/f'ortho-{x}-{y}.jpg').read_bytes());manifest[pic]=str(path);manifest['data/'+dst.name]=str(dst)
 return len(houses),len(trees),len(paths)
def main():
 data={k:area(k) for k in areas};manifest={};stats={}
 for n,x,y in tiles():
  r,b,z=data[n];x0,y0,_,_=areas[n];i=(x-x0)*64;j=(y-y0)*64;m=geometry(x,y,n,r,b,z[j:j+65,i:i+65],manifest);stats[n]=np.array(stats.get(n,[0,0,0]))+m
 BUN.write_text(json.dumps(manifest));print({n:s.tolist() for n,s in stats.items()},len(manifest),flush=True)
if __name__=='__main__':main()
