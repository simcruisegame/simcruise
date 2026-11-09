from math import cos,radians,atan2,degrees,hypot,dist
from pathlib import Path
import json,sys,os
work=Path(os.environ.get('SD_SECTION_WORK',Path(__file__).parent))
def path_rows(geo):
 return geo['coordinates'] if geo['type']=='MultiLineString' else [geo['coordinates']]
def nearest(line,pt):
 x,y=pt; closest=None; length=0
 for a,b in zip(line,line[1:]):
  dx,dy=b[0]-a[0],b[1]-a[1];m=hypot(dx,dy)
  if m<.01:continue
  t=min(1,max(0,((x-a[0])*dx+(y-a[1])*dy)/m**2));q=a[0]+t*dx,a[1]+t*dy
  item=(dist(pt,q),length+t*m,degrees(atan2(dx,dy))%360,q)
  if closest is None or item[0]<closest[0]:closest=item
  length+=m
 return closest
def along(line,s):
 if s<0:return None
 for a,b in zip(line,line[1:]):
  m=dist(a,b)
  if m<.01:continue
  if s<=m:return (a[0]+(b[0]-a[0])*s/m,a[1]+(b[1]-a[1])*s/m),degrees(atan2(b[0]-a[0],b[1]-a[1]))%360
  s-=m
 return None
def err(a,b):
 d=abs((a-b+180)%360-180);return min(d,180-d)
def read(label,loc):
 lat,lon,head=loc
 def xy(ll):return ((ll[0]-lon)*111319.49079*cos(radians(lat)),(ll[1]-lat)*111319.49079)
 mapd=json.loads((work/(label+'-map.json')).read_text());obs=json.loads((work/(label+'-inventory.json')).read_text());allsets={}
 for name in ['road','name']:
  allsets[name]=[(f['id'] if 'id' in f else '',f['properties'],[xy(q) for q in line]) for f in mapd[name] for line in path_rows(f['geometry']) if len(line)>1]
 for name,data in obs.items():
  if name=='state':continue
  allsets[name]=[(f['attributes'].get('OBJECTID',f['attributes'].get('objectid')), f['attributes'],[xy(q) for q in line]) for f in data['features'] for line in f.get('geometry',{}).get('paths',[]) if len(line)>1]
 return allsets
def nearrows(rows,pt,heading,threshold=15):
 out=[]
 for fid,attrs,line in rows:
  n=nearest(line,pt)
  if not n or err(n[2],heading)>threshold:continue
  out.append((n[0],fid,attrs,line,n))
 return sorted(out,key=lambda r:r[0])
if __name__=='__main__':
 for label,loc in json.loads((work/'locations.json').read_text()).items():
  if not (work/(label+'-inventory.json')).exists():continue
  data=read(label,loc);print('\n',label,loc)
  for name,rows in data.items():
   out=nearrows(rows,(0,0),loc[2]);print(name,[(round(r[0],1),r[1],{k:v for k,v in r[2].items() if k in ('ref','name','class','oneway','route_id','routename','routenumber','through_lanes','lane_width','facility_type','median_type','median_width','shoulder_type','shoulder_width_r','dir_through_lanes','RdwyWidth','NumberOfLanes','ShoulderWidth','St_Name','Surface_Wd','Num_Lanes','Med_Type','RouteIdentifier','TrffcwayFeatOrientCd','SnapshotDate')},[round(r[4][1]),round(sum(dist(a,b) for a,b in zip(r[3],r[3][1:])))]) for r in out[:3]])
