"""Rebuild a MassDOT one-way carriageway asset from normalized, saved public source documents."""
import argparse,hashlib,json,os,subprocess,sys
from pathlib import Path
here=Path(__file__).resolve().parent
def main():
 ap=argparse.ArgumentParser(description=__doc__)
 ap.add_argument('--work',type=Path,required=True,help='directory with keyed state/FHWA and vector JSON captures')
 ap.add_argument('--site',type=Path,required=True,help='selection.json and georeferenced MassGIS aerials')
 ap.add_argument('--game-map',type=Path,required=True,help='actual normal-game tile JSON')
 ap.add_argument('--output',type=Path,required=True,help='runtime overlay output directory')
 args=ap.parse_args();site=args.site.resolve()
 args.output.mkdir(parents=True,exist_ok=True)
 sel=json.loads((site/'selection.json').read_text())
 if sel['result']['source']!='MassDOT 2025 + FHWA 2024' or sel['result']['status']!='matched':raise ValueError('need statewide and federal event match')
 # Recheck the signed map direction, identity and attributes against the raw published events.
 os.environ['SD_SECTION_WORK']=str(args.work.resolve())
 from evaluate import profile
 recomputed=profile(sel['key'],sel['center'])
 if recomputed.get('status')!='matched' or recomputed.get('trace')!=sel['result'].get('trace'):raise ValueError('saved selection does not reproduce actual route match')
 if sel['result']['values']['lanesOnThisCarriageway']!=2 or sel['result']['values']['rightShoulderTypeState']!=3:raise ValueError('requires a two-lane directional paved shoulder event')
 import math
 lat,lon=sel['center'][:2];cx=6378137*math.radians(lon);cy=6378137*math.asinh(math.tan(math.radians(lat)));seen=set()
 for yr in (2023,2025):
  info=json.loads((site/f'{yr}.json').read_text());x0,y0,x1,y1=info['bounds']
  if abs((x0+x1)/2-cx)>.04 or abs((y0+y1)/2-cy)>.04 or abs((x1-x0)*math.cos(math.radians(lat))-136)>.1 or abs((y1-y0)*math.cos(math.radians(lat))-136)>.1 or info['metersPerPixel']!=.125:raise ValueError('unmatched aerial observation footprint')
  if not info['source'].startswith('https://tiles.arcgis.com/tiles/hGdibHYSPO59RG1h/arcgis/rest/services/'):raise ValueError('unexpected MassGIS imagery source')
  b=hashlib.sha256((site/f'{yr}.jpg').read_bytes()).hexdigest()
  if b in seen or info['source'] in seen:raise ValueError('duplicate acquisition')
  seen.update([b,info['source']])
 env=dict(os.environ,SD_SECTION_WORK=str(args.work.resolve()),SD_SECTION_SITE=str(site),SD_SECTION_GAME_MAP=str(args.game_map.resolve()),SD_SECTION_OUTPUT=str(args.output.resolve()),OPENBLAS_NUM_THREADS='1')
 for script in ['estimate.py','build_asset.py']:subprocess.run([sys.executable,str(here/script)],env=env,check=True)
if __name__=='__main__':main()
