import json,urllib.parse,urllib.request,math
from pathlib import Path
from pyproj import Transformer
from PIL import Image
import io
import os
D=Path(os.environ['SIMCRUISE_LAYOUT_WORK'])
TF=Transformer.from_crs(4326,3857,always_xy=True)
def get(file,url,cap):
 f=D/file
 if not f.is_file():
  with urllib.request.urlopen(urllib.request.Request(url,headers={'User-Agent':'SundayDriver-public-road-research/1.0'}),timeout=12) as r:raw=r.read(cap+1)
  if len(raw)>cap:raise ValueError('large response')
  f.write_bytes(raw)
  with (D/'requests.tsv').open('a') as w:w.write('%s\t%s\t%s\n'%(file,len(raw),url))
 return f
def q(base,params):return base+'?'+urllib.parse.urlencode(params)
