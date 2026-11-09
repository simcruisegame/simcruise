from measure import *
from collections import Counter
def open_rows(site,area):
 return json.loads((D/(site+'-roads.json')).read_text()),json.loads((D/(area+'-proof.json')).read_text())['accepted']
def trace_line(lum,response):
 # A narrow pale marking needs persistent longitudinal support. Restrict
 # width/brightness/contrast to exclude asphalt seams and white car bodies.
 signal=uniform_filter1d(response,5,axis=0)
 return (signal>6.)&(lum>55)
def build():
 roads,measure=open_rows('Washington','dc');s0=0.;sel=[];refusals=Counter();rs=[]
 tile=json.loads((D/'dc-game.json').read_text());x,y=tile['id'];merc=40075016.68557849/65536;west=x*merc-20037508.342789244;north=20037508.342789244-y*merc
 for r in roads:
  tag=r['tags']
  if tag.get('oneway')!='yes' or tag.get('lanes')!='1':continue
  bases=[a for a in measure if a['id']==r['id'] and a['optics2025'] and a['sourceTravelLanes']==1 and a['parkingLanes']==2 and a['parkingSideTags']==['yes','yes'] and a['parkingOrientation']==['parallel','parallel']]
  if len(bases)<2:continue
  m=json.loads((D/'dc-aerial.json').read_text());cs=m['scale'];g=LineString(r['xy']);other=[LineString([[west+a*merc,north-b*merc] for a,b in way[3]]) for way in tile['roads'] if way[2]==tag['name'] and way[1]]
  series=[]
  for row in bases:
   if not series or row['st']-series[-1][-1]['st']!=6:series.append([])
   series[-1].append(row)
  for chain in series:
   if len(chain)<2:continue
   first=chain[0]['st']-2.75;last=chain[-1]['st']+2.75
   along=np.arange(first,last+.001,.2);u=np.arange(-6.,6.01,.1)
   imgs=[]
   for im,md in [('dc-aerial.jpg','dc-aerial.json'),('dc2023.jpg','dc2023.json')]:
    meta=json.loads((D/md).read_text());resp,lum=profile(D/im,meta,g,0,along,u);imgs.append((trace_line(lum,resp),resp,lum))
   pts=np.array([g.interpolate(s/cs).coords[0] for s in along]);ons=[(i,q) for i,q in enumerate(other) if all(q.distance(Point(p))*cs<1.4 for p in [pts[0],pts[len(pts)//2],pts[-1]])]
   if len(ons)!=1:refusals['no_unique_shape']+=1;continue
   center=abs(u)<.35;center_frac=max(np.mean(np.any(bits[:,center],axis=1)) for bits,_,__ in imgs)
   if center_frac>.24:refusals['unresolved_center_paint']+=1;continue
   paint=[]
   for a,b in [(-3.8,-1.1),(1.,3.8)]:
    restrict=(u>a)&(u<b);hits=[bits.mean(axis=0) for bits,_,__ in imgs]
    k=[np.argmax(np.where(restrict,h,0)) for h in hits]
    if abs(u[k[0]]-u[k[1]])>.4:continue
    offset=float(u[k[0]]);h2=np.max([imgs[1][0][:,n] for n in np.flatnonzero(abs(u-offset)<.4)],axis=0)
    conjunction=imgs[0][0][:,k[0]]&h2
    if conjunction.mean()<.35:continue
    start=None
    for j,v in enumerate(conjunction):
     if v and start is None:start=j
     if (not v or j==len(along)-1) and start is not None:
      if j-start>=7:paint.append({'offset':round(offset,2),'s':[round(float(along[start]),2),round(float(along[j]),2)]})
      start=None
   # Keep the vectors in their registered map CRS and actual outlines. The source
   # basis includes a distinct airborne year and decision intervals.
   def coord(s,off):
    c=np.array(g.interpolate(s/cs).coords[0]);t=np.array(g.interpolate((s+.3)/cs).coords[0])-np.array(g.interpolate((s-.3)/cs).coords[0]);t=t/np.linalg.norm(t);p=c+off*np.array([t[1],-t[0]])/cs
    return [round(float((p[0]-west)/merc),8),round(float((north-p[1])/merc),8)]
   qline=[coord(first,0.),coord(last,0.)];packed=[{'a':coord(p['s'][0],p['offset']),'b':coord(p['s'][1],p['offset']),'s':p['s'],'offset':p['offset']} for p in paint]
   sels={'id':r['id'],'name':tag['name'],'first':first,'last':last,'a':qline[0],'b':qline[1],'guardWidth':1.6,'curbs':[round(np.mean([p['left'] for p in chain]),2),round(np.mean([p['right'] for p in chain]),2)],'centerSupport':round(center_frac,3),'paint':packed}
   sel.append(sels);print('ACCEPT',sels)
 (D/'paint.json').write_text(json.dumps({'tile':tile['id'],'runs':sel,'refused':refusals},indent=2))
if __name__=='__main__':build()
