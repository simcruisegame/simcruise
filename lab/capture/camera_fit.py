import numpy as np, json, math, sys
from scipy.optimize import least_squares
from pathlib import Path
HERE=Path(__file__).resolve().parent
D=HERE.parent.parent/'game/data'
config=json.loads((HERE/'chula-landmarks.json').read_text())
tile=np.array([18585,25424]); scale=40075016.686/65536*math.cos(math.radians(37.3691))
obj=json.load(open(D/'18585_25424.json')); height=np.array(obj['h']).reshape(65,65)
def geo(lon,lat):
 return (np.array([(lon+180)/360*65536,(1-math.asinh(math.tan(math.radians(lat)))/math.pi)/2*65536])-tile)*scale
def ll(x,z):
 u,v=np.array([x,z])/scale+tile
 return np.array([u/65536*360-180,math.degrees(math.atan(math.sinh(math.pi*(1-2*v/65536))))])
def alt(x,z):
 gx=np.clip(x/scale*64,0,63.999); gz=np.clip(z/scale*64,0,63.999)
 i=int(gx);j=int(gz);a=gx-i;b=gz-j;return (height[j,i]*(1-a)+height[j,i+1]*a)*(1-b)+(height[j+1,i]*(1-a)+height[j+1,i+1]*a)*b
def world(q,y):
 x,z=q;return np.array([x,alt(x,z)+y,z])
def mat(yaw,pitch,roll=0):
 h,p,r=np.deg2rad([yaw,pitch,roll]);forward=np.array([np.sin(h)*np.cos(p),-np.sin(p),-np.cos(h)*np.cos(p)]);right=np.array([np.cos(h),0,np.sin(h)]);down=np.cross(right,forward)*-1
 # positive camera y means down; right cross forward points up.
 return np.array([right*np.cos(r)+down*np.sin(r),-right*np.sin(r)+down*np.cos(r),forward])
def project(worldpts,cam,ang,f,k):
 local=(worldpts-cam)@mat(*ang).T
 uv=local[:,:2]/np.maximum(.05,local[:,2:3]);r2=np.mean(uv*uv,axis=1)*2;return np.array([480,270])+uv*(f*(1+k*r2))[:,None]
frames=list(range(2057,2065))
features=list(config['features'])
gps={int(n):geo(ll[1],ll[0]) for n,ll in config['latlon'].items()}
# x/z use tile metres, y uses the surveyed elevation datum. The two fuel canopy outlines anchor scale and rotation.
canopies=[]
for f in obj['street_objects']['canopies']:
 ring=np.array(f['ring'])*scale;canopies.append(ring[[0,3,2]])
def variables(p,frame):
 i=5+3*(frame-frames[0])
 if frame in frames:angles=p[i:i+3]
 else:angles=p[5:8]
 return world(gps[frame]+p[0:2],1.65), angles,p[2],p[3]
def fixture(p,name):
 n=5+len(frames)*3+features.index(name)*3
 x,z,h=p[n:n+3]
 return np.array([world((x,z),h),world((x,z),0)])
def residual(p):
 values=[]
 for fi,n in enumerate(frames):
  cam,ang,f,k=variables(p,n)
  for j,key in enumerate(['exxon','bp']):
   row=config['anchors'][key].get(str(n))
   if row is None:continue
   ring=canopies[j]; top=alt(*ring.mean(axis=0))+4.9+p[4]
   points=np.array([[x,top,z] for x,z in ring])
   uv=project(points,cam,ang,f,k)
   for real,seen in zip(row,uv):
    if real is not None:values.extend(seen-real)
  for name in features:
   obs=config['features'][name]['tracks'].get(str(n))
   if obs is None or [str(n),name] in config['holdout']:continue
   uv=project(fixture(p,name),cam,ang,f,k)
   for real,seen in zip(obs,uv):
    if real is not None:values.extend(seen-real)
  values.extend((ang-[104,10,1])/[5,3,1.5])
  if fi:values.extend((ang-p[5+3*(fi-1):8+3*(fi-1)])/[1.8,1.2,1.])
 for name in features:
  points=fixture(p,name);orig=config['features'][name]['estimate']
  values.extend((points[0,[0,2]]-np.array(orig[:2])*scale/960)/12.)
  values.append((points[0,1]-points[1,1]-orig[2])/1.8)
 values.extend([p[0]/10,p[1]/6,(p[2]-490)/85,p[3]/.15,p[4]/.65])
 return values
def solve():
 p=[-24,-7,520,-.10,.35]+[104,10,1]*len(frames)
 for v in features:
  x,z,h=config['features'][v]['estimate'];p.extend([x*scale/960,z*scale/960,h])
 solution=least_squares(residual,p,loss='soft_l1',max_nfev=180)
 return solution.x
def verify(p):
 errors=[]
 for n,name in config['holdout']:
  uv=project(fixture(p,name),*variables(p,int(n)))
  actual=config['features'][name]['tracks'][n]
  for u,v in zip(uv,actual):
   if v is not None:errors.append(float(np.linalg.norm(u-v)))
 return errors
if __name__=='__main__':
 p=solve();out=Path(sys.argv[1]);out.write_text(json.dumps(p.tolist()))
 print('Capture offsets, focal length, radial distortion, roof height delta',p[:5].round(3))
 for n in frames:
  c,a,f,k=variables(p,n);print(n,ll(c[0],c[2]),a.round(2))
 for n in features:print(n,fixture(p,n).round(2).tolist())
 print('Held-out pixel errors at 960 by 540',np.round(verify(p),2))
