from curbs import *
from contextlib import redirect_stdout
import io
m=json.loads((D/'dc-aerial.json').read_text());cs=m['scale'];cases={'normal':{},'wrong_survey_2.5m':{'second_shift':(0,2.5/cs)},'wrong_aerial_3m':{'photo_shift':(0,3/cs)},'opposed_lane_count':{'lane_overrides':{'696198842':{'lanes':'2'}}}}
results={}
for name,opts in cases.items():
 out='negative-'+name+'.json'
 with redirect_stdout(io.StringIO()):compare(dest=out,**opts)
 r=json.loads((D/out).read_text());here=[a for a in r['accepted'] if a['id']=='696198842'];qs=[a for a in here if a['optics2025']];spans=[(a['st'],b['st']) for a,b in zip(qs[:-1],qs[1:]) if b['st']-a['st']==6];results[name]={'shapeStations':len(here),'photoStations':len(qs),'contiguous':spans}
print(results)
assert results['normal']['contiguous']==[(20,26)]
assert not results['wrong_survey_2.5m']['contiguous'] and not results['opposed_lane_count']['contiguous'] and not results['wrong_aerial_3m']['contiguous'] 
(D/'control-results.json').write_text(json.dumps(results,indent=2))
