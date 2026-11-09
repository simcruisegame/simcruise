"""Validate absent map outlines against independent classified EPT lidar returns.

python lab/lidar/verify_footprints.py CANDIDATES.json SURVEY_ID CACHE_DIR OUTPUT.json
The output includes a certificate fragment, raw node digests and rejection reasons.
"""
import argparse
import hashlib
import io
import json
import math
from pathlib import Path
from urllib.parse import urlparse
from urllib.request import urlopen

import laspy
import numpy as np
from pyproj import CRS, Transformer
from shapely import contains_xy, unary_union
from shapely.geometry import Polygon, box


def assessment(polygon, cloud):
    inner = polygon.buffer(-.6)
    ring = polygon.buffer(7).difference(polygon.buffer(.5))
    inside = cloud[contains_xy(inner, cloud[:, 0], cloud[:, 1])]
    ground = cloud[contains_xy(ring, cloud[:, 0], cloud[:, 1]) & (cloud[:, 3] == 2)]
    roof = inside[inside[:, 3] == 6]
    if len(roof) < 120 or len(ground) < 45:
        return None, 'insufficient classified roof or surrounding ground'
    if np.ptp(np.percentile(ground[:, 2], [10, 90])) > 1.5:
        return None, 'surrounding terrain varies substantially'
    x, y, right, top = inner.bounds
    dx, dy = (right-x)/4, (top-y)/4
    occupied = eligible = 0
    for col in range(4):
        for row in range(4):
            cell = box(x+col*dx, y+row*dy, x+(col+1)*dx, y+(row+1)*dy)
            if cell.intersection(inner).area < cell.area*.45:
                continue
            eligible += 1
            occupied += int(contains_xy(cell, roof[:, 0], roof[:, 1]).sum() >= 3)
    height = float(np.median(roof[:, 2]) - np.median(ground[:, 2]))
    if occupied < 5 or occupied < eligible*.8 or not 2 <= height <= 85:
        return None, 'insufficient roof coverage or support for the height'
    return dict(heightM=round(height, 2), roofReturns=len(roof), groundReturns=len(ground),
                coveredCells=occupied, eligibleCells=eligible), None


class Ept:
    def __init__(self, source, directory):
        self.base = source['ept'].rsplit('/', 1)[0]
        parsed = urlparse(self.base)
        if parsed.scheme != 'https' or not (parsed.hostname in {
                'noaa-nos-coastal-lidar-pds.s3.amazonaws.com', 'usgs-lidar-public.s3.amazonaws.com'} or
                (parsed.hostname == 's3-us-west-2.amazonaws.com' and parsed.path.startswith('/usgs-lidar-public/'))):
            raise ValueError('Expected public NOAA or USGS survey storage')
        self.files = {}
        self.cache = directory / hashlib.sha256(self.base.encode()).hexdigest()[:16]
        self.cache.mkdir(parents=True, exist_ok=True)
        self.calls = self.bytes = 0

    def read(self, name):
        filename = self.cache / name.replace('/', '_')
        if filename.is_file():
            data = filename.read_bytes()
        else:
            if self.calls >= 14:
                raise ValueError('Survey request budget exhausted')
            self.calls += 1
            with urlopen(self.base+'/'+name, timeout=8) as response:
                remaining = min(2100000, 6000000-self.bytes)
                if remaining <= 0 or int(response.headers.get('Content-Length', 0)) > remaining:
                    raise ValueError('Survey byte budget exhausted')
                data = response.read(remaining+1)
                self.bytes += len(data)
                if len(data) > remaining:
                    raise ValueError('Survey byte budget exhausted')
            filename.write_bytes(data)
        self.files[name] = hashlib.sha256(data).hexdigest()
        return data


