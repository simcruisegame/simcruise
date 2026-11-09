"""Compare mountain faces at a fixed pose using separately marked photographed summit outlines."""
from pathlib import Path
import argparse,json
import numpy as np
from PIL import Image
from scipy.ndimage import gaussian_filter
def score(reference,marks,files):
 ref=np.array(Image.open(reference).convert('RGB'),dtype=float)
 points=json.loads(Path(marks).read_text())['after']['samples']
 x=np.arange(180,786)
 y=np.arange(ref.shape[0])[:,None]
 horizon=np.interp(x,[p[0] for p in points],[p[1] for p in points])[None,:]
 mask=(y>=horizon+12)&(y<=horizon+85)&(y<=310)
 def measures(im):
  a=im[:,x,:]
  lumin=a@np.array([.2126,.7152,.0722]);coarse=gaussian_filter(lumin,4.)
  return a[mask],lumin[mask],(lumin-coarse)[mask]
 obs,og,od=measures(ref)
 results={}
 for path in files:
  im=np.array(Image.open(path).convert('RGB'),dtype=float)
  if im.shape[0]==720:im=im[48:688]
  a,g,d=measures(im)
  results[Path(path).stem]={'rgb_error':round(float(np.abs(a-obs).mean()),2),'luminance_correlation':round(float(np.corrcoef(g,og)[0,1]),3),'luminance_spread':round(float(g.std()),2),'reference_spread':round(float(og.std()),2),'highpass_correlation':round(float(np.corrcoef(d,od)[0,1]),3),'samples':len(g)}
 return results
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('reference');p.add_argument('marks');p.add_argument('images',nargs='+');a=p.parse_args();print(json.dumps(score(a.reference,a.marks,a.images),indent=2))
