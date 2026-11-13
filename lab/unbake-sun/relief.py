# Corridor relief: what 1 m lidar ground shows that the game's ~10 m terrain grid cannot,
# plus the lidar intensity picture of the road (paint, patches). USGS 3DEP EPT, EPSG:3857.
import json, math, sys, io, urllib.request, numpy as np, laspy
from PIL import Image
from scipy import ndimage as ndi
proj,lon,lat,half=sys.argv[1],float(sys.argv[2]),float(sys.argv[3]),float(sys.argv[4])
MAXD=int(sys.argv[5]); tag=sys.argv[6]
E=f'https://usgs-lidar-public.s3.amazonaws.com/{proj}/'
R=6378137.0; cx=math.radians(lon)*R; cy=R*math.log(math.tan(math.pi/4+math.radians(lat)/2))
k=1/math.cos(math.radians(lat)); hm=half*k   # mercator metres per ground metre
box=(cx-hm,cy-hm,cx+hm,cy+hm)
B=json.load(urllib.request.urlopen(E+'ept.json'))['bounds']
def get(u):
    for a in range(5):
        try: return urllib.request.urlopen(u,timeout=60).read()
        except Exception as ex: print('retry',u,ex,file=sys.stderr)
def walk(h):
    for key,c in h.items():
        d,x,y,z=map(int,key.split('-')); size=(B[3]-B[0])/2**d; x0=B[0]+x*size; y0=B[1]+y*size
        if d>MAXD or x0>box[2] or x0+size<box[0] or y0>box[3] or y0+size<box[1]: continue
        if c==-1: yield from walk(json.loads(get(E+'ept-hierarchy/'+key+'.json')))
        elif c>0: yield key
pts=[]
for key in set(walk(json.loads(get(E+'ept-hierarchy/0-0-0-0.json')))):
    las=laspy.read(io.BytesIO(get(E+'ept-data/'+key+'.laz')))
    m=(las.x>=box[0])&(las.x<=box[2])&(las.y>=box[1])&(las.y<=box[3])
    pts.append(np.c_[las.x[m],las.y[m],las.z[m],las.classification[m],las.intensity[m]])
P=np.concatenate(pts); print(tag,P.shape,'points', P.shape[0]/(2*half)**2,'per m2')
np.savez_compressed(f'relief-{tag}.npz',p=P,box=np.array(box),k=k)
