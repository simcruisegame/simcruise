from curbs import *
from shapely.geometry import Polygon
def prepare():
 data=json.loads((D/'paint.json').read_text());tile=json.loads((D/'dc-production.json').read_text());row=data['runs'][0];roads=json.loads((D/'Washington-roads.json').read_text());road=next(r for r in roads if r['id']==row['id']);g=LineString(road['xy'])
 width=json.loads((D/'dc-width-source-block.json').read_text());spec=next(r['attributes'] for r in width['features'] if r['attributes'].get('BLOCK_NAME')=='1100 - 1199 BLOCK OF Q STREET NW');assert spec['TOTALTRAVELLANES']==1 and spec['TOTALPARKINGLANES']==2 and spec['TOTALBIKELANES']==1 and road['tags']['cycleway:right']=='lane' and road['tags']['parking:both:orientation']=='parallel'
 m=json.loads((D/'dc-aerial.json').read_text());cs=m['scale'];xy=tile['id'];span=40075016.68557849/65536;west=xy[0]*span-20037508.342789244;north=20037508.342789244-xy[1]*span
 def nd(s,u):
  c=np.array(g.interpolate(s/cs).coords[0]);a=np.array(g.interpolate((s-.3)/cs).coords[0]);b=np.array(g.interpolate((s+.3)/cs).coords[0]);t=(b-a)/np.linalg.norm(b-a);n=np.array([t[1],-t[0]]);return c+u*n/cs,t
 def uv(pt):return [round((pt[0]-west)/span,8),round((north-pt[1])/span,8)]
 walk=[LineString([[west+a*span,north-b*span] for a,b in w[2]]).buffer(max(.5,w[1]*.5)/cs) for w in tile['pedestrian']]
 curb=json.loads((D/'dc-curb-2023-block.json').read_text());curbs=[LineString(p) for f in curb['features'] if f['attributes']['FEATURECODE']==1040 for p in f['geometry']['paths']]
 for s in np.arange(18,28.1,.5):
  c,t=nd(s,0);a,b=edges(curbs,c,t,cs);assert a is not None and abs(a+4.26)<.25
 # Trim the ends and leave explicit clearances on both sides of the measured bay.
 raw=[nd(s,u)[0] for s,u in [(18,-3.99),(27.5,-3.99),(27.5,-1.97),(18,-1.97)]]
 space=Polygon(raw);assert not any(area.intersects(space) for area in walk)
 bay={'sideInWayDirection':'left','orientation':'parallel','extent':[uv(p) for p in raw],'travelAdjacent':'no designated bicycle lane on this side','observation':'mapped bay geometry; occupancy unknown','source':'DC OCTO curb geometry, DDOT roadway block 4735282 and OpenStreetMap way 696198842'}
 body={'version':1,'id':xy,'roadSignature':tile['road_layout']['roadSignature'],'geometrySource':'Office of the Chief Technology Officer, Washington DC','observed':'2025-03-27','way':row['id'],'name':row['name'],'a':row['a'],'b':row['b'],'observedPaint':[{'a':o['a'],'b':o['b']} for o in row['paint']],'availableBayEnvelopes':[bay]}
 (D/'q-registered.json').write_text(json.dumps(body,indent=2)+'\n')
 print('validated continuous ten-metre curb, OSM-confirmed parking, bike-lane and mapped-footway exclusion on left bay')
if __name__=='__main__':prepare()
