"""Fit optional upper spherical surfaces only with cross-flight support."""

import numpy as np
import shapely
from pyproj import Transformer
from scipy.optimize import least_squares
from shapely.geometry import shape


def fit_candidate(shape1, p, metric_crs, vertical_origin):
    proj = Transformer.from_crs(4326, metric_crs, always_xy=True)
    geom = shapely.transform(
        shape(shape1["geometry"]), proj.transform, interleaved=False
    )
    mid = np.array(geom.centroid.coords[0])
    R = np.sqrt(geom.area / np.pi)
    circle = 4 * np.pi * geom.area / geom.length**2
    if circle < 0.88 or not 5 < R < 25:
        return
    selected = (
        (np.linalg.norm(p[:, :2] - mid, axis=1) < R * 1.25)
        & (p[:, 3] == 1)
        & (p[:, 5] == 1)
        & (p[:, 2] > vertical_origin + 5)
    )
    pp = p[selected]
    ps = pp[:, :3] - np.r_[mid, vertical_origin]
    lines, counts = np.unique(pp[:, 4], return_counts=True)
    if len(lines) < 2:
        return
    lines = sorted(lines[np.argsort(counts)[-2:]])
    if min(np.sum(pp[:, 4] == v) for v in lines) < 100:
        return
    tr = ps[pp[:, 4] == lines[0]]
    hold = ps[pp[:, 4] == lines[1]]
    rng = np.random.default_rng(752)
    ids = rng.integers(0, len(tr), (1800, 4))
    a = tr[ids]
    sols = 2 * (a[:, 1:] - a[:, :1])
    y = (a[:, 1:] ** 2).sum(2) - (a[:, :1] ** 2).sum(2)
    ok = abs(np.linalg.det(sols)) > 1.0
    cc = np.linalg.solve(sols[ok], y[ok, :, None])[:, :, 0]
    r = np.linalg.norm(a[ok, 0] - cc, axis=1)
    valid = (
        (r > R * 0.65) & (r < R * 1.9) & (np.linalg.norm(cc[:, :2], axis=1) < R * 0.8)
    )
    cc = cc[valid]
    r = r[valid]
    if not len(r):
        return
    score = []
    ss = tr[:: max(1, len(tr) // 1000)]
    for c, rr in zip(cc, r):
        dist = np.abs(np.linalg.norm(ss - c, axis=1) - rr)
        score.append(np.sum((dist < 0.22) & (ss[:, 2] > c[2] + rr * 0.15)))
    ib = np.argmax(score)
    m0 = np.r_[cc[ib], r[ib]]

    def spheres(s, m):
        return np.linalg.norm(s - m[:3], axis=1) - m[3]

    def okp(s, m):
        return (abs(spheres(s, m)) < 0.30) & (s[:, 2] > m[2] + 0.15 * m[3])

    m = m0
    for _ in range(3):
        if np.count_nonzero(okp(tr, m)) < 12:
            return None
        m = least_squares(
            lambda mm: spheres(tr[okp(tr, m)], mm), m, loss="soft_l1", f_scale=0.10
        ).x
    a, b = tr[okp(tr, m)], hold[okp(hold, m)]
    if min(len(a), len(b)) < 75:
        return None

    def bins(x):
        return (
            (np.arctan2(x[:, 1] - m[1], x[:, 0] - m[0]) + np.pi) / (2 * np.pi) * 16
        ).astype(int) % 16

    def rad(x):
        return np.linalg.norm(x[:, :2] - m[:2], axis=1)

    ra, rb = rad(a), rad(b)
    radius_by_angle = np.array(
        [
            [
                np.percentile(rad(z)[bins(z) == j], 97)
                if (bins(z) == j).sum() >= 3
                else 0
                for j in range(16)
            ]
            for z in [a, b]
        ]
    )
    if (radius_by_angle.min(0) > R * 0.45).sum() < 12:
        return None
    lon, lat = Transformer.from_crs(metric_crs, 4326, always_xy=True).transform(
        *(mid + m[:2])
    )
    pred = m[2] + np.sqrt(np.maximum(0.0, m[3] ** 2 - rb**2))
    e = np.abs(pred - b[:, 2])
    print(
        "SPHERE",
        shape1["id"],
        round(circle, 2),
        [lon, lat],
        np.round(m, 2),
        "fit",
        len(a),
        len(b),
        "angles",
        np.round(radius_by_angle.min(0), 1),
        "MAE",
        e.mean(),
        "P90",
        np.percentile(e, 90),
    )
    if np.percentile(e, 90) > 0.40:
        return None
    return dict(
        id=shape1["id"],
        center_metric=[*list(mid + m[:2]), vertical_origin + m[2]],
        radius=m[3],
        mapped_radius=R,
        supported_r_by_16_sectors=radius_by_angle.min(0).tolist(),
        independent_n=len(b),
        plane_mae=np.mean(e),
        p90=np.percentile(e, 90),
    )
