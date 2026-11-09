from PIL import Image,ImageDraw,ImageFilter
from pathlib import Path
import json,math
manifest=json.loads(Path('lab/final-pack.json').read_text())
for item,src in list(manifest.items()):
 if 'data/' not in item or not item.endswith('.json') or 'places' in item:continue
 d=json.loads(Path(src).read_text());x,y=d['id'];side=611.496*math.cos(math.atan(math.sinh(math.pi*(1-2*(y+.5)/65536))))
 N=1536;mask=Image.new('L',(N,N));draw=ImageDraw.Draw(mask)
 for cls,_,name,pts,width in d['roads']:
  px=[(round(u*N),round(v*N)) for u,v in pts];r=max(1,round(width*N/side))
  draw.line(px,fill=255,width=r,joint='curve')
  for a,b in px:
   rad=r/2;draw.ellipse((a-rad,b-rad,a+rad,b+rad),fill=255)
 dst=Path(f'game/data/pave-{x}-{y}.png');mask.filter(ImageFilter.GaussianBlur(.85)).resize((768,768),Image.Resampling.LANCZOS).save(dst);manifest['data/'+dst.name]=str(dst)
Path('lab/final-pack.json').write_text(json.dumps(manifest))
