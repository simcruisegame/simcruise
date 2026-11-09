"""Single scattering in an Earth-sized sphere, CC0 measured cloud ranks. Lengths in km."""
from pathlib import Path
from PIL import Image
import numpy as np
import argparse
from hashlib import md5
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument("stock_hdr",type=Path)
parser.add_argument("clear_hdr",type=Path)
parser.add_argument("output",type=Path)
args=parser.parse_args()
if md5(args.clear_hdr.read_bytes()).hexdigest()!="ebc2045f4fa29ba75cd3834089ad8d14":
 raise ValueError("Expected Poly Haven kloofendal_43d_clear_puresky 1k HDR")
D=args.output
D.mkdir(parents=True,exist_ok=True)
def decode(path):
 f=open(path,"rb")
 while not (line:=f.readline()).startswith(b'-Y '):pass
 _,H,_,W=line.decode().split();h,w=int(H),int(W);raw=np.empty((h,w,4),np.uint8)
 for y in range(h):
  assert f.read(2)==b'\x02\x02';assert int.from_bytes(f.read(2),'big')==w
  for c in range(4):
   k=0
   while k<w:
    n=f.read(1)[0]
    if n>128:n-=128;pix=f.read(1)[0];raw[y,k:k+n,c]=pix
    else:raw[y,k:k+n,c]=np.frombuffer(f.read(n),np.uint8)
    k+=n
 rgb=np.ldexp(raw[:,:,:3].astype(float),raw[:,:,3:4].astype(int)-136)
 f.close()
 return rgb.astype("f4")

# Standard molecular/aerosol coefficients used by Bruneton's published Earth reference (680/550/440 nm).
R=6360.;top=6460.;B=np.array([.005802,.013558,.033100]);A=.00444;g=.8
# Infer reference top-of-atmosphere irradiance from the previously shipped HDR disk and physical clear-air attenuation.
beam=np.array([4.41930,4.46174,4.07292])
solar=beam*np.exp((B*8*np.exp(-1.6/8)+A*1.2*np.exp(-1.6/1.2))/np.sin(np.radians(48)))
def path_to_space(r,mu):
 return np.sqrt((r*mu)**2+(top-r)*(top+r))-r*mu
def density(r):
 h=np.maximum(r-R,0);return np.exp(-h/8),np.exp(-h/1.2)
N,H,W=17,48,81
az,el=np.meshgrid(np.linspace(0,np.pi,W),np.linspace(0,np.pi/2,H))
ray_mu=np.sin(el);pixels=[];n=28;m=12;r=R+1.0
d=path_to_space(r,ray_mu);v_edges=np.linspace(0,1,n+1)**2;s_edges=np.linspace(0,1,m+1)**2
for sun_el in np.linspace(3,83,N):
 smu=np.sin(np.radians(sun_el));nu=ray_mu*smu+np.cos(el)*np.cos(np.radians(sun_el))*np.cos(az)
 phaseR=3/(16*np.pi)*(1+nu**2);phaseM=3/(8*np.pi)*(1-g*g)*(1+nu**2)/((2+g*g)*(1+g*g-2*g*nu)**1.5)
 scatter=np.zeros((H,W,3));vtR=np.zeros_like(el);vtM=vtR.copy()
 for l,u in zip(v_edges[:-1],v_edges[1:]):
  t=d*(l+u)*.5;dt=d*(u-l);rr=np.sqrt(r*r+2*r*ray_mu*t+t*t);sunmu=(r*smu+t*nu)/rr;s=path_to_space(rr,sunmu);rv,mv=density(rr)
  t2=s[...,None]*(s_edges[:-1]+s_edges[1:])*.5;ds=s[...,None]*np.diff(s_edges)
  rs,ms=density(np.sqrt(rr[...,None]**2+2*(rr*sunmu)[...,None]*t2+t2*t2));sr=np.sum(rs*ds,2);ss=np.sum(ms*ds,2)
  dr=rv*dt;dm=mv*dt;ext=(vtR+.5*dr+sr)[...,None]*B+(vtM+.5*dm+ss)[...,None]*A
  scatter+=np.exp(-ext)*(dr[...,None]*B*phaseR[...,None]+dm[...,None]*A*.9*phaseM[...,None])
  vtR+=dr;vtM+=dm
 pixels.append(scatter*solar)
arr=np.concatenate(pixels);Image.fromarray(np.uint8(np.clip(np.sqrt(arr/4)*255+.5,0,255))).save(D/'day-scattering.png')
hdr=decode(args.stock_hdr);clr=decode(args.clear_hdr);y=np.arange(512)
# Match clear exposure in a neutral full-sun environment from the integrated direct irradiance (green band).
gain=4.46173566/6.12009768
def pack(a,name):Image.fromarray(np.uint8(np.clip(np.sqrt(a/4)*255+.5,0,255))).save(D/name)
pack(clr[:256:2,::2]*gain,'day-clear.png')
# Find clouds by saturation and relative brightness within each original sky elevation.
a=hdr[:224:2,::2];ratio=(a[...,2]+.001)/(a[...,0]+.001)
signal=np.log(np.maximum(a.mean(2),.001)/np.maximum(np.percentile(a.mean(2),15,axis=1)[:,None],.001))-.9*np.log(ratio/np.percentile(ratio,80,axis=1)[:,None])
sf=signal.flatten();weights=np.repeat(np.sin((np.arange(len(signal))*2+.5)*np.pi/512),signal.shape[1]);order=np.argsort(sf)
rank=np.empty(sf.shape);rank[order]=np.cumsum(weights[order])/weights.sum();im=Image.fromarray(np.uint8(rank.reshape(signal.shape)*255));im.save(D/'day-cloud-rank.png')
print('top irradiance',solar.round(3),'range',arr.min(),arr.max(),'clear exposure in stock HDR units',gain)
