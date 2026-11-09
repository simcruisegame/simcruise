"""Conflate one accepted MassDOT event with two independently registered orthophotos.

All longitudinal and transverse coordinates are metres relative to the event.
The hand-held street imagery is never read by this program.
"""
from estimate import *
reg=json.loads((site/'registration.json').read_text())
game=json.loads(Path(os.environ.get('SD_SECTION_GAME_MAP',p/'game-map.json')).read_text());tile=game['id']; lat,lon,hd=loc
if tile!=[math.floor((lon+180)/360*65536),math.floor((1-math.asinh(math.tan(math.radians(lat)))/math.pi)*32768)]:raise ValueError('game tile does not contain the photo observation')
side=40075016.68557849/65536*math.cos(math.radians(lat))
def uv(pos,t):
 point,heading=along(xyz,start+pos);angle=math.radians(heading)
 east=point[0]+t*math.cos(angle);north=point[1]-t*math.sin(angle)
 lng=lon+east/(111319.49079*math.cos(math.radians(lat))); phi=lat+north/111319.49079
 return [round((lng+180)/360*65536-tile[0],9),round((1-math.asinh(math.tan(math.radians(phi)))/math.pi)*32768-tile[1],9)]
radiance={year:shifted_rgb(year,reg['offset'][str(year)]) for year in (2023,2025)}
scores={yr:{k:ridge(im,k) for k in ('yellow','white','edge')} for yr,im in radiance.items()}
def bias(pos):return reg['bias']+reg['slope']*pos
def runs(x):
 edges=np.flatnonzero(np.diff(np.r_[False,x,False].astype(int)));return [(int(a),int(b)-1) for a,b in edges.reshape(-1,2)]
rows=[]
for pos in range(-36,58,4):
 flag=abs(s-pos)<2;at=bias(pos);obs={}
 for year,rs in scores.items():
  white=scan_peak(rs['white'],flag,at+width,.85,50);yellow=scan_peak(rs['yellow'],flag,at-width,.85,50)
  # Seek the outer shoulder transition beyond the approved edge stripe, never on its optical rim.
  outer=scan_peak(rs['edge'],flag,white[0]+right+.5,1.2,50)
  median_edge=scan_peak(rs['edge'],flag,yellow[0]-.58,.28,50)
  median_rgb=np.median(radiance[year][flag],axis=0)
  def tone(offset):return np.median(median_rgb[abs(cross-offset)<.32],axis=0)
  inside=tone(yellow[0]+1.25);beyond=tone(median_edge[0]-1.)
  # Confirm the planted strip has a different brown/green color response from the immediate travel surface.
  color_delta=(beyond[0]-beyond[2])-(inside[0]-inside[2])
  obs[year]=dict(white=white,yellow=yellow,outer=outer,medianEdge=median_edge,medianColor=float(color_delta))
 survey_ok=all(v['white'][1]>=18 and v['yellow'][1]>=11 and abs(v['white'][0]-(at+width))<.42 and abs(v['yellow'][0]-(at-width))<.42 for v in obs.values())
 for label in ('white','yellow'):survey_ok=survey_ok and abs(obs[2023][label][0]-obs[2025][label][0])<=.26
 boundary_ok=abs(obs[2023]['outer'][0]-obs[2025]['outer'][0])<=.40 and all(v['outer'][1]>=14 for v in obs.values())
 median_ok=sel['result']['values']['medianTypeState']==1 and sel['result']['values']['medianWidthFt']>=10 and abs(obs[2023]['medianEdge'][0]-obs[2025]['medianEdge'][0])<.23 and all(v['medianEdge'][1]>23 and v['medianColor']>3.5 for v in obs.values())
 rows.append(dict(station=pos,accepted=bool(survey_ok),outer=bool(boundary_ok),median=bool(median_ok),observations=obs))
segments=runs([r['accepted'] for r in rows]);valid=[(i,j) for i,j in segments if rows[i]['station']<=0<=rows[j]['station'] and (j-i)*4>=30]
if len(valid)!=1:raise ValueError(('no contiguous independently supported window',segments))
i,j=valid[0];used=rows[i:j+1];lo,hi=used[0]['station'],used[-1]['station']
# The rendered lane boundaries use only the fitted upstream event centre offset and published travel width.
marks=[]
def stroke(kind,a,b,offset):
 breaks=np.linspace(a,b,max(2,int(math.ceil((b-a)/2.5))+1))
 for a0,b0 in zip(breaks,breaks[1:]):
  marks.append([kind,uv(a0,offset(a0)),uv(b0,offset(b0)),.12])
stroke('whitepaint',lo,hi,lambda station:bias(station)+width)
stroke('yellowpaint',lo,hi,lambda station:bias(station)-width)
# An asphalt-edge ridge must be narrow across the carriageway in both acquisitions.
# This rejects vehicles next to different ends of a shared dash.
curves={}
for yr,rs in scores.items():
 lum=radiance[yr].min(-1);result=[]
 for i,station in enumerate(s):
  lane=np.where(abs(cross-bias(station))<.48)[0]
  j=lane[np.argmax(rs['white'][i,lane])];t=cross[j]
  flank=(abs(cross-t)>.48)&(abs(cross-t)<.82)
  floor=np.median(lum[i,flank]);contrast=lum[i,j]-floor
  bright=lum[i]>floor+max(10.,.55*contrast);a=j;b=j
  while a>0 and bright[a-1]:a-=1
  while b+1<len(cross) and bright[b+1]:b+=1
  narrow=contrast>=20 and rs['white'][i,j]>20 and (b-a+1)*.075<=.58
  result.append((t,narrow))
 curves[yr]=result
