import numpy as np,json,sys
from pathlib import Path
from shapely.geometry import Polygon
from shapely import contains_xy
def inspect(site):
 r=json.load(open(site+'-probe.json'));s=r['side'];t=json.load(open(site+'-base.json'));rows=[];all_old=[];all_new=[]
 p=np.fromfile(site+'-audit.f32',dtype=np.float32).reshape(-1,4);p=p[p[:,3]!=2].astype(float)
 for roof in json.load(open(site+'-fit.json'))['profiles']:
  idx=roof['index'];b=t['buildings'][idx];ring=np.array(b[0][0])*s;poly=Polygon(ring,[np.array(x)*s for x in b[0][1:]])
  if not poly.is_valid:continue
  ground=roof['validation']['ground_m'];inp=p[contains_xy(poly.buffer(-.8),p[:,0],p[:,1]) & (p[:,2]>ground+1.8)]
  if len(inp)<12:continue
  xy=inp[:,:2];target=inp[:,2]-ground;vv=np.array(roof['v'])*[s/1e5,s/1e5,.01];new=np.full(len(inp),np.nan)
  for i,j,k in np.array(roof['f']).reshape(-1,3):
   a,b1,c=vv[i],vv[j],vv[k];B=b1-a;C=c-a
   det=B[0]*C[1]-C[0]*B[1]
   if abs(det)<1e-8:continue
   d=xy-a[:2];u=(d[:,0]*C[1]-d[:,1]*C[0])/det;v=(B[0]*d[:,1]-B[1]*d[:,0])/det
   hit=(u>=-1e-5)&(v>=-1e-5)&(u+v<=1.00001);new[hit]=a[2]+u[hit]*B[2]+v[hit]*C[2]
  valid=np.isfinite(new);xy=xy[valid];target=target[valid];new=new[valid]
  if len(new)<12:continue
  width_size=(np.max(ring,axis=0)-np.min(ring,axis=0)).prod();typ=b[6:8]
  detached=(b[2]<=10 and (any(x in ['house','residential','detached'] for x in typ) or (poly.area<260 and all(x in ['','building','yes'] for x in typ))))
  if detached:
   e=np.roll(ring,-1,axis=0)-ring;d=e[np.argmax(np.sum(e*e,axis=1))]
   axis=np.array([-d[1],d[0]])/np.linalg.norm(d);proj=ring@axis;offset=(max(proj)+min(proj))/2;width=max(.1,(max(proj)-min(proj))/2)
   top=max(2.65,b[2]);rise=.16 if width_size>540 else min(width*.48,max(.1,top-2.55))
   prior=top-rise+rise*np.clip(1-abs(xy@axis-offset)/width,0,1)
  else:prior=np.full(len(new),max(2.8,b[2]))
  old=abs(prior-target);improved=abs(new-target)
  all_old.extend(old);all_new.extend(improved)
  rows.append({'index':idx,'form':roof['validation']['form'],'points':len(old),'previous_median':round(np.median(old),3),'median':round(np.median(improved),3),'previous_p90':round(np.percentile(old,90),3),'p90':round(np.percentile(improved,90),3)})
 return {'site':site,'roofs':rows,'points':len(all_old),'median':[round(float(np.median(v)),3) for v in [all_old,all_new]] if all_new else [],'p90':[round(float(np.percentile(v,90)),3) for v in [all_old,all_new]] if all_new else []}
items=[inspect(site) for site in sys.argv[1:]];Path('independent-results.json').write_text(json.dumps(items,indent=2));print(json.dumps(items))
