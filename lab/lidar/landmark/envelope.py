"""Fit a measured upper envelope inside a mapped building footprint using two flight lines."""

import math
from collections import Counter, defaultdict

import numpy as np
import shapely
from pyproj import Transformer
from scipy.spatial import cKDTree
from shapely.geometry import box, shape


def tilexy(lon, lat):
    return np.array(
        [
            (lon + 180.0) / 360 * 65536,
            (1 - np.arcsinh(np.tan(np.deg2rad(lat))) / np.pi) * 32768,
        ]
    ).T


def ground(poly, p):
    s = p[
        (p[:, 3] == 2)
        & shapely.contains_xy(
            poly.buffer(9).difference(poly.buffer(0.6)), p[:, 0], p[:, 1]
        )
    ]
    if len(s) < 75:
        raise ValueError("insufficient ground")
    centers = []
    c = np.array(poly.centroid.coords[0])
    quadrant = (
        (np.arctan2(s[:, 1] - c[1], s[:, 0] - c[0]) + np.pi) // (np.pi / 2)
    ).astype(int) % 4
    for i in range(4):
        if (quadrant == i).sum() >= 12:
            centers.append(np.median(s[quadrant == i, 2]))
    if len(centers) < 3:
        raise ValueError("insufficient local ground sectors")
    # Large public buildings can occupy a sloping city block. Use a local robust
    # plane through ground returns and check spatial residuals across the block.
    grid = np.floor(s[:, :2] / 2).astype(int)
    groups = defaultdict(list)
    for i, k in enumerate(grid):
        groups[tuple(k)].append(i)
    vox = np.array([np.median(s[ids, :3], axis=0) for ids in groups.values()])
    design = np.c_[vox[:, :2] - c, np.ones(len(vox))]
    fit = np.linalg.lstsq(design, vox[:, 2], rcond=None)[0]
    for step in range(3):
        residual = np.abs(design @ fit - vox[:, 2])
        keep = residual < max(0.28, 2.8 * np.median(residual))
        fit = np.linalg.lstsq(design[keep], vox[keep, 2], rcond=None)[0]
    residual = np.abs(design @ fit - vox[:, 2])

    # Terraces make a single plane a poor terrain model. Only its *center datum*
    # is used. Validate the datum against independent strips and dropped sectors.
    def center_estimate(xyz):
        aa = np.c_[xyz[:, :2] - c, np.ones(len(xyz))]
        m = np.linalg.lstsq(aa, xyz[:, 2], rcond=None)[0]
        for _ in range(3):
            rr = np.abs(aa @ m - xyz[:, 2])
            k = rr < max(0.30, 2.7 * np.median(rr))
            m = np.linalg.lstsq(aa[k], xyz[k, 2], rcond=None)[0]
        return float(m[2])

    line_bases = [
        center_estimate(s[s[:, 4] == line, :3])
        for line in np.unique(s[:, 4])
        if sum(s[:, 4] == line) >= 60
    ]
    if len(line_bases) < 2:
        # Some 3DEP work units classify ground in only one of the overlapping
        # strips. At those reference locations check the other strip's raw single
        # returns independently; do not assume all unclassified points are ground.
        ref_tree = cKDTree(vox[:, :2])
        dominant = int(
            np.unique(s[:, 4], return_counts=True)[0][
                np.argmax(np.unique(s[:, 4], return_counts=True)[1])
            ]
        )
        others = p[(p[:, 4] != dominant) & (p[:, 3] == 1) & (p[:, 6] == 1)]
        distances, ids = ref_tree.query(others[:, :2])
        good = (distances < 1.2) & (np.abs(others[:, 2] - vox[ids, 2]) < 0.25)
        if good.sum() >= 75 and len(np.unique(ids[good])) >= 30:
            line_bases.append(center_estimate(others[good, :3]))
    sector_bases = [center_estimate(s[quadrant != k, :3]) for k in range(4)]
    if (
        len(line_bases) < 2
        or np.ptp(line_bases) > 0.4
        or np.ptp(sector_bases) > 1.0
        or np.linalg.norm(fit[:2]) > 0.12
    ):
        raise ValueError(
            f"ground datum unsupported flight bases={line_bases}, sector bases={sector_bases}"
        )
    print(
        "ground center",
        fit[2],
        "independent strips",
        line_bases,
        "leave quadrant out",
        sector_bases,
        "P90 surface relief",
        np.percentile(residual, 90),
        flush=True,
    )
    return float(fit[2]), len(s)


GRID = 1.6


def mesh(poly):
    x0, y0, x1, y1 = poly.bounds
    look = {}
    v = []
    f = []
    edges = Counter()
    for x in np.arange(math.floor(x0 / GRID) * GRID, x1, GRID):
        for y in np.arange(math.floor(y0 / GRID) * GRID, y1, GRID):
            region = poly.intersection(box(x, y, x + GRID, y + GRID))
            if region.area < 1e-6:
                continue
            for triangle in shapely.constrained_delaunay_triangles(region).geoms:
                ids = []
                for xy in np.asarray(triangle.exterior.coords)[:3]:
                    k = tuple(np.round(xy, 5))
                    if k not in look:
                        look[k] = len(v)
                        v.append(k)
                    ids.append(look[k])
                if len(set(ids)) != 3:
                    continue
                f.append(ids)
                for a, b in zip(ids, np.roll(ids, -1)):
                    edges[tuple(sorted((int(a), int(b))))] += 1
    return np.asarray(v), np.asarray(f), [key for key, n in edges.items() if n == 1]


