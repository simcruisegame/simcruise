import json, math, sys, urllib.request, io, numpy as np, laspy
from pyproj import Transformer
EPT='https://noaa-nos-coastal-lidar-pds.s3.amazonaws.com/entwine/geoid18/10338/'
X,Y,Z=int(sys.argv[1]),int(sys.argv[2]),16
def lonlat(x,y,z): n=2**z; return x/n*360-180, math.degrees(math.atan(math.sinh(math.pi*(1-2*y/n))))
w,n=lonlat(X,Y,Z); e,s=lonlat(X+1,Y+1,Z)
t=Transformer.from_crs(4326,6346,always_xy=True)
xs,ys=zip(*[t.transform(a,b) for a in (w,e) for b in (s,n)])
box=(min(xs)-60,min(ys)-60,max(xs)+60,max(ys)+60)  # margin for shadow casters
info=json.load(urllib.request.urlopen(EPT+'ept.json')); B=info['bounds']
hier=json.load(urllib.request.urlopen(EPT+'ept-hierarchy/0-0-0-0.json'))
MAXD=int(sys.argv[3]) if len(sys.argv)>3 else 9
keys=[]
def walk(h):
  for k,c in h.items():
    if c==-1:
      d,x,y,z=map(int,k.split('-')); size=(B[3]-B[0])/2**d; x0=B[0]+x*size; y0=B[1]+y*size
      if d<=MAXD and not(x0>box[2] or x0+size<box[0] or y0>box[3] or y0+size<box[1]):
        yield from walk(json.load(urllib.request.urlopen(EPT+'ept-hierarchy/'+k+'.json')))
      continue
    yield k,c
for k,c in list(walk(hier)):
    d,x,y,z=map(int,k.split('-'))
    if d>MAXD or c==0: continue
    size=(B[3]-B[0])/2**d
    x0=B[0]+x*size; y0=B[1]+y*size
    if x0>box[2] or x0+size<box[0] or y0>box[3] or y0+size<box[1]: continue
    keys.append((k,c))
print(len(keys), sum(c for _,c in keys), file=sys.stderr)
pts=[]
for k,c in keys:
    if c<0: continue
    for attempt in range(5):
        try: data=urllib.request.urlopen(EPT+'ept-data/'+k+'.laz',timeout=60).read(); break
        except Exception as ex: print('retry',k,ex,file=sys.stderr)
    las=laspy.read(io.BytesIO(data))
    m=(las.x>=box[0])&(las.x<=box[2])&(las.y>=box[1])&(las.y<=box[3])
    pts.append(np.c_[las.x[m],las.y[m],las.z[m],las.classification[m]])
P=np.concatenate(pts); print(P.shape, file=sys.stderr)
np.savez_compressed(f'pts-{X}-{Y}.npz',p=P,box=np.array(box),tile=np.array([w,n,e,s]))
