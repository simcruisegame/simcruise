from pathlib import Path
import numpy as np,json
from PIL import Image,ImageDraw
D=Path(__file__).parent
# Ridge marks in the source photograph, outside the central calibration summit.
rough=[(112,198),(160,214),(186,201),(210,207),(246,169),(285,166),(320,190),(358,192),(396,151),(440,177),(470,146),(558,135),(580,130),(605,153),(630,163),(660,195),(690,216),(720,230),(762,242),(801,232),(842,231),(882,207),(920,195)]
def refine(a,x,y,radius):
 s=a[y-radius-2:y+radius+3,x-1:x+2].mean(1)
 # Locate the largest color transition immediately adjacent to each marked point.
 d=np.max(abs(s[4:]-s[:-4]),axis=1)
 return int(np.argmax(d)-radius+y)
def load(f):return np.asarray(Image.open(D/f).convert('RGB'),dtype=float)
obs=load('photograph.jpg')
marks=[(x,refine(obs,x,y,4)) for x,y in rough]
def render_y(a,x):
 # The target horizon is the first three-pixel run of solid land on the ray.
 strip=a[80:490,x-1:x+2].mean(1)
 rgb=(strip[:,0]<205)&(strip[:,2]<214)&(strip[:,1]<210)
 return int(np.argmax(np.convolve(rgb.astype(int),[1,1,1],'valid')==3)+80)
result={}
for name in ['before','after']:
 a=load(name+'.jpg');pred=[render_y(a,x) for x,y in marks];e=[abs(p-y) for p,(x,y) in zip(pred,marks)]
 result[name]={'medianVerticalErrorPx':float(np.median(e)),'meanVerticalErrorPx':float(np.mean(e)),'p90VerticalErrorPx':float(np.percentile(e,90)),'samples':[[x,y,p] for (x,y),p in zip(marks,pred)]}
 print(name,result[name]['medianVerticalErrorPx'],result[name]['meanVerticalErrorPx'],result[name]['p90VerticalErrorPx'])
 im=Image.fromarray(a.astype('uint8'));draw=ImageDraw.Draw(im)
 for (x,y),yp in zip(marks,pred):
  draw.ellipse([x-2,y-2,x+2,y+2],fill=(255,200,0))
  draw.ellipse([x-2,yp-2,x+2,yp+2],fill=(35,245,180))
 im.save(D/(name+'-marks.jpg'))
(D/'ridge-scores.json').write_text(json.dumps(result,indent=2)+'\n')