def surface(points, node, fitlines, fallback, base):
    # Both strips must see a consistent near-surface elevation. The survey never
    # determines facade fenestration or the material on a vertical face.
    strips = [
        points[(points[:, 4] == line) & (points[:, 3] != 18) & (points[:, 5] == 1)]
        for line in fitlines
    ]
    trees = [cKDTree(s[:, :2]) for s in strips]
    values = []
    confidence = []
    error = []
    for xy in node:
        z = None
        for radius in (1.25, 1.85, 2.5):
            samples = [s[t.query_ball_point(xy, radius)] for s, t in zip(strips, trees)]
            if min(len(a) for a in samples) < 3:
                continue
            quant = [np.percentile(a[:, 2], [90, 75, 55, 35]) for a in samples]
            # Shared upper mode, preferring a supported roof over mixed lower hits
            # at a discontinuity. Require multiple real points near that height
            # on each strip; the original coordinates are never repositioned.
            for qa, qb in zip(*quant):
                if abs(qa - qb) > 0.57:
                    continue
                mid = (qa + qb) / 2
                supporting = [s[np.abs(s[:, 2] - mid) < 0.50] for s in samples]
                if min(len(a) for a in supporting) < 2:
                    continue
                if max(
                    min(np.linalg.norm(s[:, :2] - xy, axis=1)) for s in supporting
                ) > min(radius, 1.65):
                    continue
                z = np.median([np.median(s[:, 2]) for s in supporting])
                error.append(
                    abs(np.median(supporting[0][:, 2]) - np.median(supporting[1][:, 2]))
                )
                break
            if z is not None:
                break
        confidence.append(z is not None)
        values.append(max(fallback, z - base) if z is not None else fallback)
    return np.asarray(values), np.asarray(confidence), np.asarray(error)


def build(feat, cloud, crs):
    tr = Transformer.from_crs(4326, crs, always_xy=True)
    inv = Transformer.from_crs(crs, 4326, always_xy=True)
    geom = shape(feat["geometry"])
    if geom.geom_type != "Polygon" or not geom.is_valid or geom.is_empty:
        raise ValueError("A valid mapped polygon is required")
    poly = shapely.transform(geom, tr.transform, interleaved=False)
    inside = shapely.contains_xy(poly.buffer(12), cloud[:, 0], cloud[:, 1])
    p = cloud[inside]
    base, ng = ground(poly, p)
    vs, fs, edges = mesh(poly)
    lines, n = np.unique(p[(p[:, 3] == 1) & (p[:, 5] == 1), 4], return_counts=True)
    chosen = sorted(map(int, lines[np.argsort(n)[-2:]]))
    if len(chosen) != 2:
        raise ValueError("Two independent flight strips required")
    heights, mask, err = surface(
        p, vs, chosen, float(feat["properties"]["render_height"]), base
    )
    if len(err) == 0:
        raise ValueError("No dual-strip surface support")
    rings = [
        tilexy(*np.asarray(r.coords)[:-1].T) for r in [geom.exterior, *geom.interiors]
    ]
    tile = np.floor(rings[0].mean(0)).astype(int)
    rings = [np.rint((r - tile) * 100000).astype(int).tolist() for r in rings]
    xy = np.rint((tilexy(*inv.transform(vs[:, 0], vs[:, 1])) - tile) * 100000).astype(
        int
    )
    hcm = np.rint(heights * 100).astype(int)
    aa, bb, cc = xy[fs[:, 0]], xy[fs[:, 1]], xy[fs[:, 2]]
    ba = bb - aa
    ca = cc - aa
    neg = ba[:, 0] * ca[:, 1] < ba[:, 1] * ca[:, 0]
    fs[neg] = fs[neg, ::-1]
    good = mask[fs].all(1)
    areas = np.abs(ba[:, 0] * ca[:, 1] - ba[:, 1] * ca[:, 0])
    frac = float(areas[good].sum() / areas.sum())
    print(
        "support",
        mask.mean(),
        frac,
        "err",
        np.percentile(err, [50, 90, 99]),
        "height range",
        np.percentile(heights, [0, 5, 50, 95, 100]),
        flush=True,
    )
    if mask.mean() < 0.88 or frac < 0.78 or np.percentile(err, 90) > 0.4:
        raise ValueError("insufficient two-strip corroboration")
    qa = dict(
        supported_nodes=int(mask.sum()),
        total_nodes=len(mask),
        fully_supported_area=round(frac, 4),
        strip_numbers=chosen,
        strip_discrepancy_median=round(float(np.median(err)), 3),
        strip_discrepancy_p90=round(float(np.percentile(err, 90)), 3),
        local_ground_datum_m=round(base, 3),
        ground_returns=ng,
        fallback_m=feat["properties"]["render_height"],
    )
    return tile, dict(
        reference=feat["id"],
        footprint=rings,
        eave_m=feat["properties"]["render_height"],
        v=np.c_[xy, hcm].tolist(),
        f=fs.ravel().tolist(),
        edge=np.asarray(edges).ravel().tolist(),
        qa=qa,
    )
