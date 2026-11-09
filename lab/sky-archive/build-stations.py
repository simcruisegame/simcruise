"""Refresh the offline station locator from a saved or live IEM AZOS GeoJSON."""
import argparse
import json
import urllib.request
from pathlib import Path
parser = argparse.ArgumentParser(__doc__)
parser.add_argument('--catalog', type=Path)
parser.add_argument('--output', required=True, type=Path)
args = parser.parse_args()
if args.catalog:
    catalog = json.loads(args.catalog.read_bytes())
else:
    url = 'https://mesonet.agron.iastate.edu/geojson/network/AZOS.geojson?only_online=false'
    with urllib.request.urlopen(url, timeout=20) as response:
        wire = response.read(8000001)
    if len(wire) > 8000000:
        raise ValueError('Unexpected station catalog size')
    catalog = json.loads(wire)
rows = []
for item in catalog['features']:
    station = item['properties']
    if station['country'] != 'US' or not station['network'].endswith('_ASOS'):
        continue
    lon, lat = item['geometry']['coordinates']
    elevation = station['elevation']
    rows.append([station['sid'], round(lat, 4), round(lon, 4),
                 round(elevation) if elevation is not None else None,
                 station['archive_begin'], station['archive_end'], station['state']])
args.output.write_text(json.dumps(sorted(rows), separators=(',', ':')))
