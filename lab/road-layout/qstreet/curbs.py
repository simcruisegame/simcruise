from source import *
import numpy as np
W=38.9109,-77.0271
from shapely.geometry import Point,LineString
from scipy.ndimage import map_coordinates,gaussian_filter1d
import re
from collections import Counter
def normalized(s):
 for a,b in [('STREET','ST'),('AVENUE','AVE'),('NORTHWEST','NW'),('NORTHEAST','NE'),('SOUTHEAST','SE'),('SOUTHWEST','SW')]:
  s=re.sub(r'\b'+a+r'\b',b,s.upper())
 return s
def edges(ls,c,tangent,scale):
 normal=np.array([tangent[1],-tangent[0]]);tran=LineString([c-normal*19/scale,c+normal*19/scale]);cross=[]
 for geom in ls:
  g=geom.intersection(tran)
  if g.is_empty:continue
  parts=[g] if g.geom_type=='Point' else list(g.geoms) if hasattr(g,'geoms') else []
  for v in parts:
   if v.geom_type!='Point':continue
   station=geom.project(v);a=np.array(geom.interpolate(max(0,station-.5/scale)).coords[0]);b=np.array(geom.interpolate(min(geom.length,station+.5/scale)).coords[0]);d=b-a
   if np.linalg.norm(d)==0 or abs(d@tangent)/np.linalg.norm(d)<.95:continue
   cross.append(float((np.array(v.coords[0])-c)@normal)*scale)
 def pick(sign):
  group=sorted(abs(x) for x in cross if (x*sign)>0)
  if not group or not 2.8<=group[0]<=14.5 or len(group)>1 and group[1]-group[0]<1.2:return None
  return sign*group[0]
 return [pick(-1),pick(1)]
def compare(second_shift=(0.,0.),photo_shift=(0.,0.),lane_overrides=None,dest="dc-proof.json"):
 papers={}
 for year in [2021,2023]:
  data=json.loads((D/f'dc-curb-{year}-block.json').read_text());papers[year]=[LineString(np.array(g)+np.array(second_shift)) if year==2023 else LineString(g) for f in data['features'] if f['attributes']['FEATURECODE']==1040 for g in f['geometry']['paths']]
 width=json.loads((D/'dc-width-source-block.json').read_text());blocks=[(r['attributes'],LineString(line)) for r in width['features'] for line in r['geometry']['paths']]
 roads=json.loads((D/'Washington-roads.json').read_text());m=json.loads((D/'dc-aerial.json').read_text());xmin,ymin,xmax,ymax=m['bbox'];scale=m['scale'];im=np.asarray(Image.open(D/'dc-aerial.jpg').convert('L')).astype(float);origin=np.array(TF.transform(W[1],W[0]));output=[];counts=Counter()
 for road in roads:
  tags=road['tags'].copy();tags.update((lane_overrides or {}).get(road['id'],{}));name=normalized(tags['name']);way=LineString(road['xy']);length=way.length*scale
  for st in np.arange(14,length-13.9,6.):
   c=np.array(way.interpolate(st/scale).coords[0]);ahead=np.array(way.interpolate((st+3)/scale).coords[0]);back=np.array(way.interpolate((st-3)/scale).coords[0]);d=ahead-back;t=d/np.linalg.norm(d);n=np.array([t[1],-t[0]])
   if np.linalg.norm(c-origin)*scale>95:continue
   matches=[(a,k) for a,k in blocks if k.distance(Point(c))*scale<2.5 and name in normalized(a['BLOCK_NAME'])]
   if len(matches)!=1:counts['ambiguous_inventory']+=1;continue
   attr,owned=matches[0]
   nearest=owned.project(Point(c))*scale
   if min(nearest,owned.length*scale-nearest)<14:counts['near_junction']+=1;continue
   ed=[edges(ls,c,t,scale) for yr,ls in papers.items()]
   if None in ed[0] or None in ed[1] or np.max(np.abs(np.diff(ed,axis=0)))>.6:counts['uncorroborated_curbs']+=1;continue
   e=np.mean(ed,axis=0);uwidth=(e[1]-e[0]);reserve=attr.get('TOTALCROSSSECTIONWIDTH') or 0
   if reserve<1 or abs(reserve*.3048-uwidth)>max(1.3,.13*uwidth):counts['inventory_width_disagrees']+=1;continue
   lanes=tags.get('lanes');source=attr.get('TOTALTRAVELLANES')
   if str(source)!=lanes:counts['lane_sources_disagree']+=1;continue
   # Holdout: sample signed intensity gradient transverse to the 2025 aerial, median aggregation over 5m.
   offsets=np.arange(-2.25,2.26,.075);ts=np.arange(-2.25,2.26,.15);strength=[]
   for curb in e:
    xy=c[None,None,:]+(t*ts[:,None])[:,None,:]/scale+(n*(offsets+curb)[:,None])[None,:,:]/scale
    xx=(xy[:,:,0]+photo_shift[0]-xmin)/(xmax-xmin)*im.shape[1]-.5;yy=(ymax-xy[:,:,1]-photo_shift[1])/(ymax-ymin)*im.shape[0]-.5
    lum=map_coordinates(im,[yy,xx],order=1);grad=abs(gaussian_filter1d(lum,1.3,order=1,axis=1));z=np.median(grad,axis=0)
    center=z[np.abs(offsets)<.3].max();far=np.quantile(z[(abs(offsets)>1)&(abs(offsets)<2.3)],.7);strength.append([round(center,2),round(far,2)])
   clean=all(a>max(2.8,1.5*b) for a,b in strength)
   parking=attr.get('TOTALPARKINGLANES');known=[tags.get('parking:'+s,tags.get('parking:both')) for s in ['left','right']];orient=[tags.get('parking:'+s+':orientation',tags.get('parking:both:orientation')) for s in ['left','right']]
   output.append(dict(id=road['id'],name=tags['name'],st=round(st),xy=c.tolist(),left=round(-e[0],2),right=round(e[1],2),width=round(uwidth,2),epochCurbOffsets=np.round(ed,3).tolist(),widthFromInventory=round(reserve*.3048,2),sourceTravelLanes=source,parkingLanes=parking,parkingSideTags=known,parkingOrientation=orient,optics2025=clean,curbGradient=strength))
 print(counts,'geo',len(output),'photo',sum(r['optics2025'] for r in output))
 for name in sorted(set(r['name'] for r in output)):
  rows=[r for r in output if r['name']==name];print(name,len(rows),sum(r['optics2025'] for r in rows),[(r['width'],r['widthFromInventory'],r['sourceTravelLanes'],r['parkingLanes'],r['parkingSideTags']) for r in rows[:1]])
 (D/dest).write_text(json.dumps({'refusals':dict(counts),'accepted':output},indent=2))
if __name__=='__main__':compare()
