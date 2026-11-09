"""Fetch a geographically bounded Massachusetts and federal road inventory snapshot.

Existing snapshots are reused. A truncated service response never becomes input.
"""
import argparse
import json
import math
import re
import urllib.parse
import urllib.request
from pathlib import Path

MASSDOT = "https://gis.massdot.state.ma.us/arcgis/rest/services/Roads/RoadInventoryYearEndFiles/FeatureServer/11/query"
FHWA = "https://geo.dot.gov/server/rest/services/Hosted/HPMS_FULL_MA_2024/FeatureServer/0/query"
STATE_FIELDS = ("OBJECTID,route_id,Route_System,Route_Number,Route_Direction,St_Name,Num_Lanes,"
                "Opp_Lanes,Surface_Wd,Shldr_Rt_W,Shldr_Rt_T,Shldr_UL_W,Shldr_UL_T,Shldr_Lt_W,"
                "Shldr_Lt_T,Med_Width,Med_Type,Facility,Operation,from_measure,to_measure")
FEDERAL_FIELDS = ("objectid,data_year,route_id,routenumber,routename,routesigning,routequalifier,"
                  "begin_point,end_point,facility_type,through_lanes,lane_width,median_type,"
                  "median_width,shoulder_type,shoulder_width_r,shoulder_width_l,sample_id,dir_through_lanes")

def retrieve(url, bounds, fields):
    query = dict(f='json', where='1=1', geometry=','.join(map(str, bounds)),
                 geometryType='esriGeometryEnvelope', inSR=4326, outSR=4326,
                 returnGeometry='true', geometryPrecision=7, resultRecordCount=60,
                 spatialRel='esriSpatialRelIntersects', outFields=fields)
    request = urllib.request.Request(url + '?' + urllib.parse.urlencode(query),
                                     headers={'User-Agent': 'SundayDriver/road-inventory-authoring'})
    with urllib.request.urlopen(request, timeout=18) as response:
        data = response.read(512001)
    if len(data) > 512000:
        raise ValueError('inventory response exceeds 512 kB')
    document = json.loads(data)
    if document.get('error') or document.get('exceededTransferLimit') or 'features' not in document:
        raise ValueError('incomplete published event response')
    return {'url': url, 'features': document['features']}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('work', type=Path)
    parser.add_argument('key')
    parser.add_argument('latitude', type=float)
    parser.add_argument('longitude', type=float)
    args = parser.parse_args()
    if not re.fullmatch(r'[a-z0-9-]+', args.key):
        raise ValueError('use a lowercase filename-safe site key')
    args.work.mkdir(parents=True, exist_ok=True)
    target = args.work / (args.key + '-inventory.json')
    if target.exists():
        return
    dy = 135 / 111320
    dx = dy / math.cos(math.radians(args.latitude))
    bounds = [args.longitude-dx, args.latitude-dy, args.longitude+dx, args.latitude+dy]
    data = {'state':'MA', 'fh':retrieve(FHWA,bounds,FEDERAL_FIELDS),
            'ma25':retrieve(MASSDOT,bounds,STATE_FIELDS)}
    if not data['ma25']['features']:
        raise ValueError('no Massachusetts road event in this 270 m window')
    target.write_text(json.dumps(data,separators=(',',':')))

if __name__ == '__main__':
    main()
