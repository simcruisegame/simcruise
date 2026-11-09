from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
from urllib.request import urlopen,Request
from PIL import Image
import io,json,math
import argparse
args=argparse.ArgumentParser(description="Fetch finite 2023 and 2025 MassGIS ortho windows for an accepted Massachusetts road event.")
args.add_argument("site",type=Path)
args=args.parse_args()
folder=args.site
chosen=json.loads((folder/"selection.json").read_text())
if chosen["result"]["status"]!="matched":raise ValueError("requires a matched state and federal road event")
lat,lon=chosen["center"][:2]
def bounded(url,maxb):
 req=Request(url,headers={'User-Agent':'SundayDriver/road-study'})
 with urlopen(req,timeout=17) as f:data=f.read(maxb+1)
 if len(data)>maxb:raise ValueError('response too big')
 return data
base='https://tiles.arcgis.com/tiles/hGdibHYSPO59RG1h/arcgis/rest/services/'
H=20037508.342789244;R=6378137
def prepare(item):
 epoch,service=item
 file=folder/f'{epoch}.jpg';meta=folder/f'{epoch}.json'
 if file.exists() and meta.exists():return
 site=base+service+'/MapServer'
 ox=R*math.radians(lon);oy=R*math.asinh(math.tan(math.radians(lat)));c=math.cos(math.radians(lat));length=136;half=length/2/c
 bounds=[ox-half,oy-half,ox+half,oy+half]; pix=2*H/256/2**20
 x0,y0=(bounds[0]+H)/pix,(H-bounds[3])/pix;x1,y1=(bounds[2]+H)/pix,(H-bounds[1])/pix
 ix0,ix1,iy0,iy1=int(x0/256),int(x1/256),int(y0/256),int(y1/256)
 canvas=Image.new('RGB',((ix1-ix0+1)*256,(iy1-iy0+1)*256));n=0;size=0;refs=[]
 for ix in range(ix0,ix1+1):
  for iy in range(iy0,iy1+1):
   u=f'{site}/tile/20/{iy}/{ix}';dat=bounded(u,180000);n+=1;size+=len(dat);refs.append(u);canvas.paste(Image.open(io.BytesIO(dat)),((ix-ix0)*256,(iy-iy0)*256))
 out=canvas.transform((1088,1088),Image.Transform.EXTENT,(x0-ix0*256,y0-iy0*256,x1-ix0*256,y1-iy0*256),Image.Resampling.BILINEAR)
 out.save(file,quality=97);meta.write_text(json.dumps({'source':site,'tileUrls':refs,'bounds':bounds,'metersPerPixel':.125,'requests':n,'bytes':size}))
 print(epoch,n,size,flush=True)
with ThreadPoolExecutor(2) as ex:list(ex.map(prepare,[('2025','Massachusetts_Aerial_Imagery_2025'),('2023','orthos2023')]))
