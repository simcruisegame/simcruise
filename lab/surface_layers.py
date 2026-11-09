from PIL import Image,ImageFilter
import numpy as np,json
from pathlib import Path
manifest=json.loads(Path('lab/final-pack.json').read_text())
for name,src in list(manifest.items()):
 if not name.startswith('data/pave-'):continue
 coords=Path(src).stem.replace('pave-','')
 base=Image.open(src).convert('RGB').getchannel('R');cover=np.array(Image.open(f'lab/field/class-{coords}.png').convert('RGB'),int)
 rgb=[[233,255,190],[115,115,0],[255,211,127],[170,255,0],[255,170,0],[38,115,0],[56,168,0]]
 grassy=np.min([np.sum((cover-c)**2,axis=2) for c in rgb],axis=0)<32**2
 green=Image.fromarray((grassy*255).astype('uint8')).resize(base.size,Image.Resampling.BILINEAR).filter(ImageFilter.GaussianBlur(1.5))
 Image.merge('RGB',(base,green,Image.new('L',base.size))).save(src)
manifest['data/grass-detail.jpg']='game/data/grass-detail.jpg'
Path('lab/final-pack.json').write_text(json.dumps(manifest))
