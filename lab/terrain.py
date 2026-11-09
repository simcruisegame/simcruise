from fetch_field import *
import numpy as np,rasterio,os
from rasterio.warp import reproject,Resampling
from rasterio.transform import from_bounds
env=rasterio.Env(GDAL_DISABLE_READDIR_ON_OPEN='EMPTY_DIR',CPL_VSIL_CURL_ALLOWED_EXTENSIONS='.tif',GDAL_HTTP_CONNECTTIMEOUT=4,GDAL_HTTP_TIMEOUT=12,GDAL_HTTP_MAX_RETRY=1)
def entire(n):
 x,y,X,Y=areas[n];box=extent(x,y,X,Y);nx=(X-x+1)*64+1;ny=(Y-y+1)*64+1;pad=w/128;grid=np.full((ny,nx),-9999,dtype=np.float32);dst=from_bounds(box[0]-pad,box[1]-pad,box[2]+pad,box[3]+pad,nx,ny)
 items=json.load(open(D/(n+'-dem.json')))['items']
 for item in sorted(items,key=lambda s:('Sandy' not in s['title'],s['title'])):
  if (grid>-1000).all():break
  u=item['downloadURL']
  try:
   with rasterio.open('/vsicurl/'+u) as r:
    vals=np.full_like(grid,-9999);reproject(source=rasterio.band(r,1),destination=vals,src_transform=r.transform,src_crs=r.crs,dst_transform=dst,dst_crs='EPSG:3857',src_nodata=r.nodata,dst_nodata=-9999,resampling=Resampling.bilinear)
    mask=(vals>-1000)&(grid<-1000);grid[mask]=vals[mask];print(n,u.split('/')[-1],mask.sum(),flush=True)
  except Exception as e:print('terrain',n,str(e)[:150],flush=True)
 np.save(D/(n+'-grid.npy'),grid);print(n,'covered',np.mean(grid>-1000),flush=True)
with env,concurrent.futures.ThreadPoolExecutor(2) as e:list(e.map(entire,areas))
