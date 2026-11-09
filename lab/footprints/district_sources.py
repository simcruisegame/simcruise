"""Bounded, cached public source reads for the optional offline footprint importer."""
import hashlib
import io
import json
import math
import os
from pathlib import Path
import time
from urllib.error import HTTPError
from urllib.parse import urlencode, urlparse
from urllib.request import urlopen

import numpy as np
from PIL import Image
from shapely.geometry import Point, shape

FEMA = 'https://services2.arcgis.com/FiaPA4ga0iQKduv3/arcgis/rest/services/USA_Structures_View/FeatureServer/0/query'
PLANETARY = 'https://planetarycomputer.microsoft.com'
TILE = 40075016.68557849 / 65536


class SourceCache:
    def __init__(self, directory, offline=False):
        self.directory = Path(directory)
        self.directory.mkdir(parents=True, exist_ok=True)
        self.offline = offline
        self.requests = self.size = 0

    def get(self, url, limit):
        name = hashlib.sha256(url.encode()).hexdigest()
        path = self.directory / name
        if path.exists():
            if path.stat().st_size > limit:
                raise ValueError('Oversized cached source')
            return path.read_bytes()
        cooldown = self.directory / (urlparse(url).hostname + '.cooldown')
        if self.offline or (cooldown.exists() and time.time()-cooldown.stat().st_mtime < 3600):
            raise ValueError('Source unavailable in cache or on cooldown')
        if self.requests >= 12 or self.size + limit > 6400000:
            raise ValueError('District source budget exhausted')
        self.requests += 1
        try:
            with urlopen(url, timeout=9) as response:
                if int(response.headers.get('Content-Length', 0)) > limit:
                    raise ValueError('Oversized source')
                data = response.read(limit + 1)
        except HTTPError as exc:
            if exc.code in {429, 503}:
                cooldown.touch()
            raise
        self.size += len(data)
        if len(data) > limit:
            raise ValueError('Oversized source')
        temporary = path.with_suffix('.' + str(os.getpid()) + '.tmp')
        temporary.write_bytes(data)
        temporary.replace(path)
        return data


def geometry_source(tile, cache):
    x, y = tile
    west, north = (x-32768)*TILE, (32768-y)*TILE
    bounds = [west, north-TILE, west+TILE, north]
    query = dict(f='json', where='1=1', geometry=','.join(map(str, bounds)),
                 geometryType='esriGeometryEnvelope', inSR=3857, outSR=3857,
                 spatialRel='esriSpatialRelIntersects', outFields='SOURCE,VAL_METHOD,IMAGE_DATE,HEIGHT',
                 returnGeometry='true', returnZ='false', returnM='false', geometryPrecision=2, resultRecordCount=350)
    doc = json.loads(cache.get(FEMA+'?'+urlencode(query), 350000))
    if 'features' not in doc or doc.get('exceededTransferLimit') or len(doc['features']) > 350:
        raise ValueError('Incomplete or dense ORNL district')
    rows = []
    for feature in doc['features']:
        rings = feature.get('geometry', {}).get('rings', [])
        if not rings or sum(map(len, rings)) > 1500:
            continue
        geometry = [[[round((a-west)/TILE, 7), round((north-b)/TILE, 7)] for a, b in ring] for ring in rings]
        attrs = feature.get('attributes', {})
        rows.append({'geo': geometry, 'origin': {k: attrs.get(k) for k in ('SOURCE', 'VAL_METHOD', 'IMAGE_DATE')}})
    return rows


def lon_lat(tile, u, v):
    return ((tile[0]+u)/65536*360-180,
            math.degrees(math.atan(math.sinh(math.pi*(1-2*(tile[1]+v)/65536)))))


def photo_catalog(tile, cache):
    west, north = lon_lat(tile, 0, 0)
    east, south = lon_lat(tile, 1, 1)
    query = {'collections':'naip', 'bbox':','.join(map(str, [west, south, east, north])), 'limit':35}
    response = json.loads(cache.get(PLANETARY+'/api/stac/v1/search?'+urlencode(query), 480000))
    items = []
    for item in response.get('features', []):
        if item.get('collection') != 'naip':
            continue
        date = item['properties']['datetime'][:10]
        identifier = item['id']
        if date[:4].isdigit() and identifier.replace('_','').isalnum():
            items.append((date, identifier, shape(item['geometry'])))
    return sorted(items, key=lambda row:(row[0],row[1]), reverse=True)


class Photos:
    def __init__(self, tile, cache):
        self.tile, self.cache = tile, cache
        self.items = photo_catalog(tile, cache)
        self.decoded = {}

    def at(self, center):
        lon, lat = lon_lat(self.tile, center.x, center.y)
        location = Point(lon, lat)
        years = set()
        for date, identifier, bounds in self.items:
            if date[:4] in years or not bounds.covers(location):
                continue
            if identifier not in self.decoded:
                x, y = self.tile
                url = (f'{PLANETARY}/api/data/v1/collections/naip/items/{identifier}/tiles/'
                       f'WebMercatorQuad/16/{x}/{y}@4x.jpeg?'+urlencode({'assets':'image','asset_bidx':'image|1,2,3'}))
                if len(self.decoded) >= 8:
                    raise ValueError('District image budget exhausted')
                picture = Image.open(io.BytesIO(self.cache.get(url, 540000)))
                if picture.size != (1024,1024):
                    raise ValueError('Unexpected NAIP raster dimensions')
                self.decoded[identifier] = np.array(picture.convert('RGB'))
            raster = self.decoded[identifier]
            x, y = int(center.x*1024), int(center.y*1024)
            if np.mean(raster[max(0,y-5):y+6,max(0,x-5):x+6].max(axis=2)>5) < .95:
                continue
            years.add(date[:4])
            yield date, identifier, raster
            if len(years) == 2:
                break
