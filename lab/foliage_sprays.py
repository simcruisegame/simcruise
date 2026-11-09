"""Build three-dimensional limbs with folded, textured sprays for daylight foliage.
Run from the source root after woodland.py. Poly Haven source images are public CC0.
"""
import io, math, json, random, struct, sys
from pathlib import Path
from PIL import Image, ImageDraw
import numpy as np, requests
D=Path('game/data')
def sample(asset,channel):
 data=requests.get('https://api.polyhaven.com/files/'+asset,timeout=15).json()
 def load(suffix):
  v=data[channel+'_'+suffix]['1k'];item=v.get('jpg') or v.get('png')
  res=requests.get(item['url'],timeout=25);res.raise_for_status();return Image.open(io.BytesIO(res.content))
 image=load('diff').convert('RGBA');image.putalpha(load('alpha').convert('L'));image.thumbnail((512,512));return image
def spray_atlas():
 # Choose clean photographic texels from the botanical texture sheets.
 needle=sample('pine_tree_01','twig').crop((10,8,116,223))
 leaf=sample('tree_small_02','leaves').crop((273,240,353,287))
 mask=Image.new('L',leaf.size);draw=ImageDraw.Draw(mask);draw.ellipse((5,5,74,40),fill=255)
 leaf.putalpha(Image.fromarray(np.minimum(np.array(leaf.getchannel('A')),np.array(mask))))
 for kind,source in [('pine',needle),('leaf',leaf)]:
  out=Image.new('RGBA',(256,256));d=ImageDraw.Draw(out);rng=random.Random(517)
  d.line([(128,255),(121,136),(129,45),(132,2)],fill=(97,85,52,255),width=3)
  for j in range(12 if kind=='pine' else 10):
   y=230-j*(18 if kind=='pine' else 20);s=math.sin(math.pi*(j+1)/14)**.55
   for sign in [-1,1]:
    x=126+sign*(45+36*s)+rng.randint(-5,5);end_y=y-37-rng.randint(0,18)
    d.line([(126,y),(x,end_y)],fill=(92,87,53,255),width=2)
    if kind=='pine':
     part=source.resize((round(35+23*s),round(61+26*s)),Image.Resampling.LANCZOS).rotate(-sign*57,Image.Resampling.BICUBIC,expand=True)
     out.alpha_composite(part,(int((126+x)/2-part.width/2),int((end_y+y)/2-part.height/2)))
    else:
     for t in [.45,.85,1.18]:
      part=source.resize((int((22+17*s)),int((12+11*s))),Image.Resampling.LANCZOS).rotate(sign*(15+rng.randint(-22,32)),Image.Resampling.BICUBIC,expand=True)
      out.alpha_composite(part,(round(126+(x-126)*t-part.width/2),round(y+(end_y-y)*t-part.height/2)))
  out.save(D/('spray-'+kind+'.png'))
