from pathlib import Path
import numpy as np,sys
from PIL import Image
from scipy.ndimage import median_filter,gaussian_filter1d
D=Path(__file__).parent
def skyline(f,real):
 a=np.array(Image.open(f).convert('RGB'),dtype=float)
 # Strip contains sky and land only. Reject isolated compression differences.
 if real: sky=(a[:,:,2]>a[:,:,0]*1.04)&(a[:,:,2]>a[:,:,1]*1.015)
 else: sky=(a[:,:,2]>a[:,:,0]*1.025)&(a[:,:,2]>=a[:,:,1]*.99) | (a.min(axis=2)>180)
 raw=np.argmax(~sky[310:432],axis=0)+310
 return median_filter(raw,3).astype(float)
true=skyline(D/'photograph.jpg',True);i=np.arange(8,952);train=(i>340)&(i<620)
for f in sys.argv[1:] or ['before','after']:
 p=skyline(D/(f+'.jpg'),False);off=np.median((true-p)[i[train]]);t=i[~train];err=p[t]-true[t]
 print(f,'95pct range',np.percentile(p,[1,99]),'absolute mae',abs(err).mean(),'shape mae',abs(err+off).mean(),'offset',off,'edge slope correlation',np.corrcoef(gaussian_filter1d(true,2,order=1)[t],gaussian_filter1d(p,2,order=1)[t])[0,1])
