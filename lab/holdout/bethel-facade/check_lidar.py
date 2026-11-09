import laspy, numpy as np, pyproj, shapely, json
from shapely.geometry import Polygon
from scipy.optimize import least_squares
from geometry import base, ground, lonlat, r, row, shape, side, st

tr = pyproj.Transformer.from_crs(4326, 6348, always_xy=True)


def world(uv):
    x, y = uv.T
    lon = (19880 + x) / 65536 * 360 - 180
    lat = np.degrees(np.arctan(np.sinh(np.pi * (1 - (23726 + y) / 32768))))
    return np.column_stack(tr.transform(lon, lat))


poly = Polygon(world(shape))
camera = np.array(tr.transform(*lonlat))
bounds = poly.buffer(18).bounds
selected = []
with laspy.open(r / "2016.laz") as reader:
    for p in reader.chunk_iterator(400000):
        x = np.asarray(p.x)
        y = np.asarray(p.y)
        near = (x > bounds[0]) & (x < bounds[2]) & (y > bounds[1]) & (y < bounds[3])
        if near.any():
            selected.append(
                np.column_stack(
                    [
                        x[near],
                        y[near],
                        np.asarray(p.z)[near],
                        p.classification[near],
                        p.point_source_id[near],
                        p.return_number[near],
                    ]
                )
            )
arr = np.concatenate(selected)
x, y, z, cls, strip, ret = arr.T


def inside(region):
    return shapely.contains_xy(region, x, y)


def stats(test):
    v = z[test]
    return (
        dict(n=len(v), q=np.round(np.quantile(v, [0.1, 0.25, 0.5, 0.75, 0.9]), 2).tolist())
        if len(v)
        else None
    )


interior = inside(poly.buffer(-1.4))
nearby = inside(poly.buffer(1.7)) & ~inside(poly.buffer(0.3))
np.save(r / "building-cloud.npy", arr)
out = {
    "known_roof_extent_m": list(np.round(poly.bounds, 2)),
    "all_classes_inside": {str(int(k)): stats(interior & (cls == k)) for k in set(cls)},
    "roof_and_eye_by_strip": {},
    "center_ground_surrounding_plane": None,
}
# Fit a robust local terrain plane using classified ground along the perimeter to infer the hidden footprint-centre elevation.
q = nearby & (cls == 2)
c0 = np.array([poly.centroid.x, poly.centroid.y])
A = np.c_[x[q] - c0[0], y[q] - c0[1], np.ones(q.sum())]
m = least_squares(lambda v: A @ v - z[q], [0, 0, np.median(z[q])], loss="huber", f_scale=0.18).x
out["center_ground_surrounding_plane"] = {
    "points": int(q.sum()),
    "slope_east_north_height": m.tolist(),
}
# Independent dates and swaths. Some USGS LPC files leave buildings unclassified; use actual return class counts and trim all tails.
mask_roof = interior & (z > m[2] + 5) & (z < m[2] + 18) & (ret == 1)
for s in sorted(set(strip)):
    roof = stats(mask_roof & (strip == s))
    cam = stats((np.hypot(x - camera[0], y - camera[1]) < 4) & (cls == 2) & (strip == s))
    if roof or cam:
        out["roof_and_eye_by_strip"][str(int(s))] = {"roof": roof, "camera_ground": cam}
out["photo_fit_eave_over_center"] = st[5] - ground(
    (np.array(row[4:6]) - base % 1) * np.array([side, -side])
)
out["photo_fit_roof_above_eye"] = st[5] - ground(st[:2]) - 1.65
(r / "lidar-validation.json").write_text(json.dumps(out, indent=2))
print(json.dumps(out))
