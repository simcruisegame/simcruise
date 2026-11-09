"""Bake the geographically fixed Needles Highway solid and its two screened albedo maps."""
from pathlib import Path
from struct import pack
import argparse
import numpy as np
import cv2
from scipy.ndimage import gaussian_filter, distance_transform_edt, map_coordinates, maximum_filter, minimum_filter
from scipy.interpolate import LinearNDInterpolator
from scipy.spatial import cKDTree
parser=argparse.ArgumentParser()
parser.add_argument("output",type=Path)
args=parser.parse_args()
D=Path(__file__).resolve().parent
room=args.output
room.mkdir(exist_ok=True,parents=True)
survey=np.load(D/"survey.npz")
G,C,GB,CB,KA,KB=(survey[k] for k in ("G","C","GB","CB","KA","KB"))
meas=survey["measured_front"];obs=survey["direct_stereo"]
imA=cv2.imread(str(D/"149021336.jpg"));imB=cv2.imread(str(D/"149021324.jpg"))
lat,lon=survey["anchor_latlon"];side=float(survey["side"])
OUTLINE=[[13, 670], [28, 628], [48, 582], [69, 570], [81, 560], [77, 531], [80, 482], [82, 446], [86, 420], [92, 374], [102, 349], [112, 332], [118, 328], [120, 350], [126, 342], [139, 305], [151, 266], [151, 230], [158, 208], [169, 175], [183, 144], [191, 128], [213, 119], [227, 115], [248, 116], [260, 125], [273, 146], [278, 177], [281, 213], [284, 222], [293, 219], [303, 211], [304, 217], [310, 206], [311, 221], [324, 218], [325, 246], [329, 255], [329, 276], [337, 295], [346, 301], [360, 288], [374, 277], [387, 275], [390, 294], [394, 313], [408, 309], [429, 315], [436, 320], [447, 321], [454, 337], [459, 348], [465, 365], [476, 360], [487, 358], [507, 367], [512, 379], [524, 389], [533, 400], [546, 410], [551, 428], [557, 426], [562, 444], [570, 454], [571, 502], [544, 503], [519, 524], [496, 532], [477, 530], [465, 536], [446, 532], [425, 543], [408, 553], [388, 553], [376, 561], [355, 563], [347, 555], [336, 565], [314, 570], [299, 582], [282, 581], [266, 593], [253, 597], [236, 591], [226, 604], [212, 608], [198, 606], [184, 617], [168, 626], [150, 631], [134, 635], [120, 640], [111, 652], [91, 654], [77, 659], [62, 664], [48, 664], [26, 676]]
vertices_b=[[24,630],[39,595],[50,572],[73,552],[83,532],[97,515],[88,481],[92,453],[96,426],[100,397],[101,369],[110,344],[118,327],[124,323],[125,342],[131,334],[133,320],[136,325],[137,338],[142,325],[152,304],[161,283],[163,261],[163,242],[164,229],[175,207],[185,193],[198,180],[208,161],[224,147],[241,138],[255,138],[264,143],[276,154],[281,168],[282,183],[277,213],[279,231],[285,229],[291,224],[297,230],[304,224],[304,242],[311,240],[319,232],[319,255],[318,275],[323,284],[326,299],[327,322],[336,319],[351,312],[361,307],[378,304],[385,308],[386,321],[395,326],[402,329],[415,334],[431,345],[439,353],[441,375],[448,372],[461,372],[475,381],[480,391],[486,392],[493,410],[492,435],[506,438],[513,446],[519,453],[531,450],[538,451],[544,442],[556,437],[563,442],[565,453],[574,460],[574,508],[552,510],[528,520],[512,522],[458,509],[447,519],[426,513],[404,524],[381,525],[360,536],[337,532],[319,540],[297,539],[275,549],[253,549],[229,553],[200,563],[175,575],[149,579],[129,583],[104,595],[89,598],[71,607],[58,614],[41,623]]
vegB=[[[183,384],[186,336],[196,308],[197,274],[206,249],[217,243],[223,263],[230,250],[237,248],[241,257],[249,244],[261,247],[262,239],[275,236],[281,252],[278,276],[272,287],[277,304],[272,326],[263,340],[248,354],[235,372],[222,400],[218,430],[209,466],[206,441],[204,405],[195,421],[198,387],[188,409]],[[272,310],[284,295],[300,298],[306,280],[316,281],[321,300],[315,320],[309,333],[294,347],[283,359],[277,369],[279,345],[269,342]],[[384,479],[398,454],[407,455],[420,469],[427,483],[424,495],[409,507],[391,506]],[[482,502],[493,471],[505,472],[513,480],[517,499],[506,512]]]
vegA=[[[136,328],[147,281],[152,244],[160,214],[174,212],[185,248],[195,271],[206,237],[235,218],[247,224],[251,251],[245,269],[276,267],[287,281],[285,299],[266,314],[248,332],[222,348],[201,372],[185,392],[171,416],[166,472],[155,477],[154,414],[140,469],[132,478],[136,441],[128,458],[127,439],[134,403],[124,421],[124,412],[136,375],[125,385],[125,362]],[[273,292],[305,288],[320,279],[317,315],[299,333],[281,348],[260,381],[253,383],[267,337]],[[357,484],[374,463],[397,468],[409,480],[430,485],[436,508],[405,521],[386,540],[365,540],[366,520],[351,514]],[[475,523],[494,490],[514,485],[538,490],[548,511],[534,536],[492,540]]]
def masks(im,outline,fol):
 h,w=im.shape[:2];m=np.zeros((h,w),'u1');cv2.fillPoly(m,[np.rint(np.array(outline)/.6).astype('i4')],1);r,g,b=im[:,:,::-1].astype('f4').transpose(2,0,1)
 vegetation=(g>1.07*r+1.5)&(g>1.17*b+3);sky=(b>1.12*r+4)&(b>1.025*g+3)
 vm=vegetation.astype('u1')
 for v in fol:cv2.fillPoly(vm,[np.rint(np.array(v)/.6).astype('i4')],1)
 keep=m.astype(bool)&~cv2.dilate(vm,np.ones((4,4),'u1')).astype(bool)&~sky
 return m,keep
