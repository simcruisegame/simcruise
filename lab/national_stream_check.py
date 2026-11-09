import math,json,os,sys,tempfile,subprocess
from pathlib import Path
# The compiled atlas helper: first argument, else SIMCRUISE_ATLAS, else tools/atlas.
game=Path(sys.argv[1] if len(sys.argv)>1 else os.environ.get('SIMCRUISE_ATLAS','../tools/atlas')).resolve()
lon,lat=-100.168317,37.779225
x=math.floor((lon+180)/360*65536);y=math.floor((1-math.asinh(math.tan(math.radians(lat)))/math.pi)*32768)
with tempfile.TemporaryDirectory() as d:
 def run(*a):return json.loads(subprocess.check_output([game,a[0],d,*map(str,a[1:])],timeout=45))
 t=run('tile',x,y);assert t['roads'] and not t['aerial'] and t['aerial_key'];print('cold Kansas drivable before photos',t['requests'], 'source reads',len(t['roads']),'road segments',flush=True)
 t2=run('tile',x,y);assert not t2['requests'];print('repeat source reads',t2['requests'],flush=True)
 photo=run('aerial',x//4,y//4);t3=run('tile',x,y);assert not t3['requests']
 print('optional imagery arrived',bool(photo.get('aerial')),'ready on next load',bool(t3['aerial']),flush=True)
 assert bool(t3['aerial'])==bool(photo.get('aerial'))