support=np.array([a[1] and b[1] and abs(a[0]-b[0])<.25 for a,b in zip(curves[2023],curves[2025])])
agreed=[]
for ia,ib in runs(support):
 a,b=max(lo,s[ia]-.1),min(hi,s[ib]+.1)
 if b-a<1.6 or b-a>5.2:continue
 agreed.append([round(float(a),2),round(float(b),2)]);stroke('whitepaint',a,b,lambda station:bias(station))
if len(agreed)<2:raise ValueError('insufficient repeated dash phases')
polygons=[];probes=[];median_probes=[]
def strip(a,b,inner,outer,paved):
 points=np.linspace(a,b,max(2,math.ceil((b-a)/.75)+1));poly=[uv(t,inner(t)) for t in points]+[uv(t,outer(t)) for t in reversed(points)]
 polygons.append((poly,paved))
def boundary(row):return median(v['outer'][0] for v in row['observations'].values())
for r0,r1 in zip(used,used[1:]):
 a,b=r0['station'],r1['station']
 # Travel lanes and verified interior right shoulder. Only the separately confirmed median band gets a ground value.
 strip(a,b,lambda t:bias(t)-width,lambda t:bias(t)+width+right,True)
 if r0['median'] and r1['median']:
  e0,e1=[median(v['medianEdge'][0] for v in row['observations'].values()) for row in (r0,r1)]
  e=lambda t:e0+(e1-e0)*(t-a)/(b-a)
  strip(a,b,lambda t:e(t)-1.85,lambda t:e(t)-.38,False)
  t=(a+b)/2;median_probes.append(dict(station=t,uv=uv(t,e(t)-.8)))
 if not (r0['outer'] and r1['outer']):continue
 edge=lambda t:boundary(r0)+(boundary(r1)-boundary(r0))*(t-a)/(b-a)
 # Mark pavement on the observed side up to the registration uncertainty. The edge itself and everything beyond abstain.
 strip(a,b,lambda t:bias(t)+width+right-.06,lambda t:edge(t)-.30,True)
 t=(a+b)/2;probes.append({'s':t,'inner':uv(t,bias(t)+width+right-.4),'shoulder':uv(t,edge(t)-.48),'outside':uv(t,edge(t)+.65),'width':edge(t)-(bias(t)+width)})
if not probes:raise ValueError('no cross-epoch physical shoulder measured')
allpts=np.array([v for poly,_ in polygons for v in poly]);
if np.any(allpts<0) or np.any(allpts>=1):raise ValueError('profile crosses a tile boundary; split upstream')
mn=np.floor(allpts.min(axis=0)*4096)-2;mx=np.ceil(allpts.max(axis=0)*4096)+2
im=Image.new('RGB',tuple(map(int,mx-mn)));draw=ImageDraw.Draw(im)
for poly,paved in polygons:draw.polygon([tuple((np.array(v)*4096-mn)) for v in poly],fill=(255 if paved else 0,255,0))
destination=Path(os.environ.get('SD_SECTION_OUTPUT',p/'stage/game/data'));prefix=destination/f'measured-section-{tile[0]}-{tile[1]}';im.save(str(prefix)+'.png')
tvals=np.linspace(lo,hi,max(2,int((hi-lo)/2)));replace=[uv(t,bias(t)-3.25) for t in tvals]+[uv(t,bias(t)+3.25) for t in reversed(tvals)]
doc=dict(version=1,source='MassDOT Road Inventory 2025 / FHWA HPMS 2024 / MassGIS 2023 and 2025',stateEvent=sel['result']['trace'][2]['stateId'],route=sel['result']['trace'][2]['route'],tile=tile,road=sel['result']['name'],anchors=[[uv(t,0),uv(t+1,0)] for t in np.linspace(lo+2,hi-3,5)],crop=[*(mn/4096),*((mx-mn)/4096)],marks=marks,replacement=replace)
(Path(str(prefix)+'.json')).write_text(json.dumps(doc,separators=(',',':'),default=float))
hold=[(r['station'],yr,v['white'][0],v['yellow'][0]) for r in used if r['station']>8 for yr,v in r['observations'].items()]
err=lambda vals:round(float(np.median(vals)),3)
report=dict(registered=reg,range=[lo,hi],marks=len(marks),dashRuns=agreed,probes=probes,medianProbes=median_probes,stations=rows,holdoutObservations=len(hold)*2,heldoutUncorrectedMAE=err([abs(obs-origin) for st,yr,m1,m2 in hold for obs,origin in ((m1,width),(m2,-width))]),heldoutCorrectedMAE=err([abs(obs-origin-bias(st)) for st,yr,m1,m2 in hold for obs,origin in ((m1,width),(m2,-width))]),paintedLaneWidthM=err([(m1-m2)/2 for _,_,m1,m2 in hold]),medianObservedRightShoulderM=err([probe['width'] for probe in probes]))
(site/'geometry-proof.json').write_text(json.dumps(report,indent=2,default=float));print({k:v for k,v in report.items() if k not in ('registered','probes','medianProbes','stations')},flush=True)
