from align import *
from statistics import median
import re

def normalize(value):
 return re.sub(r'[^a-z0-9]','',str(value or '').casefold())

def profile(label,loc):
 sets=read(label,loc)
 def gate(reason,**more):return {'status':reason,**more}
 roads=nearrows(sets['road'],(0,0),loc[2])
 if not roads or roads[0][0]>7:return gate('camera-axis-unverified')
 road=roads[0]
 direction=road[2].get('oneway',0)
 if direction:
  road_heading=(road[4][2]+(180 if direction==-1 else 0))%360
  if abs((road_heading-loc[2]+180)%360-180)>15:return gate('wrong-way-on-directional-map')
 if road[2].get('ramp') or road[2].get('brunnel'):return gate('structure-or-ramp')
 trace=[]
 for station in (-24,-12,0,12,24):
  sample=along(road[3],road[4][1]+station)
  if not sample:return gate('mapped-way-or-structure-boundary')
  point,heading=sample
  # A source axis can project near the end of a noncovering event. Only the interior
  # of the event is eligible, apart from sub-metre endpoint rounding.
  names=nearrows(sets['name'],point,heading,10)
  if not names or names[0][0]>2.5:return gate('unlabeled-or-misaligned-way')
  nt=names[0][2];ref=nt.get('ref');name=nt.get('name')
  fh=nearrows(sets['fh'],point,heading,10)
  possible=[r for r in fh if r[0]<6 and min(r[4][1],sum(dist(a,b) for a,b in zip(r[3],r[3][1:]))-r[4][1])>.12]
  if not possible:return gate('federal-axis-unverified',station=station,nearestM=round(fh[0][0],2) if fh else None)
  closest=possible[0]
  if any(r[2]['route_id']!=closest[2]['route_id'] and r[0]<closest[0]+3 for r in possible[1:]):return gate('parallel-federal-axis')
  f=closest[2];st={}
  if 'ma25' in sets:
   near=nearrows(sets['ma25'],point,heading,10)
   valid=[r for r in near if r[0]<6 and r[2]['route_id']==f['route_id'] and (normalize(name)==normalize(r[2].get('St_Name')) or (nt.get('route_1_network')=='US:MA' and nt.get('route_1_ref')==r[2].get('Route_Number') and r[2].get('Route_System')=='SR' and re.fullmatch(r'SR0*'+re.escape(str(nt.get('route_1_ref')))+r' [NESW]B',str(r[2]['route_id']))))]
   if not valid:return gate('state-road-or-direction-mismatch')
   st=valid[0][2]
  shield={'us-highway':3,'us-interstate':2,'us-state':4}.get(nt.get('network'))
  equivalent_shield=shield is not None and f.get('routesigning')==shield and f.get('routequalifier') in (None,1)
  identified=(equivalent_shield and ref and f.get('routenumber') and str(f['routenumber']) in re.findall(r'\d+',str(ref))) or normalize(name)==normalize(f.get('routename')) and bool(normalize(name)) or bool(st)
  if not identified:return gate('no-shared-route-identity')
  ow=bool(road[2].get('oneway',0))
  if st:
   if not ow or st['Facility']!=1 or st['Opp_Lanes']<1 or st['Num_Lanes']+st['Opp_Lanes']!=f['through_lanes']:return gate('unreconciled-directional-count')
   cardinal={'EB':90,'WB':270,'NB':0,'SB':180}.get(st['Route_Direction'])
   if cardinal is None or abs((heading-cardinal+180)%360-180)>65:return gate('map-direction-disagrees-with-state')
   values={'lanesOnThisCarriageway':st['Num_Lanes'],'travelWayFt':st['Surface_Wd'],'rightShoulderFt':st['Shldr_Rt_W'],'rightShoulderTypeState':st['Shldr_Rt_T'],'leftShoulderFt':st['Shldr_Lt_W'],'medianWidthFt':st['Med_Width'],'medianTypeState':st['Med_Type'],'fhThroughLanesBothDirections':f['through_lanes']}
   source='MassDOT 2025 + FHWA 2024'
  else:
   if (ow and f['facility_type']!=1) or (not ow and f['facility_type']!=2):return gate('incompatible-single-or-dual-carriageway')
   values={'lanesOnThisCarriageway':f.get('through_lanes'),'meanLaneWidthFt':f.get('lane_width'),'inventoryRightShoulderFt':f.get('shoulder_width_r'),'inventoryShoulderType':f.get('shoulder_type'),'medianTypeFH':f.get('median_type'),'medianWidthFt':f.get('median_width')}
   source='FHWA 2024'
   if 'state7' in sets:
    directionWidths={}
    for d in ('i','d'):
     ident=f['route_id'][:-1]+d
     w=[r for r in nearrows(sets['state7'],point,heading,10) if r[0]<6 and r[2]['RouteIdentifier']==ident]
     l=[r for r in nearrows(sets['state3'],point,heading,10) if r[0]<6 and r[2]['RouteIdentifier']==ident]
     if not w or not l:return gate('unverified-state-lane-or-width')
     directionWidths[d]=(w[0][2]['RdwyWidth'],l[0][2]['NumberOfLanes'])
    if sum(v[1] for v in directionWidths.values())!=f['through_lanes'] or not f['lane_width'] or any(abs(width/lanes-f['lane_width'])>0.6 for width,lanes in directionWidths.values()):return gate('state-federal-width-conflict', values=directionWidths)
    values['travelWayFt']=sum(v[0] for v in directionWidths.values())
    values['widthByInventoryDirectionFt']=directionWidths
    source='WSDOT March 2026 directional logs + FHWA 2024'
  trace.append({'station':station,'mapId':road[1],'name':name,'ref':ref,'route':f['route_id'],'id':closest[1],'stateId':st.get('OBJECTID'),'lateralM':round(closest[0],2),'bearingError':round(err(closest[4][2],heading),2),'values':values})
 key=lambda r:(r['route'],r['name'],r['ref'],json.dumps(r['values'],sort_keys=True))
 if len(set(map(key,trace)))!=1:return gate('inventory-transition',trace=trace)
 return {'status':'matched','source':source,'name':trace[2]['name'],'maxLateralM':max(row['lateralM'] for row in trace),'maxAngleDegrees':max(row['bearingError'] for row in trace),'values':trace[2]['values'],'trace':trace}
if __name__=='__main__':
 sites=json.loads((work/'locations.json').read_text());out={}
 for label,loc in sites.items():
  out[label]=profile(label,loc)
  print(label,json.dumps({k:v for k,v in out[label].items() if k!='trace'}))
 (work/'results.json').write_text(json.dumps(out,indent=2)+'\n')
