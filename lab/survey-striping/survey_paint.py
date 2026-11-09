"""Measure recurring edge positions in a federal ortho only for an independent reported marking layout."""
from pathlib import Path
from math import *
from statistics import median
import json, argparse
import urllib.parse, urllib.request
from PIL import Image
from io import BytesIO
from road_profile import acquire, sample as road_sample, striping
from source_access import url_read
R = 6378137.0
USD = 'https://imagery.geoplatform.gov/iipp/rest/services/NAIP/NAIP_plus/ImageServer'

def merc(ll):
    return (R * radians(ll[0]), R * asinh(tan(radians(ll[1]))))

def lonlat(x, y):
    return (degrees(x / R), degrees(atan(sinh(y / R))))

def request(path, params, maxbytes):
    url = USD + path + '?' + urllib.parse.urlencode(params)
    return (url, url_read(url, maxbytes))

class Ortho:

    def __init__(self, loc, radius):
        (lat, lon) = loc
        self.center = merc((lon, lat))
        self.cos = cos(radians(lat))
        self.span = radius * 2 / self.cos
        (_, raw) = request('/query', {'f': 'json', 'geometry': ','.join(map(str, self.center)), 'geometryType': 'esriGeometryPoint', 'inSR': 3857, 'spatialRel': 'esriSpatialRelIntersects', 'where': 'category=1', 'outFields': 'objectid,name,year,lowps,category', 'returnGeometry': 'false', 'resultRecordCount': 24}, 32000)
        catalog = json.loads(raw)
        refs = [r['attributes'] for r in catalog.get('features', []) if 0 < r['attributes']['lowps'] <= 0.65]
        if not refs:
            raise ValueError('No suitable USDA ortho')
        self.meta = max(refs, key=lambda r: r['year'])
        (x, y) = self.center
        s = self.span / 2
        bbox = [x - s, y - s, x + s, y + s]
        (url, data) = request('/exportImage', {'f': 'image', 'bbox': ','.join(map(str, bbox)), 'bboxSR': 3857, 'imageSR': 3857, 'size': '1000,1000', 'format': 'jpg', 'compressionQuality': 93, 'interpolation': 'RSP_BilinearInterpolation', 'bandIds': '0,1,2', 'renderingRule': '{"rasterFunction":"None"}', 'mosaicRule': json.dumps({'mosaicMethod': 'esriMosaicLockRaster', 'lockRasterIds': [self.meta['objectid']]})}, 950000)
        self.meta['url'] = url
        self.im = Image.open(BytesIO(data)).convert('RGB')
        self.pix = self.im.load()
        self.k = self.im.width / self.span / self.cos

    def en(self, ll):
        (x, y) = merc(ll)
        return ((x - self.center[0]) * self.cos, (y - self.center[1]) * self.cos)

    def ll(self, x, y):
        return lonlat(x / self.cos + self.center[0], y / self.cos + self.center[1])

    def rgb(self, x, y):
        xp = self.im.width * 0.5 + x * self.k - 0.5
        yp = self.im.height * 0.5 - y * self.k - 0.5
        (i, j) = (floor(xp), floor(yp))
        if min(i, j) < 0 or i + 1 >= self.im.width or j + 1 >= self.im.height:
            raise ValueError('out')
        (a, b) = (self.pix[i, j], self.pix[i + 1, j])
        (c, d) = (self.pix[i, j + 1], self.pix[i + 1, j + 1])
        fx = xp - i
        fy = yp - j
        return tuple(((a[k] + (b[k] - a[k]) * fx) * (1 - fy) + (c[k] + (d[k] - c[k]) * fx) * fy for k in range(3)))

class Axis:

    def __init__(self, road, ortho):
        pts = [ortho.en(p) for p in road['geometry']['paths'][0]]
        self.segs = []
        self.length = 0.0
        for (a, b) in zip(pts, pts[1:]):
            length = dist(a, b)
            if length < 0.03:
                continue
            self.segs.append((self.length, self.length + length, a, (b[0] - a[0]) / length, (b[1] - a[1]) / length))
            self.length += length

    def xy(self, s, t):
        (lo, hi, a, dx, dy) = next((v for v in self.segs if v[1] >= s), self.segs[-1])
        return (a[0] + (s - lo) * dx + t * dy, a[1] + (s - lo) * dy - t * dx)

