"""Fit mapped Anchorage roofs to flight 110, verify the emitted cell levels on flight 111, and export Godot profiles."""

from pathlib import Path
import json, math, sys
from pyproj import Transformer
import numpy as np
from shapely.geometry import Polygon
from shapely import contains_xy
from collections import defaultdict
from scipy.spatial import cKDTree
from scipy.signal import find_peaks
from scipy.ndimage import gaussian_filter1d
import shapely
from shapely.geometry import box
from shapely.ops import unary_union
from shapely.geometry.polygon import orient
D = Path(__file__).resolve().parent
tile = [5480, 18579]
records = json.loads((D / 'footprints.json').read_text())
base = {'buildings': {f['index']: f['row'] for f in records}}
raw = np.load(D / 'returns.npz')['points']
proj = Transformer.from_crs(4326, 3857, always_xy=True)
inv = Transformer.from_crs(3857, 4326, always_xy=True)
rx, ry = proj.transform(-149.8937, 61.21768)
cos = math.cos(math.radians(61.21768))
xy = (raw[:, :2] - [rx, ry]) * cos

def geo(x, y):
    return inv.transform(x / cos + rx, y / cos + ry)
build = []
for record in records:
    ring = np.asarray(record['row'][0][0])
    longitude = (tile[0] + ring[:, 0]) / 65536 * 360 - 180
    latitude = np.degrees(np.arctan(np.sinh(np.pi * (1 - (tile[1] + ring[:, 1]) / 32768))))
    X, Y = proj.transform(longitude, latitude)
    build.append({'i': record['index'], 'tile': tile, 'poly': Polygon(np.column_stack(((X - rx) * cos, (Y - ry) * cos)))})
profiles = []
reports = {}

def target_xy(x, y):
    lon, lat = geo(x, y)
    return np.round(np.stack(((lon + 180.0) / 360.0 * 65536 - tile[0], (1 - np.arcsinh(np.tan(np.radians(lat))) / np.pi) * 32768 - tile[1]), axis=-1) * 100000).astype(int)