mA,keepA=masks(imA,OUTLINE,vegA);mB,keepB=masks(imB,vertices_b,vegB)
def sdf(m):return distance_transform_edt(m==0)-distance_transform_edt(m)
sA=sdf(mA);sB=sdf(mB)
def lower_boundary(m):
 h,w=m.shape
 return (h-1-np.argmax(m[::-1,:],axis=0))*np.any(m,axis=0)+2*h*(~np.any(m,axis=0))
footA=lower_boundary(mA);footB=lower_boundary(mB)
def proj(p,rot,c,k):
 v=(p-c)@rot;uv=(v[:,:2]/np.maximum(v[:,2:],1e-4))@k[:2,:2]+k[:2,2];return uv,v[:,2]
def read(arr,uv,cval=999):return map_coordinates(arr.astype('f4'),[uv[:,1],uv[:,0]],order=1,mode='constant',cval=cval,prefilter=False)
def depths(points,rot,c,k,im):
 uv,dep=proj(points,rot,c,k);yd,xd=np.mgrid[:im.shape[0]:6,:im.shape[1]:6];qp=np.c_[xd.ravel(),yd.ravel()];q=LinearNDInterpolator(uv,1/dep)(qp);ds,ii=cKDTree(uv).query(qp);nan=~np.isfinite(q);q[nan]=1/dep[ii[nan]];return gaussian_filter((1/q).reshape(xd.shape),.78),ds.reshape(xd.shape)
depA,dsA=depths(meas,G,C,KA,imA);depB,dsB=depths(obs,GB,CB,KB,imB)
# Metric volume in east, up, north. Use a strictly bounded local domain and lock every solid below to the true project ground.
res=.37;lower=np.array([-56.,1917.,-52.]);limits=np.array([1.,1966.,-1.]);ax=[np.arange(lo,hi+res,res) for lo,hi in zip(lower,limits)]
XX,ZZ=np.meshgrid(ax[0],ax[2],indexing='ij');en=np.c_[XX.ravel(),ZZ.ravel()];ground=survey['ground']
# Ceiling from the native laser footprint and corroborating optical surface heights, with a 1.2m allowable laser-registration difference.
laser=survey['laser']
def envelope(p,r):
 xi=np.rint((p[:,0]-ax[0][0])/res).astype(int);zi=np.rint((p[:,2]-ax[2][0])/res).astype(int);ok=(xi>=0)&(xi<len(ax[0]))&(zi>=0)&(zi<len(ax[2]));top=np.full(XX.shape,-999.,'f4');np.maximum.at(top,(xi[ok],zi[ok]),p[ok,1]);t=maximum_filter(top,size=r)
 ds,at=distance_transform_edt(t<-900,return_indices=True);return np.where(ds*res<=2,t[tuple(at)]-ds*.55,t)
