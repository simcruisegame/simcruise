"""Build the public-domain distant-ocean mask. Takes a verified Natural Earth 5.1.1 ZIP and output JSON."""
import argparse
import base64
import hashlib
import json
import sys
import zlib
from pathlib import Path
import numpy as np
import shapefile
import shapely
from rasterio.features import rasterize
from rasterio.transform import from_bounds

pixel_count = 256  # Per geographic degree, roughly 435 m north to south.
bounds = [
    (-129, 22, -63, 52), (-180, 48, -126, 74), (169, 48, 180, 74),
    (-180, 17, -151, 31), (-70, 15, -62, 21),
    (141, 11, 149, 23), (-173, -16, -166, -10), (158, 5, 166, 23),
    (-179, -2, -172, 4), (-162, -2, -154, 10), (-82, 17, -72, 21),
]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument("zip",type=Path); p.add_argument("output",type=Path)
args=p.parse_args()
expected="db626fcd5d50b096b156c78a2cc95011b39f32a61b4e47d147e3f7a77b8b2719"
assert hashlib.sha256(args.zip.read_bytes()).hexdigest()==expected
reader = shapefile.Reader(str(args.zip))
# Natural Earth's ESRI clockwise shell convention is interpreted by PyShp.
from shapely.geometry import shape
geometry=shape(reader.shape(0).__geo_interface__)
cells={}
for xmin,ymin,xmax,ymax in bounds:
 for west in range(xmin,xmax,5):
  east=min(west+5,xmax)
  for south in range(ymin,ymax,5):
   north=min(south+5,ymax)
   sea=shapely.clip_by_rect(geometry,west,south,east,north)
   if sea.is_empty:continue
   w=(east-west)*pixel_count;h=(north-south)*pixel_count
   raster=rasterize([(sea,1)],out_shape=(h,w),transform=from_bounds(west,south,east,north,w,h),dtype='uint8')
   for ix in range(east-west):
    for iy in range(north-south):
     tile=raster[iy*pixel_count:(iy+1)*pixel_count,ix*pixel_count:(ix+1)*pixel_count]
     if not tile.any():continue
     key=f"{west+ix},{north-iy-1}"
     cells[key]=1 if tile.all() else base64.b64encode(zlib.compress(np.packbits(tile,bitorder='big').tobytes(),9)).decode()
result={"dataset":"Natural Earth 1:10m ocean, 5.1.1","license":"Public domain","source":"https://www.naturalearthdata.com/downloads/10m-physical-vectors/10m-ocean/","sourceSHA256":expected,"samplesPerDegree":pixel_count,"tiles":cells}
args.output.write_text(json.dumps(result,separators=(',',':'))+'\n')
print(f"{len(cells)} ocean and coastal cells, {args.output.stat().st_size:,} bytes")
