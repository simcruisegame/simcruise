from fetch_field import *
from PIL import Image
def grid(args):
 _,x,y=args;dest=D/f'class-{x}-{y}.png'
 fetch(v+f'VA_Base_Layers/VA_Land_Cover_Land_Use_2021/MapServer/tile/16/{y}/{x}',{},dest,'image')
def dem_index(name):
 x,y,X,Y=areas[name];west,south,east,north=extent(x,y,X,Y);proj=Transformer.from_crs(3857,4326,always_xy=True);lo=proj.transform(west,south);hi=proj.transform(east,north)
 params=dict(datasets='Digital Elevation Model (DEM) 1 meter',bbox=','.join(map(str,lo+hi)),max=60,outputFormat='JSON')
 fetch('https://tnmaccess.nationalmap.gov/api/v1/products',params,D/(name+'-dem.json'),'json')
with concurrent.futures.ThreadPoolExecutor(6) as e:
 list(e.map(dem_index,areas));list(e.map(grid,tiles()))
