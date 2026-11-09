from collections import defaultdict

import numpy as np
import shapely
from pyproj import Transformer


def check(doc, p, sphere, metric_crs):
    r = doc["roofs"][0]
    v = np.asarray(r["v"])
    faces = np.asarray(r["f"]).reshape(-1, 3)
    tile = np.asarray(doc["tile"])
    half = 20037508.342789244
    step = 2 * half / 65536
    merc = Transformer.from_crs(3857, metric_crs, always_xy=True)

    def uv_to_xy(uv):
        q = tile + uv / 1.0e5
        return np.column_stack(
            merc.transform(q[:, 0] * step - half, half - q[:, 1] * step)
        )

    xy = uv_to_xy(v[:, :2])
    z = v[:, 2] / 100 + r["qa"]["local_ground_datum_m"]
    allowed = np.zeros(len(z), dtype=bool)
    if sphere:
        cc = np.array(sphere["center_metric"])
        rad = sphere["radius"]
        ds = xy - cc[:2]
        rho = np.linalg.norm(ds, axis=1)
        bins = ((np.arctan2(ds[:, 1], ds[:, 0]) + np.pi) / (2 * np.pi) * 16).astype(
            int
        ) % 16
        target = cc[2] + np.sqrt(np.maximum(0.0, rad**2 - rho**2))
        rr = np.array(sphere["supported_r_by_16_sectors"])
        allowed = (rho < rr[bins] - 0.30) & (np.abs(z - target) < 0.62)
        z[allowed] = target[allowed]
    newcm = np.rint((z - r["qa"]["local_ground_datum_m"]) * 100).astype(int)
    r["v"] = np.c_[v[:, :2], newcm].tolist()
    rings = [uv_to_xy(np.array(ring)) for ring in r["footprint"]]
    metric_poly = shapely.Polygon(rings[0], rings[1:])
    inner = metric_poly.buffer(-0.8)
    p = p[
        (p[:, 3] == 1) & (p[:, 5] == 1) & shapely.contains_xy(inner, p[:, 0], p[:, 1])
    ]
    t = shapely.STRtree(shapely.polygons(xy[faces]))
    pairs = t.query(shapely.points(p[:, :2]), predicate="intersects")
    q, k = pairs
    a, b, c = np.transpose(xy[faces[k]], (1, 0, 2))
    ba = b - a
    ca = c - a
    qa = p[q, :2] - a
    det = ba[:, 0] * ca[:, 1] - ba[:, 1] * ca[:, 0]
    good = np.abs(det) > 1e-6
    q = q[good]
    k = k[good]
    ba = ba[good]
    ca = ca[good]
    qa = qa[good]
    det = det[good]
    bx = (qa[:, 0] * ca[:, 1] - qa[:, 1] * ca[:, 0]) / det
    cx = (ba[:, 0] * qa[:, 1] - ba[:, 1] * qa[:, 0]) / det
    za, zb, zc = z[faces[k]].T
    pred = za + (zb - za) * bx + (zc - za) * cx

    def stats(a):
        return {
            "n": len(a),
            "mae_m": round(float(np.mean(abs(a))), 3),
            "median_m": round(float(np.median(abs(a))), 3),
            "p90_m": round(float(np.percentile(abs(a), 90)), 3),
        }

    # All first returns, plus a separate direct test on surfaces with locally coherent
    # withheld heights. Keep every result for honest coverage of discontinuities.
    e = pred - p[q, 2]
    flat = r["qa"]["fallback_m"] + r["qa"]["local_ground_datum_m"] - p[q, 2]
    data = defaultdict(list)
    for i, vv in enumerate(p[q]):
        data[tuple(np.floor(vv[:2] / 1.8).astype(int))].append(i)
    coherent = [
        v
        for v in data.values()
        if len(v) >= 3 and np.ptp(np.percentile(p[q[v], 2], [10, 90])) < 0.55
    ]
    if len(e) < 100 or not coherent:
        raise ValueError("Insufficient withheld first returns or coherent patches")
    indices = np.concatenate(coherent)
    report = {
        "reserved_returns_in_footprint": len(p),
        "all_first_returns": stats(e),
        "all_first_returns_flat": stats(flat),
        "coherent_cells_reserved_returns": stats(e[indices]),
        "coherent_cells_flat": stats(flat[indices]),
        "sphere_smoothed_vertices": int(allowed.sum()),
        "sphere": sphere,
    }
    if sphere:
        b = p[(np.linalg.norm(p[:, :2] - cc[:2], axis=1) < rad * 0.50)]
        if len(b) < 24:
            raise ValueError("Insufficient reserved returns over candidate cap")
        pred = cc[2] + np.sqrt(rad**2 - np.sum((b[:, :2] - cc[:2]) ** 2, axis=1))
        report["sphere_inner_half_reserved_all_first_returns"] = stats(pred - b[:, 2])
        if report["sphere_inner_half_reserved_all_first_returns"]["p90_m"] > 0.35:
            raise ValueError("Cap fails completely reserved core test")
    if (
        len(e) < 100
        or stats(e)["mae_m"] >= stats(flat)["mae_m"]
        or stats(e[indices])["mae_m"] > 0.5
    ):
        raise ValueError(
            "Insufficient withheld roof accuracy or no baseline improvement"
        )
    r["qa"]["reserved_test"] = report["coherent_cells_reserved_returns"]
    r["qa"]["sphere_smoothed"] = int(allowed.sum())
    return report
