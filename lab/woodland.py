"""Build closed, tapered limbs and irregular three-dimensional crowns at three levels of detail."""
import math,random,struct,json
from pathlib import Path
import numpy as np
D=Path('game/data')
def unit(v):return v/max(.00001,np.linalg.norm(v))
def xyz(a,y):return np.array([math.cos(a),y,math.sin(a)])
def mesh(kind,lod):
 rng=random.Random(865 if kind=='pine' else 1194);geom=random.Random(73+lod);v=[];norm=[];col=[];idx=[];tips=[]
 def vertex(point,n,c):
  v.append(point);norm.append(unit(n));col.append(np.clip((np.array(c)/255.)**2.2*255,0,255).astype('uint8'));return len(v)-1
 def tube(a,b,r,s,c,sides):
  direction=unit(b-a);e=unit(np.cross(direction,np.array([0.,0.,1.])));f=np.cross(direction,e);first=len(v)
  for y,rad in [(a,r),(b,s)]:
   for k in range(sides):
    angle=math.tau*k/sides;n=e*math.cos(angle)+f*math.sin(angle)
    vertex(y+n*rad,n,np.array(c)*(.75+.25*(1+math.sin(angle*2.35))*.5))
  for k in range(sides):
   l=(k+1)%sides;idx.extend([first+k,first+l,first+sides+k,first+l,first+sides+l,first+sides+k])
 def bough(start,d,length,r,depth):
  direction=unit(d);end=start+direction*length
  if depth>=lod-1:tube(start,end,r,r*.62,(95,88,73),5 if depth>1 else 3)
  if depth==0:tips.append(end);return
  phi=math.atan2(direction[2],direction[0])
  for i in range(2 if depth<3 else 3):
   ang=phi+(-1 if i==0 else 1)*rng.uniform(.48,1.18);n=unit(direction*.42+xyz(ang,rng.uniform(.68,1.65))*.78)
   bough(end,n,length*rng.uniform(.52,.84),r*.60,depth-1)
 def cluster(c,radii,color,detail,flat=False):
  sides=[9,7,5][detail];rings=[5,4,3][detail];start=len(v);phase=geom.random()*math.tau
  for j in range(rings+1):
   theta=math.pi*j/rings
   for k in range(sides):
    a=math.tau*k/sides+phase+(j%2)*.23
    n=np.array([math.cos(a)*math.sin(theta),math.cos(theta),math.sin(a)*math.sin(theta)])
    noise=geom.uniform(.72,1.25) if j not in (0,rings) else 1
    point=c+n*np.array(radii)*noise
    tint=geom.uniform(.70,1.12)*(.72+.28*(n[1]+1)*.5)
    vertex(point,n,color*tint)
  for j in range(rings):
   for k in range(sides):
    a=start+j*sides+k;b=start+j*sides+(k+1)%sides
    idx.extend([a,b,a+sides,b,b+sides,a+sides])
 trunk=np.zeros(3);height=9.;n=12
 for j in range(n):
  t=j/n;end=np.array([.12*math.sin(t*4),height*(j+1)/n,.22*t*t]);tube(trunk,end,.17*(1-t)**.78+.008,.17*(1-(j+1)/n)**.78+.008,(90,77,63),7 if lod<2 else 4);trunk=end
 if kind=='pine':
  for j in range(36):
   t=j/36;ang=j*2.39996+rng.uniform(-.23,.23);y=3.1+t*5.5;length=(1-t)**.55*(1.35+.8*math.sin(math.pi*t))+.16;start=np.array([0.,y,0.]);d=xyz(ang,-.13);end=start+d*length
   if lod<2:tube(start,end,.034*(1-t)+.012,.009,(84,71,52),4)
   if lod==2 and j%2:continue
   for k in range(3 if lod==0 else 2):
    t2=(k+.9)/(3 if lod==0 else 2)
    p=start+d*length*t2+np.array([0.,.16,0.]);r=(.36+.52*length)*(1.-t2*.37)
    cluster(p,[r,.37+r*.35,r],np.array([49,74,44])*geom.uniform(.85,1.25),lod)
 else:
  for j in range(16):
   t=j/16;ang=j*2.39996+rng.uniform(-.18,.18);start=np.array([.05,1.85+t*5.55,0.]);length=(1-t*.71)*rng.uniform(1.15,1.65)
   bough(start,xyz(ang,.60+t*.66),length,.1*(1-t*.66),3 if kind=='winter' else 2)
  if kind=='leaf':
   for i,tip in enumerate(tips):
    if i%[1,3,9][lod]:continue
    r=geom.uniform(.43,.84)*[1.,1.32,1.72][lod]
    cluster(tip,[r,r*.8,r],np.array([65,90,37])*geom.uniform(.70,1.25),lod)
 # Normalize the geometry to one unit of height and one unit of crown width.
 verts=np.array(v,dtype='<f4');h=verts[:,1].max();width=max(verts[:,0].ptp() if hasattr(verts[:,0],'ptp') else np.ptp(verts[:,0]),np.ptp(verts[:,2]))
 verts/=np.array([width,h,width])
 normals=np.array(norm,dtype='<f4')*np.array([width,h,width]);normals/=np.maximum(.00001,np.linalg.norm(normals,axis=1)[:,None])
 colors=np.array(col,dtype='uint8');colors=np.c_[colors,np.ones(len(colors),'uint8')*255]
 data=bytearray();views=[];acc=[]
 for a,comp,typ,target in [(verts,5126,'VEC3',34962),(normals.astype('<f4'),5126,'VEC3',34962),(colors,5121,'VEC4',34962),(np.array(idx,'<u4'),5125,'SCALAR',34963)]:
  a=np.ascontiguousarray(a);views.append(dict(buffer=0,byteOffset=len(data),byteLength=a.nbytes,target=target));data.extend(a.tobytes());data.extend(bytes(-len(data)%4))
  row=dict(bufferView=len(views)-1,componentType=comp,count=len(a),type=typ)
  if comp==5121:row['normalized']=True
  if len(acc)==0:row.update(min=a.min(0).tolist(),max=a.max(0).tolist())
  acc.append(row)
 doc=dict(asset={'version':'2.0'},scene=0,scenes=[dict(nodes=[0])],nodes=[dict(mesh=0)],meshes=[dict(primitives=[dict(attributes=dict(POSITION=0,NORMAL=1,COLOR_0=2),indices=3,material=0)])],materials=[dict(doubleSided=True,pbrMetallicRoughness=dict(metallicFactor=0,roughnessFactor=.98))],buffers=[dict(byteLength=len(data))],bufferViews=views,accessors=acc)
 js=json.dumps(doc,separators=(',',':')).encode();js+=b' '*(-len(js)%4);g=struct.pack('<4sII',b'glTF',2,28+len(js)+len(data))+struct.pack('<I4s',len(js),b'JSON')+js+struct.pack('<I4s',len(data),b'BIN\0')+data
 (D/f'wood-{kind}-{lod}.meshdata').write_bytes(g);print(kind,lod,len(idx)//3,flush=True)
for kind in ['pine','winter','leaf']:
 for lod in range(3):mesh(kind,lod)