def model(kind,lod):
 rng=random.Random(712 if kind=='pine' else 954);wood=[];sprays=[];wi=[];si=[]
 def vec(x,y,z):return np.array([x,y,z],dtype=float)
 def unit(a):return a/max(1e-8,np.linalg.norm(a))
 def tube(a,b,r,rr):
  direction=unit(b-a);right=unit(np.cross(direction,vec(0,0,1)));other=np.cross(direction,right);k=len(wood);sides=5 if lod<2 else 3
  for at,rad in [(a,r),(b,rr)]:
   for j in range(sides):
    angle=j*math.tau/sides;n=right*math.cos(angle)+other*math.sin(angle);wood.append((at+n*rad,n,(0,0)))
  for j in range(sides):
   l=(j+1)%sides;wi.extend([k+j,k+l,k+sides+j,k+l,k+sides+l,k+sides+j])
 def fold(base,direction,length,width,roll):
  f=unit(direction);w=unit(np.cross(f,vec(0,1,0)));up=np.cross(w,f);w=w*math.cos(roll)+up*math.sin(roll);up=unit(np.cross(w,f));k=len(sprays)
  for t in [0,.55,1.]:
   center=base+f*(t*length)+vec(0,-.15*t*t*length,0)
   for x in [-1,0,1]:
    p=center+w*(x*width*.5)+up*(abs(x)*.10*width)
    sprays.append((p,unit(up+vec(0,.7,0)),((x+1)*.5,1.-t)))
  for j in [0,1,3,4]:si.extend([k+j,k+j+1,k+j+3,k+j+1,k+j+4,k+j+3])
 h=10.;last=vec(0,0,0);bc=45 if kind=='pine' else 34
 for j in range(12):
  t=(j+1)/12;end=vec(.16*math.sin(t*4),t*h,.18*t**2)
  tube(last,end,.17*((13-j)/13)**.83,.17*((12-j)/13)**.83);last=end
 for j in range(bc):
  t=j/bc;angle=j*2.39996+rng.uniform(-.23,.23);radial=vec(math.cos(angle),0,math.sin(angle));side=np.cross(radial,vec(0,1,0))
  at=vec(.10,(2.65 if kind=='pine' else 1.7)+(6.9 if kind=='pine' else 7.3)*t,.03)
  length=(1.15+1.85*max(0.,math.sin(math.pi*(t+.06)))**.62)*(1.-.68*t)*rng.uniform(.77,1.18)
  lift=rng.uniform(-.11,.17) if kind=='pine' else rng.uniform(.32,.76)
  mid=at+length*(radial*.44+vec(0,lift*.38,0));end=at+length*(radial+vec(0,lift+.12,0))
  tube(at,mid,.071*(1.-t*.65),.044*(1.-t*.6));tube(mid,end,.044*(1.-t*.6),.009)
  pairs=[6,4,3][lod]
  for i in range(pairs):
   t2=(i+.6)/(pairs+.1);pivot=mid*(1.-t2)+end*t2
   for sign in [-1,1]:
    spread=(.28+.37*(1.-t2))*length
    d=unit(radial*.45+side*sign*.82+vec(0,rng.uniform(.02,.50),0))
    reach=spread*[1.,1.13,1.28][lod];finish=pivot+d*reach
    if lod<2:tube(pivot,finish,.018,.004)
    fold(pivot,d,reach*1.23,reach*.95,rng.uniform(-.7,.7))
    if lod==0:fold(pivot+d*reach*.28,d+side*.25*sign,reach*.88,reach*.72,1.15)
  fold(mid,unit(end-mid),np.linalg.norm(end-mid)*1.3,length*.66,.12)
 allpos=np.array([v[0] for v in wood+sprays]);height=allpos[:,1].max();width=max(np.ptp(allpos[:,0]),np.ptp(allpos[:,2]));scale=vec(width,height,width)
 data=bytearray();views=[];acc=[];prims=[]
 def attr(values,comp,size,target):
  a=np.ascontiguousarray(values,dtype='<u4' if comp==5125 else '<f4');views.append(dict(buffer=0,byteOffset=len(data),byteLength=a.nbytes,target=target));data.extend(a.tobytes());data.extend(bytes(-len(data)%4))
  spec=dict(bufferView=len(views)-1,componentType=comp,count=len(a),type=size)
  if size=='VEC3':spec.update(min=a.min(0).tolist(),max=a.max(0).tolist())
  acc.append(spec);return len(acc)-1
 for mat,vertices,indices in [(0,wood,wi),(1,sprays,si)]:
  pos=attr([p/scale for p,n,u in vertices],5126,'VEC3',34962);norm=attr([unit(n*scale) for p,n,u in vertices],5126,'VEC3',34962);uv=attr([u for p,n,u in vertices],5126,'VEC2',34962)
  prims.append(dict(attributes=dict(POSITION=pos,NORMAL=norm,TEXCOORD_0=uv),indices=attr(indices,5125,'SCALAR',34963),material=mat))
 materials=[dict(doubleSided=True,pbrMetallicRoughness=dict(baseColorFactor=[.09,.073,.055,1],metallicFactor=0,roughnessFactor=.97)),dict(doubleSided=True,pbrMetallicRoughness=dict(metallicFactor=0,roughnessFactor=.93))]
 doc=dict(asset={'version':'2.0'},scene=0,scenes=[dict(nodes=[0])],nodes=[dict(mesh=0)],meshes=[dict(primitives=prims)],materials=materials,buffers=[dict(byteLength=len(data))],bufferViews=views,accessors=acc)
 js=json.dumps(doc,separators=(',',':')).encode();js+=b' '*(-len(js)%4)
 glb=struct.pack('<4sII',b'glTF',2,28+len(js)+len(data))+struct.pack('<I4s',len(js),b'JSON')+js+struct.pack('<I4s',len(data),b'BIN\0')+data
 (D/f'wood-{kind}-{lod}.meshdata').write_bytes(glb)
spray_atlas()
for kind in ['leaf','pine']:
 for lod in range(3):model(kind,lod)
