# Unbake the sun: find the sun shadows baked into an aerial photograph from lidar
# heights alone (the sun's direction is fitted to the photograph's own shadows), then lift
# each shadow by the local lit/shade ratio of the same surface class.
import sys, math, numpy as np
from PIL import Image
from pyproj import Transformer
from scipy import ndimage as ndi
X,Y=int(sys.argv[1]),int(sys.argv[2]); SUN=tuple(map(int,sys.argv[3].split(','))) if len(sys.argv)>3 else None; SRC=sys.argv[4] if len(sys.argv)>4 else None; ax,ay=X//4,Y//4; qx,qy=X%4,Y%4
d=np.load(f'pts-{X}-{Y}.npz'); P=d['p']
img=np.asarray(Image.open(SRC or f'a-{ax}-{ay}.jpg').convert('RGB')).astype(np.float32)/255
N=img.shape[0]; R=6378137.0; half=math.pi*R
size14=2*half/16384; px=size14/N           # metres (mercator) per pixel
west=ax*size14-half; north=half-ay*size14
mx,my=Transformer.from_crs(6346,3857,always_xy=True).transform(P[:,0],P[:,1])
c=np.floor((mx-west)/px).astype(int); r=np.floor((north-my)/px).astype(int)
# window: our z16 quadrant plus a 32 px margin for casters
M=32; c0,r0=qx*N//4-M, qy*N//4-M; S=N//4+2*M
c0c,r0c=max(c0,0),max(r0,0)
k=(c>=c0)&(c<c0+S)&(r>=r0)&(r<r0+S)
c,r,z,cl=c[k]-c0,r[k]-r0,P[k,2],P[k,3].astype(int)
dsm=np.full((S,S),-1e9); np.maximum.at(dsm,(r,c),z)
g=cl==2; dtm=np.full((S,S),1e9); np.minimum.at(dtm,(r[g],c[g]),z[g])
# fill gaps
def fill(a,bad):
    i=ndi.distance_transform_edt(bad,return_distances=False,return_indices=True); return a[tuple(i)]
dsm=fill(dsm,dsm<-1e8); dtm=fill(dtm,dtm>1e8)
cls=np.zeros((S,S),int); cnt=np.zeros((S,S,4))
for j,(lo,hi) in enumerate([(2,2),(3,5),(6,6),(9,9)]):
    m=(cl>=lo)&(cl<=hi); np.add.at(cnt[...,j],(r[m],c[m]),1)
cls=np.argmax(cnt,axis=2); cls[cnt.sum(2)==0]=0
scale=px*math.cos(math.radians(25.78))   # ground metres per pixel at Miami Beach
H=dsm
def shadow(az,el):
    dx,dy=math.sin(math.radians(az)),-math.cos(math.radians(az))  # toward the sun, image rows grow south
    t=math.tan(math.radians(el))*scale
    out=np.zeros_like(H,bool)
    for s in np.arange(1,60,0.7):
        sh=ndi.shift(H,(-dy*s,-dx*s),order=1,mode='nearest')
        out|=sh-H>s*t+0.3
    return out
L=(img[r0:r0+S,c0:c0+S]@np.array([.2126,.7152,.0722])) if c0>=0 and r0>=0 else None
crop=np.zeros((S,S,3),np.float32)
ys,xs=slice(max(r0,0),min(r0+S,N)),slice(max(c0,0),min(c0+S,N))
crop[max(-r0,0):max(-r0,0)+(ys.stop-ys.start),max(-c0,0):max(-c0,0)+(xs.stop-xs.start)]=img[ys,xs]
lin=crop**2.2; L=lin@np.array([.2126,.7152,.0722])
inner=np.zeros((S,S),bool); inner[M:-M,M:-M]=True
ground=inner&(cls!=2)
best=None
for az in ([] if SUN else range(0,360,6)):
    for el in range(25,86,5):
        sm=shadow(az,el)&ground
        if sm.sum()<50: continue
        a,b=np.log(L[sm]+1e-4).mean(),np.log(L[ground&~sm]+1e-4).mean()
        sc=(b-a)*math.sqrt(sm.mean()*(1-sm.mean()))
        if best is None or sc>best[0]: best=(sc,az,el)
