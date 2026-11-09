"""Exercise the shipped geographic loader at one road district in each state."""
import argparse
import json
import math
from pathlib import Path
import subprocess
import time

# Fixed localities make results and repeat visits comparable across releases.
SAMPLES = """AL Montgomery 32.377 -86.300
AK Anchorage 61.2174 -149.8938
AZ Phoenix 33.4485 -112.0741
AR Little_Rock 34.7465 -92.2896
CA Sacramento 38.5794 -121.4900
CO Denver 39.7394 -104.9846
CT Hartford 41.764 -72.683
DE Dover 39.157 -75.521
FL Miami 25.7484 -80.2089
GA Atlanta 33.749 -84.389
HI Honolulu 21.3089 -157.861
ID Boise 43.617 -116.200
IL Springfield 39.799 -89.654
IN Indianapolis 39.769 -86.162
IA Des_Moines 41.590 -93.605
KS Topeka 39.048 -95.678
KY Frankfort 38.200 -84.877
LA Baton_Rouge 30.455 -91.188
ME Augusta 44.310 -69.781
MD Annapolis 38.978 -76.491
MA Cambridge 42.365183 -71.134311
MI Lansing 42.734 -84.555
MN Saint_Paul 44.953 -93.090
MS Jackson 32.299 -90.184
MO Jefferson_City 38.577 -92.173
MT Helena 46.588 -112.024
NE Lincoln 40.809 -96.675
NV Carson_City 39.164 -119.767
NH Concord 43.206 -71.538
NJ Trenton 40.220 -74.768
NM Santa_Fe 35.682 -105.940
NY Albany 42.652 -73.757
NC Raleigh 35.780 -78.639
ND Bismarck 46.808 -100.783
OH Columbus 39.961 -82.999
OK Oklahoma_City 35.468 -97.517
OR Salem 44.939 -123.031
PA Harrisburg 40.264 -76.884
RI Providence 41.825 -71.412
SC Columbia 34.001 -81.034
SD Pierre 44.368 -100.351
TN Nashville 36.163 -86.782
TX Austin 30.271 -97.743
UT Salt_Lake_City 40.768 -111.889
VT Stowe 44.4654 -72.6877
VA Chula 37.370268 -77.906945
WA Olympia 47.038 -122.901
WV Charleston 38.350 -81.632
WI Madison 43.074 -89.384
WY Cheyenne 41.140 -104.820"""

def run(helper, cache, out):
    rows = []
    for line in SAMPLES.splitlines():
        state, town, lat, lon = line.split()
        x = math.floor((float(lon) + 180) / 360 * 65536)
        y = math.floor((1 - math.asinh(math.tan(math.radians(float(lat)))) / math.pi) * 32768)
        args = [helper, 'tile', cache, str(x), str(y)]
        row = dict(state=state, town=town.replace('_',' '), lat=float(lat), lon=float(lon), tile=[x,y])
        try:
            first = json.loads(subprocess.check_output(args, timeout=55))
            second = json.loads(subprocess.check_output(args, timeout=55))
            if first.get('error') or second.get('error'):
                raise ValueError(first.get('error') or second.get('error'))
            elevation = first['h']
            if isinstance(elevation[0],list):
                elevation = [v for group in elevation for v in group]
            row.update(roads=len(first['roads']), terrain_samples=len(elevation),
                       terrain_min_m=min(elevation), terrain_max_m=max(elevation),
                       first_requests=first['requests'], repeat_requests=second['requests'])
            row['passed'] = bool(first['id']==[x,y] and row['roads'] and len(elevation)>3
                                 and all(math.isfinite(v) for v in elevation)
                                 and first['roads']==second['roads'] and elevation and second['requests']==0)
        except (OSError, ValueError, KeyError, IndexError, subprocess.SubprocessError) as e:
            row.update(passed=False, error=str(e))
        rows.append(row)
        out.write_text(json.dumps(rows, indent=2)+'\n')
        print(state, row.get('roads'), row.get('repeat_requests'), row.get('error',''), flush=True)
        time.sleep(.2)
    return sum(row['passed'] for row in rows)

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('helper',type=Path)
    parser.add_argument('cache',type=Path)
    parser.add_argument('report',type=Path)
    args=parser.parse_args()
    args.cache.mkdir(parents=True,exist_ok=True)
    raise SystemExit(0 if run(args.helper.resolve(),args.cache.resolve(),args.report)==50 else 1)
