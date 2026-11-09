"""Evaluate a mapped road sample against national HPMS and Utah's dated striping inventory."""
import argparse
from math import *
from pathlib import Path
import json
from source_access import fetch, UT
R = 6378137.0
TIGER = 'https://tigerweb.geo.census.gov/arcgis/rest/services/TIGERweb/State_County/MapServer/0/query'
FHWAROOT = 'https://geo.dot.gov/server/rest/services/Hosted/HPMS_FULL_{}_2024/FeatureServer/0/query'
FH_FIELDS = 'objectid,data_year,route_id,routenumber,routename,begin_point,end_point,facility_type,through_lanes,lane_width,shoulder_type,shoulder_width_r,shoulder_width_l,sample_id'
PAINT_FIELDS = 'OBJECTID,ROUTE,UDOT_BMP,UDOT_EMP,TRAVEL_DIR,PASS_TYPE,NORM_SLD_W,NORM_SLD_Y,WIDE_SLD_W,NORM_BRK_W,NORM_BRK_Y,NORM_DTD_W,NORM_DTD_Y,WIDE_DTD_W,COLL_DATE'

def line_heading(p, q):
    return degrees(atan2(q[0] - p[0], q[1] - p[1])) % 360

def closest(paths, loc):
    (lat, lon) = loc
    best = None
    for path in paths:
        pts = [(radians(x - lon) * R * cos(radians(lat)), radians(y - lat) * R) for (x, y, *_) in path]
        lengths = [dist(a, b) for (a, b) in zip(pts, pts[1:])]
        total = sum(lengths)
        walked = 0.0
        if total < 0.1:
            continue
        for (a, b, length) in zip(pts, pts[1:], lengths):
            if length == 0:
                continue
            (dx, dy) = (b[0] - a[0], b[1] - a[1])
            t = min(1.0, max(0.0, (-a[0] * dx - a[1] * dy) / (length * length)))
            distance = hypot(a[0] + t * dx, a[1] + t * dy)
            current = (distance, (walked + length * t) / total, line_heading(a, b))
            best = min(best, current) if best is not None else current
            walked += length
    return best

def candidate(features, loc, bearing, id_field):
    rows = []
    for f in features:
        near = closest(f['geometry']['paths'], loc)
        if not near or near[0] > 9 or abs(cos(radians(near[2] - bearing))) < cos(radians(12)):
            continue
        rows.append((near[0], near[1], near[2], f['attributes']))
    rows.sort(key=lambda s: s[0])
    if not rows:
        return None
    best = rows[0]
    if any((r[3][id_field] != best[3][id_field] and r[0] < best[0] + 3.0 for r in rows[1:])):
        return None
    return best

def striping(row):
    # Inventory coverage includes the gaps in a broken line.
    scale = (row['UDOT_EMP'] - row['UDOT_BMP']) * 5280
    if scale < 60 or not row.get('COLL_DATE'):
        return None
    ratio = lambda name: float(row.get(name) or 0) / scale
    near = lambda num, expected: abs(num - expected) <= 0.12
    if any((ratio(key) > 0.1 for key in ['WIDE_SLD_W', 'WIDE_DTD_W', 'NORM_BRK_W', 'NORM_DTD_W', 'NORM_DTD_Y'])):
        return None
    white = near(ratio('NORM_SLD_W'), 2)
    if row['PASS_TYPE'] == 'Permitted' and near(ratio('NORM_BRK_Y'), 1) and near(ratio('NORM_SLD_Y'), 0):
        center = 'broken-yellow'
    elif row['PASS_TYPE'] == 'Not Permitted' and near(ratio('NORM_SLD_Y'), 2) and near(ratio('NORM_BRK_Y'), 0):
        center = 'double-solid-yellow'
    else:
        return None
    return {'center': center, 'twoSolidWhiteEdges': white, 'inventoryDate': row['COLL_DATE'], 'sourceTravel': row['TRAVEL_DIR'], 'yellowSolidFeet': row['NORM_SLD_Y'], 'yellowBrokenFeet': row['NORM_BRK_Y'], 'whiteSolidFeet': row['NORM_SLD_W']}

def acquire(loc, extent):
    (lat, lon) = loc
    dlat = extent / 111320
    dlon = dlat / cos(radians(lat))
    envelope = ','.join((str(v) for v in [lon - dlon, lat - dlat, lon + dlon, lat + dlat]))
    state_doc = fetch(TIGER, {'geometry': f'{lon},{lat}', 'geometryType': 'esriGeometryPoint', 'inSR': 4326, 'returnGeometry': 'false', 'outFields': 'STUSAB', 'resultRecordCount': 3})
    states = state_doc.get('features', [])
    if len(states) != 1:
        raise ValueError(f"No unique state at {loc}: {state_doc.get('error')}")
    state = states[0]['attributes']['STUSAB']
    if len(state) != 2 or not state.isalpha():
        raise ValueError(state)
    fp = {'geometry': envelope, 'geometryType': 'esriGeometryEnvelope', 'inSR': 4326, 'outSR': 4326, 'geometryPrecision': 7, 'returnGeometry': 'true', 'resultRecordCount': 128}
    shapes = fetch(FHWAROOT.format(state), fp | {'where': 'through_lanes > 0', 'outFields': FH_FIELDS})
    if shapes.get('error') or shapes.get('exceededTransferLimit'):
        raise ValueError('Truncated or unavailable HPMS')
    paint = fetch(UT + '/0/query', fp | {'outFields': PAINT_FIELDS}) if state == 'UT' else {}
    if paint.get('exceededTransferLimit') or paint.get('error'):
        paint = {}
    return (state, shapes.get('features', []), paint.get('features', []))

def sample(loc, bearing, state, roads, paint):
    hit = candidate(roads, loc, bearing, 'route_id')
    if not hit:
        return None
    (distance, along, angle, h) = hit
    result = {'state': state, 'route': h['route_id'], 'offsetM': round(distance, 2), 'axis': round(angle, 2), 'mile': round(h['begin_point'] + along * (h['end_point'] - h['begin_point']), 5), 'lanes': h['through_lanes'], 'hpmsYear': h['data_year'], 'hpmsId': h['objectid']}
    if h['lane_width'] and 8 <= h['lane_width'] <= 16:
        result['laneWidthM'] = round(h['lane_width'] * 0.3048, 4)
    if h['shoulder_width_r'] is not None and h['shoulder_type'] and (0 <= h['shoulder_width_r'] <= 20):
        result['rightShoulderM'] = round(h['shoulder_width_r'] * 0.3048, 4)
        result['shoulderType'] = h['shoulder_type']
    if h['through_lanes'] != 2 or h['facility_type'] != 2:
        return result
    obs = candidate([f for f in paint if f['attributes']['ROUTE'] == h['route_id']], loc, bearing, 'ROUTE')
    if obs:
        style = striping(obs[3])
        if style:
            result['paint'] = {**style, 'id': obs[3]['OBJECTID']}
    return result
if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('lat', type=float)
    parser.add_argument('lon', type=float)
    parser.add_argument('bearing', type=float)
    args = parser.parse_args()
    loc = (args.lat, args.lon)
    (state, roads, paint) = acquire(loc, 240)
    obj = sample(loc, args.bearing, state, roads, paint)
    print(json.dumps(obj, indent=2))