if SUN: best=(-1,)+SUN
_,az,el=best
for az2 in ([] if SUN else [0]):
  pass
for az2 in (range(az-5,az+6,1) if not SUN else []):
    for el2 in range(el-4,el+5,1):
        sm=shadow(az2,el2)&ground
        if sm.sum()<50: continue
        a,b=np.log(L[sm]+1e-4).mean(),np.log(L[ground&~sm]+1e-4).mean()
        sc=(b-a)*math.sqrt(sm.mean()*(1-sm.mean()))
        if sc>best[0]: best=(sc,az2,el2)
_,az,el=best; print(f'fitted sun: azimuth {az} deg, elevation {el} deg')
sm=shadow(az,el)&(cls!=2)
soft=ndi.gaussian_filter(sm.astype(np.float32),0.7)
# local lit/shade ratio per surface class (ground, vegetation, water), per channel
out=lin.copy(); sig=12
for j in (0,1,3):
    mc=(cls==j).astype(np.float32)
    if mc.sum()<20: continue
    lit=mc*(1-soft); sh=mc*soft
    for ch in range(3):
        lm=ndi.gaussian_filter(lin[...,ch]*lit,sig)/(ndi.gaussian_filter(lit,sig)+1e-6)
        smn=ndi.gaussian_filter(lin[...,ch]*sh,sig)/(ndi.gaussian_filter(sh,sig)+1e-6)
        gl=np.median(lin[...,ch][(lit>0.9)]); gs=np.median(lin[...,ch][(sh>0.9)]) if (sh>0.9).sum()>10 else gl
        ratio=np.where(ndi.gaussian_filter(sh,sig)>0.05,lm/(smn+1e-4),gl/(gs+1e-4))
        ratio=np.clip(ratio,1,4)
        lifted=lin[...,ch]*(1+(ratio-1)*soft)
        # never lift a shaded pixel above the lit surface of its class nearby
        lifted=np.where(soft>0.05,np.minimum(lifted,np.maximum(lm*1.1,lin[...,ch])),lifted)
        out[...,ch]=np.where(mc>0,lifted,out[...,ch])
res=np.clip(out,0,1)**(1/2.2)
full=img.copy(); rr,cc=r0+M,c0+M; full[rr:rr+N//4,cc:cc+N//4]=res[M:-M,M:-M]
np.save(f'out-{X}-{Y}.npy',(res[M:-M,M:-M]*255).astype(np.uint8))
def save(a,name): Image.fromarray((np.clip(a[M:-M,M:-M],0,1)*255).astype(np.uint8)).resize((N//4*3,N//4*3),Image.NEAREST).save(name)
save(crop,f'before-{X}-{Y}.png'); save(res,f'after-{X}-{Y}.png')
ov=crop.copy(); ov[sm]=ov[sm]*0.4+np.array([1,0,1])*0.6; save(ov,f'mask-{X}-{Y}.png')
# metric: lit/shade luminance gap on ground and vegetation, before and after
Lo=res**2.2@np.array([.2126,.7152,.0722])
for j,name in ((0,'ground'),(1,'vegetation')):
    m=inner&(cls==j); a=m&sm; b=m&~sm
    if a.sum()>20: print(f'{name}: {a.sum()} shaded px; shade/lit luminance before {np.median(L[a])/np.median(L[b]):.2f}, after {np.median(Lo[a])/np.median(Lo[b]):.2f}')
np.save('cls.npy',cls[M:-M,M:-M]); np.save('dsmh.npy',(dsm-dtm)[M:-M,M:-M])
