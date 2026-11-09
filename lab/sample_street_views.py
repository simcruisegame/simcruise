"""Select random districts before inspecting their imagery and retain coverage misses."""
import argparse
import json
import math
import random
import subprocess
import time
from pathlib import Path

RURAL=[('VA','Chula',37.370268,-77.906945),('KS','Cimarron',37.806,-100.348),
       ('VT','Stowe',44.4654,-72.6877),('NC','Burnsville',35.9173,-82.3014),
       ('ID','Salmon',45.1755,-113.8958),('MT','Choteau',47.8124,-112.1823),
       ('IA','Elkader',42.8547,-91.4033),('TX','Marfa',30.3096,-104.0225),
       ('OR','John Day',44.4157,-118.9549),('WV','Lewisburg',37.8023,-80.4457),
       ('NY','Lake Placid',44.2869,-73.9855),('ME','Bethel',44.4042,-70.7915)]
def sample(helper,cache,out,seed,num):
    states=json.loads(Path(__file__).with_name('us-road-samples.json').read_text())
    cities=[(r['state'],r['town'],r['lat'],r['lon']) for r in states if r['town'] not in ('Stowe','Chula')]
    rng=random.Random(seed)
    chosen=[(stratum,*r) for name,pool in [('urban',cities),('small town or rural',RURAL)]
            for stratum,r in [(name,r) for r in rng.sample(pool,num)]]
    rows=[]
    for stratum,state,place,lat,lon in chosen:
        tx=int((lon+180)/360*65536)
        ty=int((1-math.asinh(math.tan(math.radians(lat)))/math.pi)*32768)
        raw=subprocess.check_output([helper,'street-sample',cache,str(tx),str(ty)],timeout=50)
        result=json.loads(raw)
        rows.append(dict(stratum=stratum,state=state,place=place,lat=lat,lon=lon,
                         tile=[tx,ty],result=result))
        out.write_text(json.dumps(dict(seed=seed,sites=rows),indent=2)+'\n')
        print(stratum,state,place,result['status'],len(result['frames']),result['requests'],flush=True)
        time.sleep(1)
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('helper',type=Path);p.add_argument('cache',type=Path);p.add_argument('out',type=Path)
    p.add_argument('--seed',type=int,default=19991107);p.add_argument('--per-stratum',type=int,default=4)
    a=p.parse_args();a.cache.mkdir(exist_ok=True,parents=True)
    sample(a.helper.resolve(),a.cache.resolve(),a.out,a.seed,a.per_stratum)