for b in build:
    if b['tile'] != tile or b['i'] not in [14, 18]:
        continue
    pi = b['poly']
    inside = contains_xy(pi.buffer(-1.2), xy[:, 0], xy[:, 1])
    nground = contains_xy(pi.buffer(9).difference(pi), xy[:, 0], xy[:, 1]) & (raw[:, 3] == 2)
    first = (raw[:, 3] == 1) & (raw[:, 5] == 1)
    train = (raw[:, 4] == 110) & first & inside
    test = (raw[:, 4] == 111) & first & inside
    z0 = np.median(raw[nground & (raw[:, 4] == 110), 2])
    z1 = np.median(raw[nground & (raw[:, 4] == 111), 2])
    z = raw[train, 2] - z0
    h, bins = np.histogram(z, np.arange(2, 100, 0.15))
    dens = gaussian_filter1d(h.astype('f'), 1.2)
    peaks, _ = find_peaks(dens, distance=13, prominence=max(dens) * 0.028)
    candidates = [np.median(z[abs(z - (bins[i] + 0.075)) < 0.45]) for i in peaks]
    modes = np.sort([p for p in candidates if sum(abs(z - p) < 0.45) >= len(z) * 0.035])
    main = modes[np.argmin(abs(modes - np.median(z)))]
    modes = modes[(modes >= main - 8) & (modes <= main + 12)]
    assert len(modes) >= 1
    step = 1.8
    cache = {}
    tree = cKDTree(xy[train])
    buckets = defaultdict(list)
    xmin, ymin, xmax, ymax = pi.bounds
    for ix in range(math.floor(xmin / step), math.floor(xmax / step) + 1):
        for iy in range(math.floor(ymin / step), math.floor(ymax / step) + 1):
            cell = box(ix * step, iy * step, (ix + 1) * step, (iy + 1) * step).intersection(pi)
            if cell.area < 0.005:
                continue
            pt = np.array(cell.representative_point().coords[0])
            dist, near = tree.query(pt, k=12)
            sel = near[dist < max(2.6, dist[0] + 0.9)]
            lo = np.array([sum(abs(z[sel] - p) < 0.65) for p in modes])
            ii = int(np.argmax(lo)) if lo.max() >= 2 else int(np.argmin(abs(modes - main)))
            cache[ix, iy] = ii
            buckets[ii].append(cell)
    surfaces = {i: unary_union(v).buffer(0) for i, v in buckets.items()}
    lookup = {}
    vertices = []
    faces = []
    edge = []

    def vertex(x, y, zz):
        X, Y = target_xy(x, y)
        key = (int(X), int(Y), int(round(zz * 100)))
        if key not in lookup:
            lookup[key] = len(vertices)
            vertices.append(key)
        return lookup[key]
    for i, g in surfaces.items():
        for tri in shapely.constrained_delaunay_triangles(g).geoms:
            if tri.area < 0.002:
                continue
            xyz = list(tri.exterior.coords)[:3]
            ids = [vertex(x, y, modes[i]) for x, y in xyz]
            tri_uv = np.asarray([vertices[j][:2] for j in ids])
            dx = tri_uv[1] - tri_uv[0]
            dy = tri_uv[2] - tri_uv[0]
            if dx[0] * dy[1] - dx[1] * dy[0] < 0:
                ids.reverse()
            faces.extend(ids)
        for upper in range(i + 1, len(modes)):
            if upper not in surfaces:
                continue
            linework = g.boundary.intersection(surfaces[upper].boundary)
            pieces = getattr(linework, 'geoms', [linework])
            for line in pieces:
                if line.geom_type != 'LineString':
                    continue
                for a, zv in zip(list(line.coords), list(line.coords)[1:]):
                    A = vertex(*a, modes[i])
                    B = vertex(*zv, modes[i])
                    C = vertex(*zv, modes[upper])
                    E = vertex(*a, modes[upper])
                    faces.extend([A, C, B, A, E, C, A, B, C, A, C, E])
        perimeter = g.boundary.intersection(pi.boundary)
        for line in getattr(perimeter, 'geoms', [perimeter]):
            if line.geom_type != 'LineString':
                continue
            for a, zv in zip(list(line.coords), list(line.coords)[1:]):
                edge.extend([vertex(*a, modes[i]), vertex(*zv, modes[i])])
    vals = raw[test, 2] - z1
    pred = np.array([modes[cache[ix, iy]] for ix, iy in np.floor(xy[test] / step).astype(int)])
    err = abs(pred - vals)
    testcells = defaultdict(list)
    for pt, data in zip(np.floor(xy[test] / step).astype(int), vals):
        testcells[tuple(pt)].append(data)
    errs = np.array([abs(modes[cache[k]] - np.median(vv)) for k, vv in testcells.items() if len(vv) >= 2])
    ev = dict(train_points=int(sum(train)), test_points=int(sum(test)), ground_counts=[int(sum(nground & (raw[:, 4] == i))) for i in (110, 111)], train_flight=110, test_flight=111, levels_m=np.round(modes, 2).tolist(), heldout_first_return_within_1m=round(float(np.mean(err < 1)), 3), heldout_cells=len(errs), heldout_median_m=round(float(np.median(errs)), 3), heldout_p90_m=round(float(np.quantile(errs, 0.9)), 3), heldout_grid_within_1m=round(float(np.mean(errs < 1)), 3))
    print(b['i'], ev, 'vertices', len(vertices), flush=True)
    reports[str(b['i'])] = ev
    if len(errs) < 70 or np.median(errs) > 0.25 or np.mean(errs < 1) < 0.84:
        continue
    row = base['buildings'][b['i']]
    profiles.append(dict(reference='2015 Anchorage airborne flight 110; independent flight 111; row ' + str(b['i']), footprint=np.round(np.array(row[0]) * 100000).astype(int).tolist(), v=vertices, f=faces, edge=edge, eave_m=round(float(min(modes)), 2), top_m=round(float(max(modes)), 2), validation=ev))
doc = dict(schema=1, tile=tile, source='Municipality of Anchorage / USGS 3DEP, 2015', source_url='https://www.fisheries.noaa.gov/inport/item/50281', acquired=['2015-05-10', '2016-09-29'], roofs=profiles)
(Path(sys.argv[1]) if len(sys.argv) > 1 else D / 'lidar-roofs-5480-18579.json').write_text(json.dumps(doc, separators=(',', ':')) + '\n')
(D / 'validation.json').write_text(json.dumps(reports, indent=2))