def verify(candidate_file, source_id, cache, registry):
    candidate = json.loads(candidate_file.read_text())
    source = json.loads(registry.read_text())['sources'][source_id]
    reader = Ept(source, cache)
    doc = json.loads(reader.read('ept.json'))
    srs = doc['srs']
    crs = CRS.from_user_input(int(srs['horizontal'])) if str(srs.get('horizontal','')).isdigit() else CRS.from_wkt(srs['wkt'])
    if crs.is_compound:
        crs = crs.sub_crs_list[0]
    if not crs.is_projected:
        raise ValueError('Expected projected lidar coordinates')
    xy_scale = crs.axis_info[0].unit_conversion_factor
    transformer = Transformer.from_crs(3857, crs, always_xy=True)
    x, y = candidate['tile']
    tile_width = 40075016.68557849 / 65536
    polygons = {}
    for item in candidate['candidates']:
        pts = [transformer.transform((x+u-32768)*tile_width, (32768-y-v)*tile_width) for u,v in item['ring']]
        poly = Polygon([(px*xy_scale, py*xy_scale) for px,py in pts])
        if poly.is_valid and not poly.is_empty:
            polygons[item['id']] = poly
    result = {}
    rejected = {}
    if polygons:
        region = unary_union(list(polygons.values())).buffer(7)
        bounds = doc['bounds']
        width = bounds[3]-bounds[0]
        # Use a similar horizontal sample spacing across projects. A web
        # Mercator metre stretches on the ground with latitude.
        lat = math.atan(math.sinh(math.pi*(1-2*(y+.5)/65536)))
        scale = xy_scale*(math.cos(lat) if crs.to_epsg()==3857 else 1)
        depth = max(1,min(13,round(math.log2(width*scale/(.8*doc['span'])))))
        lower,upper = doc['boundsConforming'][2],doc['boundsConforming'][5]
        keys = set()
        def visit(parent):
            for key,count in json.loads(reader.read('ept-hierarchy/'+parent+'.json')).items():
                d,i,j,k = map(int,key.split('-'))
                size = width/2**d
                px,py,pz = bounds[0]+size*i,bounds[1]+size*j,bounds[2]+size*k
                area = box(px*xy_scale,py*xy_scale,(px+size)*xy_scale,(py+size)*xy_scale)
                if pz >= upper or pz+size <= lower or not region.intersects(area):
                    continue
                if key != parent and count == -1 and d <= depth:
                    visit(key)
                elif d == depth and count > 0:
                    keys.add(key)
        visit('0-0-0-0')
        chunks = []
        if len(keys)>24:
            raise ValueError('Survey district exceeds the EPT node limit')
        for key in sorted(keys):
            tile = laspy.read(io.BytesIO(reader.read('ept-data/'+key+'.laz')))
            chunks.append(np.c_[np.asarray(tile.x)*xy_scale, np.asarray(tile.y)*xy_scale,
                                np.asarray(tile.z)*source['zMeters'], np.asarray(tile.classification)])
        points = np.vstack(chunks) if chunks else np.empty((0,4))
        for item in candidate['candidates']:
            if item['id'] not in polygons:
                continue
            certificate, reason = assessment(polygons[item['id']], points)
            if certificate:
                result[item['id']] = dict(outlineHash=item['outlineHash'], geometrySource=item['geometrySource'],
                                          release=candidate['release'], survey=source_id, **certificate)
            else:
                rejected[item['id']] = reason
    return dict(tile=candidate['tile'], certificates=result, rejected=rejected, acquisition=source['acquisition'],
                survey=source_id, ept=source['ept'], requests=reader.calls, bytes=reader.bytes, files=reader.files)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('candidates', type=Path)
    parser.add_argument('source')
    parser.add_argument('cache', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--registry', type=Path, default=Path(__file__).resolve().parents[2]/'helper'/'footprint-surveys.json')
    args = parser.parse_args()
    value = verify(args.candidates, args.source, args.cache, args.registry)
    args.output.write_text(json.dumps(value, indent=2)+'\n')
    print(len(value['certificates']), 'verified footprints')