ceiling=np.maximum(envelope(laser,7)+1.05,envelope(meas,9)+.32);ceiling=np.minimum(ceiling,float(obs[:,1].max())+.25)
horizontal=cKDTree(meas[:,[0,2]]).query(en)[0].reshape(XX.shape);distlim=horizontal-7.0
# A gridded bare-earth model can rise above the vertical face. The optical lower envelope is also a ground constraint wherever the stereo sees stone below that grid.

# The negated envelope sampler uses an elevation floor; compute optical minima explicitly for the same posting.
from scipy.ndimage import minimum_filter
xi=np.rint((meas[:,0]-ax[0][0])/res).astype(int);zi=np.rint((meas[:,2]-ax[2][0])/res).astype(int);good=(xi>=0)&(xi<len(ax[0]))&(zi>=0)&(zi<len(ax[2]));bottom=np.full(XX.shape,np.inf);np.minimum.at(bottom,(xi[good],zi[good]),meas[good,1]);bottom=minimum_filter(bottom,size=13)
floor=np.minimum(ground-1.,bottom-2.6)
volume=np.empty((len(ax[0]),len(ax[1]),len(ax[2])),'f4')
for i,y in enumerate(ax[1]):
 xyz=np.c_[en[:,0],np.full(len(en),y),en[:,1]];u,za=proj(xyz,G,C,KA);v,zb=proj(xyz,GB,CB,KB)
 a=read(sA,u)*np.maximum(za,1)/KA[0,0];b=read(sB,v)*np.maximum(zb,1)/KB[0,0]
 # Rock/sky outlines are free-space constraints. A rock/soil contact only provides an approximate above-ground boundary; bury that part in the local terrain.
 belowA=u[:,1]>=footA[u[:,0].astype(int).clip(0,len(footA)-1)];belowB=v[:,1]>=footB[v[:,0].astype(int).clip(0,len(footB)-1)]
 under=horizontal.ravel()-2.1
 a=np.where(belowA,np.minimum(a,under),a);b=np.where(belowB,np.minimum(b,under),b)
 # Each photograph constrains the outline and measured/lower-uncertainty fronts. Outside the traced 336 image, the solid ends.
 frontA=(read(depA,u/6)-za)*.78;frontB=(read(depB,v/6)-zb)*.78;confB=read(dsB,v/6)
 frontB=np.where(confB<19,frontB-0.48,-999.)
 volume[:,i,:]=np.maximum.reduce([a.reshape(XX.shape),b.reshape(XX.shape),frontA.reshape(XX.shape),frontB.reshape(XX.shape),y-ceiling,floor-y,distlim])
volume=gaussian_filter(np.clip(volume,-2.,2.),.72);volume[[0,-1],:,:]=3;volume[:,[0,-1],:]=3;volume[:,:,[0,-1]]=3
# Marching tetrahedra with shared-edge deduplication and gradients for source-oriented normals. This yields a closed, real 3-D rock solid.
shape=np.array(volume.shape);aa,bb,cc=np.indices(shape-1);base_i=(aa*shape[1]+bb)*shape[2]+cc;corners=np.array([0,shape[1]*shape[2],shape[1]*shape[2]+shape[2],shape[2],1,shape[1]*shape[2]+1,shape[1]*shape[2]+shape[2]+1,shape[2]+1]);base_i=base_i.ravel();given=volume.ravel();active=np.minimum.reduce([given[base_i+o] for o in corners])<=0;active&=np.maximum.reduce([given[base_i+o]for o in corners])>0;grid=base_i[active,None]+corners;elist=[(0,1),(0,2),(0,3),(1,2),(1,3),(2,3)];faces=[]
# Standard tetrahedra share the 0--6 body diagonal; each quad is split into two consistent triangles.
for tet in [(0,1,2,6),(0,2,3,6),(0,3,7,6),(0,7,4,6),(0,4,5,6),(0,5,1,6)]:
 q=grid[:,tet];flags=(given[q]<0);bits=(flags*np.array([1,2,4,8])).sum(1)
 for code in range(1,15):
  rows=q[bits==code];inner=[i for i in range(4) if code&(1<<i)];outer=[i for i in range(4) if i not in inner]
  if not len(rows):continue
  if len(inner)==1 or len(outer)==1:
   anchor,others=(inner[0],outer)if len(inner)==1 else(outer[0],inner);triangles=[[(anchor,k)for k in others]]
  else:
   i,j=inner;k,l=outer;triangles=[[(i,k),(i,l),(j,l)],[(i,k),(j,l),(j,k)]]
  for tri in triangles:faces.append(np.stack([np.sort(rows[:,[a,b]],axis=1)for a,b in tri],axis=1))
