import math, json
from pathlib import Path
import numpy as np
from scipy.optimize import least_squares

r = Path(__file__).resolve().parent
d = json.loads((r / "footprint.json").read_text())
row = d["building"]
lat = 44.408
lon = -70.79333333333334
merc = lambda la, lo: np.array(
    [(lo + 180) / 360 * 65536, (1 - math.asinh(math.tan(math.radians(la))) / math.pi) * 32768]
)
base = merc(lat, lon)
side = 611.496 * math.cos(math.radians(lat))
shape = np.asarray(row[0][0], float)
plan = (shape - base % 1) * np.array([side, -side])
f = 320 / math.tan(math.radians(61.041895 / 2))
cx = 320
cy = 239.0


def ground(en):
    uv = base % 1 + np.array([en[0], -en[1]]) / side
    g = uv * d["grid"]
    x, y = g.astype(int)
    g -= g.astype(int)
    h = np.array(d["h"]).reshape((65, 65))
    a, b = g
    return (
        h[y, x] * (1 - a) * (1 - b)
        + h[y, x + 1] * a * (1 - b)
        + h[y + 1, x] * (1 - a) * b
        + h[y + 1, x + 1] * a * b
    )


def projection(points, state):
    east, north, yaw, pitch_up, roll, height = state
    yaw, pitch, roll = np.radians([yaw, pitch_up, roll])
    eye = ground((east, north)) + 1.65
    right = np.array([np.cos(yaw), -np.sin(yaw), 0])
    forward = np.array([np.sin(yaw) * np.cos(pitch), np.cos(yaw) * np.cos(pitch), np.sin(pitch)])
    up = np.cross(right, forward)
    q = points - np.array([east, north, eye])
    u = q @ right
    v = q @ up
    z = q @ forward
    x = (u * np.cos(roll) - v * np.sin(roll)) / z
    y = (u * np.sin(roll) + v * np.cos(roll)) / z
    return np.column_stack([cx + f * x, cy - f * y])


# Three anchor pixels use the top edge of the white metal coping. The north-
# west corner is tree-obscured; its ground-level abscissa receives lower weight.
lines = [
    (7, 6, [(226, 146.6), (273, 154.8), (372, 170.2)]),
    (6, 5, [(430, 168.7), (493, 154.2)]),
    (5, 4, [(574, 152), (617, 165)]),
]


def residual(s):
    top = projection(np.c_[plan, np.full(8, s[5])], s)
    ret = []
    for a, b, pts in lines:
        aa, bb = top[[a, b]]
        for x, y in pts:
            estimate = aa[1] + (bb[1] - aa[1]) * (x - aa[0]) / (bb[0] - aa[0])
            ret.append((estimate - y) / 1.6)
    for i, x, y in [(6, 408, 174), (5, 552, 144)]:
        ret.extend((top[i] - [x, y]) / [1.9, 1.8])
    # Independent plank wall outlines and original compass provide weak priors.
    ret.append((top[0, 0] - 11) / 8)
    ret.append((s[2] - 25.65) / 5.0)
    ret.append((s[4] - 0.6) / 3.0)
    return ret


st = least_squares(
    residual,
    [5.76, -1.13, 24.76, 0.0, 0.0, 217],
    bounds=([-12, -14, 8, -9, -7, 211.5], [16, 14, 42, 12, 7, 225]),
).x
uv = base % 1 + st[:2] * np.array([1, -1]) / side
lonlat = (
    (base[0] // 1 + uv[0]) / 65536 * 360 - 180,
    math.degrees(math.atan(math.sinh(math.pi * (1 - (base[1] // 1 + uv[1]) / 32768)))),
)
center = ground((plan.mean(axis=0)))
out = {
    "pose": [*lonlat, st[2], -st[3], st[4]],
    "state": st.tolist(),
    "eave_absolute_game_m": st[5],
    "eave_center_m": st[5] - ground((np.array(row[4:6]) - base % 1) * np.array([side, -side])),
    "plan": plan.tolist(),
    "vertices": shape.tolist(),
    "residual_px": np.round(np.asarray(residual(st)), 2).tolist(),
    "top_projected": np.round(projection(np.c_[plan, np.full(8, st[5])], st), 2).tolist(),
}
if __name__ == "__main__":
    print(json.dumps(out))
    (r / "geometry.json").write_text(json.dumps(out, indent=2))
