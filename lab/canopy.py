from fetch_field import D,w,half,areas
from pathlib import Path
import numpy as np,laspy,sys
from pyproj import Transformer
from scipy.ndimage import map_coordinates,distance_transform_edt
x,y,X,Y=areas['Chula'];ground=np.load(D/'Chula-grid.npy')
top=np.zeros(((Y-y+1)*256,(X-x+1)*256),dtype=np.float32)
with laspy.open(sys.argv[1]) as source:
 transform=Transformer.from_crs(source.header.parse_crs(),3857,always_xy=True)
 errors=[]
 for chunk in source.chunk_iterator(500000):
  ex,ny=transform.transform(chunk.x,chunk.y);u=((ex+half)/w-x);v=((half-ny)/w-y);ok=(u>=0)&(v>=0)&(u<X-x+1)&(v<Y-y+1)
  if not np.any(ok):continue
  u=u[ok];v=v[ok];z=np.array(chunk.z)[ok];cl=np.array(chunk.classification)[ok]
  agl=z-map_coordinates(ground,[v*64,u*64],order=1,mode='nearest')
  errors.extend(agl[cl==2][::250]);valid=(agl>=-1)&(agl<45)&(cl!=7)&(cl!=18)
  np.maximum.at(top,((v[valid]*256).astype(int),(u[valid]*256).astype(int)),np.maximum(.1,agl[valid]))
 print('median ground residual metres',np.median(errors),flush=True)
 if abs(np.median(errors))>2:raise ValueError('Check vertical reference')
for j in range(Y-y+1):
 for i in range(X-x+1):
  a=top[j*256:(j+1)*256,i*256:(i+1)*256]
  if np.count_nonzero(a)<900:continue
  dist,indices=distance_transform_edt(a==0,return_indices=True);a=np.where(dist<=2,a[tuple(indices)],0)
  np.save(D/f'cloud-{x+i}-{y+j}.npy',a)
