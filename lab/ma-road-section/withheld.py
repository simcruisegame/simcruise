"""Independent vehicle-view check. No data from these photographs are read while authoring."""
from pathlib import Path
from PIL import Image,ImageDraw
import numpy as np,json,sys
p=Path(sys.argv[1]) if len(sys.argv)>1 else Path.cwd()
# 960x540 image coordinates. Broad windows enclose each visible roadway boundary in all three
# independent vehicle frames. Fit along rows 412–475 to avoid the distant vanishing-point region.
def fit(im,label):
 y=np.arange(412,476);dy=y-450
 windows={'yellow':(90,185,-3.75,-2.6),'white':(510,555,1.45,2.2),'curb':(730,850,3.65,5.7)}
 a,b,c,d=windows[label]; fits=[]
 for k in np.arange(c,d,.04):
  for xmid in np.arange(a,b+1,1):
   x=np.round(xmid+k*dy).astype(int)
   at=lambda off:im[y,np.clip(x+off,0,959)]
   if label=='white':support=np.min(at(0),axis=1)-(np.min(at(8),axis=1)+np.min(at(-8),axis=1))*.5
   elif label=='yellow':
    def tone(val):return val[:,0]*.55+val[:,1]*.4-val[:,2]*.3
    support=tone(at(0))-(tone(at(6))+tone(at(-6)))*.5
   else:
    def soil(val):return val[:,0]-val[:,2]
    # A gray paved shoulder ends at a low curb and brown/green planted verge.
    support=(soil(at(9))-soil(at(-9)))*.80+.20*np.linalg.norm(at(5)-at(-5),axis=1)
   fits.append((float(np.mean(np.clip(support,-15,50))),float(xmid),float(k)))
 return max(fits)
out=[]
for f in range(3):
 img=Image.open(p/f'street-{f}.jpg').resize((960,540)); im=np.asarray(img,dtype=float)
 lines={k:fit(im,k) for k in ('yellow','white','curb')};d=ImageDraw.Draw(img)
 for label,(_,x,k) in lines.items():
  d.line((x+k*(412-450),412,x+k*(475-450),475),fill={'yellow':'yellow','white':'cyan','curb':'pink'}[label],width=2)
 coords={label:np.array([x+k*(y-450) for y in (420,432,444,456,468)]) for label,(_,x,k) in lines.items()}
 ratios=(coords['curb']-coords['white'])/(coords['white']-coords['yellow'])
 out.append(dict(frame=json.loads((p/"selection.json").read_text())["frames"][f]["sequenceIndex"],lines=lines,fractions=ratios.tolist(),estimatedShoulderM=float(np.median(ratios)*7.3152)))
 img.crop((0,365,960,500)).save(p/f'optical-{f}.jpg',quality=92)
(p/'vehicle-view.json').write_text(json.dumps(out,indent=2));print([(r['frame'],r['estimatedShoulderM']) for r in out])