def measure(record, roads, ortho, center_name):
    style = striping(record['attributes'])
    if not style or not style['twoSolidWhiteEdges']:
        return None
    ax = Axis(record, ortho)
    if not 70 < ax.length < 220:
        return None
    (x, y) = ax.xy(ax.length * 0.5, 0)
    (lon, lat) = ortho.ll(x, y)
    (*_, dx, dy) = ax.segs[len(ax.segs) // 2]
    match = road_sample((lat, lon), degrees(atan2(dx, dy)), center_name, roads, [record])
    if not match or match.get('paint', {}).get('id') != record['attributes']['OBJECTID']:
        return None

    def rgb(s, t):
        return ortho.rgb(*ax.xy(s, t))

    def score(s, t):
        # Local ridge contrast suppresses uniformly bright soil beside the road.
        mid = min(rgb(s, t))
        adj = (min(rgb(s, t - 0.9)) + min(rgb(s, t + 0.9))) / 2
        return mid - adj
    bands = []
    for j in range(8):
        start = ax.length * j / 8
        step = ax.length / 8 / 10
        stations = [start + j * step for j in range(1, 10)]
        vals = {k * 0.15: median((score(s, k * 0.15) for s in stations)) for k in range(-40, 41)}
        options = []
        for left in [a for a in vals if -5.6 <= a <= -1.8 and vals[a] > 5]:
            for right in [a for a in vals if 1.8 <= a <= 5.6 and vals[a] > 5]:
                if 5.65 <= right - left <= 8.2 and abs(right + left) <= 3.4:
                    options.append((min(vals[left], vals[right]) + 0.23 * (vals[left] + vals[right]), left, right))
        if not options:
            bands.append(None)
            continue
        (_, l, r) = max(options)
        bands.append((l, r))
    train = [b for b in bands[::2] if b]
    hold = [b for b in bands[1::2] if b]
    if min(len(train), len(hold)) < 3:
        return None
    (left, right) = (median((v[i] for v in train)) for i in range(2))
    err = [abs(b[i] - (left, right)[i]) for b in hold for i in range(2)]
    if median(err) > 0.35 or max(err) > 1.0:
        return None
    center = (left + right) / 2
    phase = None
    period = None
    phase_gap = None
    if style['center'] == 'broken-yellow':
        obs = []
        for i in range(int(ax.length * 2)):
            s = i * 0.5

            def chroma(t):
                (r, g, b) = rgb(s, t)
                return 0.7 * r + 0.3 * g - 0.65 * b
            mid = chroma(center)
            flanks = (chroma(center - 1) + chroma(center + 1)) / 2
            obs.append((s, mid - flanks))
        fit = obs[:int(len(obs) * 0.65)]
        check = obs[len(fit):]
        best = None
        for T in [11.45 + i * 0.05 for i in range(31)]:
            for phase0 in [a * 0.18 for a in range(ceil(T / 0.18))]:
                on = [v for (s, v) in fit if (s - phase0) % T < T * 0.25]
                off = [v for (s, v) in fit if (s - phase0) % T > T * 0.4 and (s - phase0) % T < T * 0.85]
                if not on or not off:
                    continue
                d = median(on) - median(off)
                if best is None or d > best[0]:
                    best = (d, T, phase0)
        (quality, period, phase) = best
        phase_gap = median((v for (s, v) in check if (s - phase) % period < period * 0.25)) - median((v for (s, v) in check if period * 0.4 < (s - phase) % period < period * 0.85))
        if min(quality, phase_gap) < 3.0:
            return None
    result = {'route': match['route'], 'hpmsId': match['hpmsId'], 'sourceId': record['attributes']['OBJECTID'], 'date': style['inventoryDate'], 'ortho': ortho.meta['name'], 'center': style['center'], 'hpmsLaneWidthM': match.get('laneWidthM'), 'edgeOffsets': [round(left, 2), round(right, 2)], 'testMedianM': round(median(err), 3), 'testMaxM': round(max(err), 3), 'onset': phase, 'period': period, 'heldoutYellowContrast': round(phase_gap, 2) if phase_gap is not None else None, 'lengthM': round(ax.length, 2), 'mile': [record['attributes']['UDOT_BMP'], record['attributes']['UDOT_EMP']], 'bands': bands, 'axis': [ortho.ll(*ax.xy(s, 0)) for s in [0.0] + [v[1] for v in ax.segs]]}
    return result
if __name__ == '__main__':
    pa = argparse.ArgumentParser(description=__doc__)
    pa.add_argument('lat', type=float)
    pa.add_argument('lon', type=float)
    pa.add_argument('output', type=Path)
    a = pa.parse_args()
    loc = (a.lat, a.lon)
    (state, roads, paints) = acquire(loc, 360)
    ortho = Ortho(loc, 370)
    result = []
    for road in paints:
        try:
            r = measure(road, roads, ortho, state)
        except ValueError:
            continue
        if r:
            result.append(r)
    a.output.write_text(json.dumps(result, indent=2) + '\n')
    print(a.output, len(paints), 'tested;', len(result), 'accepted')
    for r in result:
        print(r['mile'], r['center'], r['edgeOffsets'], r['testMedianM'], r['period'], r['heldoutYellowContrast'])