ed=np.concatenate(faces);edges,idx=np.unique(ed.reshape(-1,2),axis=0,return_inverse=True);tri=idx.reshape((-1,3));start,end=edges.T;v1=given[start];v2=given[end];t0=(v1/(v1-v2))[:,None];p1=np.stack(np.unravel_index(start,shape),axis=1);p2=np.stack(np.unravel_index(end,shape),axis=1);gridp=p1+(p2-p1)*t0;pt=gridp*res+lower
gradient=np.gradient(volume,res);normal=np.stack([map_coordinates(grad,gridp.T,order=1)for grad in gradient],axis=1);normal/=np.maximum(np.linalg.norm(normal,axis=1)[:,None],1e-9)
nf=np.cross(pt[tri[:,1]]-pt[tri[:,0]],pt[tri[:,2]]-pt[tri[:,0]]);flip=(nf*normal[tri].mean(1)).sum(1)<0;tri[flip]=tri[flip][:,[0,2,1]]
# Each sampled optical texture supplies only screened rock. Apply geometric visibility and agreement once in world space.
def attribution(im,valid,p,rot,c,K,dep,dist):
 u,z=proj(p,rot,c,K);ray=c-p;ray/=np.maximum(np.linalg.norm(ray,axis=1)[:,None],.001);facing=(normal*ray).sum(1);dd=np.abs(z-read(dep,u/6));ds=read(dist,u/6);conf=np.clip((1.7-dd)/1.05,0,1)*np.clip((facing+.13)/.50,0,1)*np.clip((60.-ds)/28.,0,1)
 rgba=cv2.cvtColor(im,cv2.COLOR_BGR2BGRA);rgba[:,:,3]=np.uint8(np.clip(distance_transform_edt(valid),0,5)*51);rgba[~valid,:3]=[105,110,114]
 return u/[im.shape[1],im.shape[0]],conf,rgba
uva,wa,ta=attribution(imA,keepA,pt,G,C,KA,depA,dsA);uvb,wb,tb=attribution(imB,keepB,pt,GB,CB,KB,depB,dsB)
HA=room/'needles-volume-a.webp';HB=room/'needles-volume-b.webp';cv2.imwrite(str(HA),ta,[cv2.IMWRITE_WEBP_QUALITY,82]);cv2.imwrite(str(HB),tb,[cv2.IMWRITE_WEBP_QUALITY,82]);# measured-source granite sample for the unobserved backside
zref=float(survey['zref']);vp=pt.copy();vp[:,1]-=zref;vp[:,2]*=-1.;ns=normal.copy();ns[:,2]*=-1.;ltc=np.array([(lon+180)/360*65536,(1-np.arcsinh(np.tan(np.radians(lat)))/np.pi)*32768])
f=room/'needles-volume.meshdata';color=np.c_[wa,wb,np.zeros(len(wa)),np.ones(len(wa))]
with f.open('wb') as out:
 out.write(pack('<IfffII',3,*(ltc%1),side,len(pt),tri.size));out.write(vp.astype('<f4').tobytes());out.write(ns.astype('<f4').tobytes());out.write(np.float32(uva).tobytes());out.write(np.float32(uvb).tobytes());out.write(np.uint8(np.round(color*255)).tobytes());out.write(tri.astype('<i4').tobytes())
eg=np.concatenate([tri[:,[0,1]],tri[:,[1,2]],tri[:,[2,0]]]);_,count=np.unique(np.sort(eg,axis=1),axis=0,return_counts=True)

print('solid nodes',len(pt),'triangles',len(tri),'boundary edge count',int((count!=2).sum()),'ceiling',pt[:,1].max(),'nearest ground',np.min(pt[:,1]),'bytes',f.stat().st_size,HA.stat().st_size,HB.stat().st_size,flush=True)
