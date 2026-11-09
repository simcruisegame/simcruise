"""Fit a lightweight pavement classifier. Hold out an entire strip of Chesterfield tiles."""
import json
from pathlib import Path
import numpy as np
from scipy.optimize import minimize
from scipy.ndimage import uniform_filter
from PIL import Image,ImageDraw
D=Path('game/data')
tiles=[]
for f in sorted(D.glob('*.json')):
 if not f.stem[0].isdigit():continue
 t=json.loads(f.read_text())
 if t.get('region')!='Raven':continue
 x,y=t['id'];N=256;ppm=N/(611.496/np.cosh(np.pi*(1-2*(y+.5)/65536)))
 target=np.asarray(Image.open(D/f'pave-{x}-{y}.png').resize((N,N),Image.Resampling.BOX))[:,:,0]>127
 rgb=np.asarray(Image.open(D/f'va-{x}-{y}.jpg').convert('RGB').resize((N,N),Image.Resampling.BOX),float)/255.
 prior=Image.new('L',(N*4,N*4));brush=ImageDraw.Draw(prior)
 for extra,value in [(7,30),(4,70),(2,100),(0,155),(-2,190),(-4,225),(-6,249)]:
  for road in t['roads']:
   pts=[tuple(int(c*(N*4)) for c in point[:2]) for point in road[3]]
   width=round((road[4]+extra)*ppm*4)
   if width<1:continue
   brush.line(pts,fill=value,width=width,joint='curve')
 prior=np.asarray(prior.resize((N,N),Image.Resampling.BOX),float)/255.
 use=(prior>.03)&(prior<.94)
 if use.sum()<40:continue
 r,g,b=rgb.transpose(2,0,1);l=(r+g+b)/3
 samples=np.stack([np.ones_like(r),prior,r-g,g-b,l,np.abs(l-uniform_filter(l,3)),np.abs(r-g)+np.abs(g-b)],axis=-1)[use]
 tiles.append((x,y,samples,target[use]))
def merge(rows):
 return np.concatenate([t[2] for t in rows]),np.concatenate([t[3] for t in rows])
xs=sorted(set(v[0] for v in tiles))
train,label=merge([t for t in tiles if t[0] not in xs[-2:]])
test,answer=merge([t for t in tiles if t[0] in xs[-2:]])
def fit(x,y):
 def objective(w):
  score=x@w
  probs=1/(1+np.exp(-np.clip(score,-40,40)))
  return np.mean(np.logaddexp(0,score)-y*score)+.00001*np.sum(w[1:]**2), x.T@(probs-y)/len(y)+.00002*np.r_[0,w[1:]]
 return minimize(objective,np.zeros(x.shape[1]),jac=True,method='BFGS',options={'maxiter':1500,'gtol':1e-8}).x
w=fit(train,label)
def iou(pred):return round(np.sum(answer&pred)/np.sum(answer|pred),4)
geo=fit(train[:,:2],label)
print('held out tile columns',xs[-2:],'train samples',len(train),'holdout',len(test),'baseline',iou(test[:,1]>.5),'calibrated shape only',iou(test[:,:2]@geo>0),'shape + imagery',iou(test@w>0),'weights',w.tolist())

