import json,laspy,math,numpy as np
from pyproj import Transformer,CRS
from shapely.geometry import Polygon,mapping
from pathlib import Path
import argparse, hashlib
parser=argparse.ArgumentParser(description='Prepare a reproducible rooftop validation set in metric coordinates.')
parser.add_argument('laser_file',type=Path);parser.add_argument('output',type=Path)
cfg=parser.parse_args(); N=cfg.output;N.mkdir(parents=True,exist_ok=True)
study=Path(__file__).resolve().parent.parent/'bartlesville'
source=json.loads((study/'source.json').read_text());src=cfg.laser_file
if hashlib.sha256(src.read_bytes()).hexdigest()!=source['sha256']:raise ValueError('Unexpected survey tile')
pc=laspy.read(src);crs=CRS.from_epsg(32615);xf=Transformer.from_crs(3857,crs,always_xy=True);X,Y=xf.transform(pc.x,pc.y);Z=np.asarray(pc.z)
hold=((np.floor(X).astype(int)+2*np.floor(Y).astype(int))%5)==0
L=laspy.LasData(laspy.LasHeader(version='1.2',point_format=3));L.header.add_crs(crs);L.header.offsets=[math.floor(X.min()),math.floor(Y.min()),200];L.header.scales=[.001]*3
L.x=X[~hold];L.y=Y[~hold];L.z=Z[~hold];L.classification=np.asarray(pc.classification)[~hold];L.point_source_id=np.asarray(pc.point_source_id)[~hold];L.write(N/'train.las')
np.savez(N/'held.npz',x=X[hold],y=Y[hold],z=Z[hold],cls=np.asarray(pc.classification)[hold])
data=json.loads((study/'base-tile.json').read_text());tx,ty=data['id'];radius=20037508.342789244;side=2*radius/65536
features=[]
for k in [2,4]:
 rings=data['buildings'][k][0];outer=[xf.transform((tx+x)*side-radius,radius-(ty+y)*side) for x,y in rings[0]];poly=Polygon(outer)
 features.append(dict(type='Feature',properties={'bid':str(k)},geometry=mapping(poly)))
(N/'footprints.geojson').write_text(json.dumps(dict(type='FeatureCollection',crs={'type':'name','properties':{'name':'urn:ogc:def:crs:EPSG::32615'}},features=features)))
print('train',len(L.points),'held',int(hold.sum()))
