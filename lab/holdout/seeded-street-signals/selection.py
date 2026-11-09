import hashlib,json
from pathlib import Path
d=Path(__file__).parent
table=d.parents[1]/'us-road-samples.json'
rows=json.loads(table.read_text());seed='Sunday Driver / ground-photo holdout / 2026-11-08 / 01'
# Withhold the places with existing photo-gallery or in-flight studies.
used={'Anchorage','Miami','Charleston','Lincoln','Cheyenne','Cambridge','Seattle','Bismarck','Jefferson City','Hartford','Honolulu'}
regions=[('west',lambda lon: lon< -105),('central',lambda lon:-105<=lon< -85),('east',lambda lon:lon>=-85)]
chosen=[]
for name,accept in regions:
 candidates=[r for r in rows if accept(r['lon']) and r['town'].replace('_',' ') not in used and r['state'] not in {'AK','HI'}]
 f=lambda r:hashlib.sha256((seed+'|'+r['state']).encode()).hexdigest()
 selected=min(candidates,key=f)
 chosen.append(dict(stratum=name,rank=f(selected),**selected))
rec=dict(seed=seed,rank='minimum SHA-256 of UTF-8 seed vertical bar plus state postal code in each eligible longitude stratum',source_sha256=hashlib.sha256(table.read_bytes()).hexdigest(),excluded_prior_locations=sorted(used),denominator=3,selected=chosen)
(d/'selected.json').write_text(json.dumps(rec,indent=2));print([(r['state'],r['town'],r['lat'],r['lon'],r['tile'])for r in chosen])
