"""Fit a separate primitive to a roof over the entrance; the east wing occludes much of it."""

import json
import numpy as np
from scipy.optimize import least_squares
from geometry import base, out, plan, projection, r, side, st
import pyproj

arr = np.load(r / "building-cloud.npy")
x, y, z, cls, strip, ret = arr.T
lo, la = pyproj.Transformer.from_crs(6348, 4326, always_xy=True).transform(x, y)
uv = np.column_stack(
    [(lo + 180) / 360 * 65536, (1.0 - np.arcsinh(np.tan(np.radians(la))) / np.pi) * 32768]
)
en = (uv - base) * np.array([side, -side])
axis = plan[6] - plan[7]
L = np.linalg.norm(axis)
axis /= L
outward = np.array([axis[1], -axis[0]])
frame = np.column_stack([axis, outward])
local = (en - plan[7]) @ frame
u, v = local.T
us = [10.70, L]
vs = [0.0, 3.18]
# The source image provides the visible, left-front roof corner. Clip where the
# measured entrance roof abuts the mapped east wing. All first returns count in
# the interior held-out comparison, including unrelated classes above ground.
mask = (u > us[0] + 0.18) & (u < us[1] - 0.18) & (v > 0.18) & (v < vs[1] - 0.18) & (ret == 1)
A = np.column_stack([u, v, np.ones_like(u)])
inliers = mask & (strip == 154) & (z > 215.4) & (z < 216.2)
fit = least_squares(
    lambda m: A[inliers] @ m - z[inliers], [0, 0, 215.86], loss="huber", f_scale=0.06
).x
hold = mask & (strip == 155)
res = (A[hold] @ fit) - z[hold]
sroof = 218.91
center_m = st[5] - out["eave_center_m"]
corners = [[us[0], vs[0]], [us[0], vs[1]], [us[1], vs[1]], [us[1], vs[0]]]


def uv3(u, v, dz=0):
    pos = plan[7] + frame @ [u, v]
    h = np.dot([u, v, 1], fit) - sroof + st[5] - dz
    return [*(base % 1 + pos * [1, -1] / side), h - center_m]


def lift(px, datum):
    return least_squares(
        lambda nm: (
            projection(
                np.array(
                    [[*(plan[7] + frame @ nm), np.dot([*nm, 1], fit) - sroof + st[5] - datum]]
                ),
                st,
            )[0]
            - px
        ),
        [12.8, 2.0],
    ).x


caps = [
    lift(p, 0.54) for p in [(370.5, 229.9), (417.7, 230.3)]
]  # prism centre at first visible brick row below soffit
pillars = []
for u1, v1 in caps:
    if us[0] <= u1 <= us[1] and 0 <= v1 <= vs[1]:
        pillars.append(
            [
                uv3(u1 + du, v1 + dv, 0.54)
                for du, dv in [(-0.23, -0.23), (-0.23, 0.23), (0.23, 0.23), (0.23, -0.23)]
            ]
        )
# Only fascia/upper pier shapes are identifiable, and use independent world geometry.
data = {
    "corners": [uv3(*pt) for pt in corners],
    "fascia_height_m": 0.54,
    "top_lip_m": 0.12,
    "piers": pillars,
    "fascia_srgb": [0.14, 0.203, 0.209],
    "top_srgb": [0.105, 0.129, 0.134],
    "soffit_srgb": [0.57, 0.53, 0.44],
}
(r / "awning-data.json").write_text(json.dumps(data, indent=2))
truth = {
    "from_strip": 154,
    "heldout_strip": 155,
    "fit_return_count": int(inliers.sum()),
    "test_all_first_returns_inside": int(hold.sum()),
    "within_25_cm": int((abs(res) < 0.25).sum()),
    "median_absolute_residual_m": round(float(np.median(abs(res))), 3),
    "median_roof_154_m_NAVD88": float(np.median(z[inliers])),
    "median_roof_155_m_NAVD88": float(np.median(z[hold])),
    "length_m": round(float(us[1] - us[0]), 2),
    "projection_m": vs[1],
    "centers_wall_xy": np.round(caps, 2).tolist(),
    "slope_fit": fit.tolist(),
    "limits": "Source photo supplies the exposed front corner, color and fascia. Visible masonry pier widths and thickness are approximate; the surveyed plane validates roof position. Upper roof separated from the recess according to the mapped wing.",
}
(r / "awning-validation.json").write_text(json.dumps(truth, indent=2))
print(truth)
print("piers", len(pillars))
