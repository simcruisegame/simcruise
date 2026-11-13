import sys, math, numpy as np
from PIL import Image
from scipy import ndimage as ndi
tag=sys.argv[1]; d=np.load(f'relief-{tag}.npz'); P=d['p']; box=d['box']; k=float(d['k'])
res=0.5; S=int(round((box[2]-box[0])/k/res))
c=((P[:,0]-box[0])/k/res).astype(int).clip(0,S-1); r=((box[3]-P[:,1])/k/res).astype(int).clip(0,S-1)
g=P[:,3]==2
def grid(sel,val,fn,init):
    a=np.full((S,S),init); fn.at(a,(r[sel],c[sel]),val[sel]); return a
dtm=grid(g,P[:,2],np.minimum,1e9); bad=dtm>1e8
idx=ndi.distance_transform_edt(bad,return_distances=False,return_indices=True); dtm=dtm[tuple(idx)]
dtm=ndi.gaussian_filter(dtm,0.6)
# the game's terrain: 1/3 arc-second (~10 m) samples, bilinear between them
n=int(10/res); coarse=dtm[::n,::n]
yy,xx=np.mgrid[0:S,0:S]/n; game=ndi.map_coordinates(coarse,[yy,xx],order=1,mode='nearest')
resid=dtm-game
print(f'{tag}: lidar minus 10 m terrain: RMS {resid.std():.2f} m, 95th pct |d| {np.percentile(abs(resid),95):.2f} m, max {abs(resid).max():.1f} m')
def shade(z):
    gy,gx=np.gradient(z,res); az,el=math.radians(315),math.radians(40)
    nx,ny,nz=-gx,gy,np.ones_like(z); l=np.sqrt(nx*nx+ny*ny+1)
    s=(nx*math.sin(az)*math.cos(el)+ny*math.cos(az)*math.cos(el)+nz*math.sin(el))/l
    return (np.clip(s,0,1)*255).astype(np.uint8)
slope=np.degrees(np.arctan(np.hypot(*np.gradient(dtm,res))))
gslope=np.degrees(np.arctan(np.hypot(*np.gradient(game,res))))
print(f'{tag}: ground steeper than 35 deg: lidar {100*(slope>35).mean():.1f} %, 10 m terrain {100*(gslope>35).mean():.1f} %')
# intensity of ground returns (road paint, patches, gravel shoulders)
inten=grid(g,P[:,4].astype(float),np.add,0.0); cnt=grid(g,np.ones(len(P)),np.add,0.0)
I=np.where(cnt>0,inten/np.maximum(cnt,1),np.nan); I=np.where(np.isnan(I),np.nanmedian(I),I)
lo,hi=np.percentile(I,[2,99.5]); I8=(np.clip((I-lo)/(hi-lo),0,1)**0.8*255).astype(np.uint8)
ims=[Image.fromarray(shade(game)),Image.fromarray(shade(dtm)),Image.fromarray(I8)]
W=ims[0].width; o=Image.new('L',(3*W+40,W),255)
for i,im in enumerate(ims): o.paste(im,(i*(W+20),0))
o=o.resize((o.width*2//3,o.height*2//3)); o.save(f'relief-{tag}.png')
np.save(f'relief-{tag}-resid.npy',resid.astype(np.float32))
