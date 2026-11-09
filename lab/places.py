import math,json
from collections import defaultdict
from pathlib import Path
roads=defaultdict(list)
for path in json.loads(Path('lab/detail-manifest.json').read_text()):
 if not path.endswith('.json'):continue
 d=json.loads((Path('game')/path).read_text());x,y=d['id']
 for _,_,name,points,width in d['roads']:
  if not name:continue
  city='Amelia' if d['region']=='Chula' else 'Chesterfield'
  for u,v in points:roads[(name,city)].append((u+x,v+y))
out=[]
for (name,city),coords in sorted(roads.items()):
 mx=sum(x for x,y in coords)/len(coords);my=sum(y for x,y in coords)/len(coords);x,y=min(coords,key=lambda pt:(pt[0]-mx)**2+(pt[1]-my)**2)
 lon=x/65536*360-180;lat=math.degrees(math.atan(math.sinh(math.pi*(1-2*y/65536))))
 if name=='Chula Road':lon,lat=-77.907114,37.369974
 if name=='Raven Wing Drive':lon,lat=-77.6244769,37.3850801
 out.append(dict(street=name,county=city,lon=lon,lat=lat,title=f'{name}, {city} County, VA'))
Path('game/data/virginia-places.json').write_text(json.dumps(out))
