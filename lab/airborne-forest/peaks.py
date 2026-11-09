"""Derive canopy peaks and fitted crown footprints from lidar and two independent satellite products."""

import json, math, base64, zlib
import numpy as np
from scipy import ndimage
from config import root as p, side, cx, cy

cache = p / "canopy"
s = np.load(p / "surface.npz")
chm = s["chm"]
ground = s["ground"]
origin = s["origin"]
step = float(s["resolution"])
a, b = s["a"], s["b"]
h, w = chm.shape
yy, xx = np.indices((h, w))
east = origin[0] + (xx + 0.5) * step
north = origin[1] + (yy + 0.5) * step
tx = cx + east / side
ty = cy - north / side
optical = np.zeros((h, w), dtype=float)
evergreen = np.zeros((h, w), dtype=bool)
for f in cache.glob("c2-*"):
    name = f.name.split("-")
    if len(name) != 3 or not name[1].isdigit():
        continue
    x, y = map(int, name[1:])
    roi = (tx >= x) & (tx < x + 1) & (ty >= y) & (ty < y + 1)
    tile = np.frombuffer(zlib.decompress(f.read_bytes()), dtype="uint8").reshape(
        512, 512
    )
    optical[roi] = tile[
        ((ty[roi] - y) * 512).astype(int), ((tx[roi] - x) * 512).astype(int)
    ]
for f in cache.glob("forest-form*.json"):
    doc = json.loads(f.read_text())
    if doc.get("status", "available") != "available" or "bits" not in doc:
        continue
    x, y = doc["tile"]
    grid = doc["grid"]
    bits = np.unpackbits(
        np.frombuffer(base64.b64decode(doc["bits"]), dtype=np.uint8), bitorder="little"
    )[: grid * grid].reshape(grid, grid)
    roi = (tx >= x * 4) & (ty >= y * 4) & (tx < (x + 1) * 4) & (ty < (y + 1) * 4)
    evergreen[roi] = bits[
        ((ty[roi] / 4 - y) * grid).astype(int), ((tx[roi] / 4 - x) * grid).astype(int)
    ]
can = np.nan_to_num(chm)
can[~np.isfinite(ground)] = 0
# Canopy sits above supported ground, includes multiple-return structure, and has an independent positive optical height.
forest = (
    (can >= 3.5) & (ndimage.maximum_filter(optical, size=3) >= 3) & np.isfinite(ground)
)
local_top = ndimage.maximum_filter(np.where(forest, can, 0), size=3)
# Broader neighborhoods produce one crest per 4–7 m, preserving both narrow conifers and real river gaps.
sm = ndimage.gaussian_filter(local_top, 0.70)
good = forest & (can >= 5) & (sm >= ndimage.maximum_filter(sm, size=3) - 0.001)
# Choose the strongest actual lidar cell from each summit neighbourhood.
points = []
for y, x in zip(*np.nonzero(good)):
    yl = max(0, y - 1)
    xl = max(0, x - 1)
    z = np.where(forest[yl : y + 2, xl : x + 2], can[yl : y + 2, xl : x + 2], 0)
    i, j = np.unravel_index(np.argmax(z), z.shape)
    points.append((can[yl + i, xl + j], yl + i, xl + j))
points.sort(reverse=True)
taken = np.zeros((h, w), bool)
rows = []
classes = [0, 0, 0]
for height, y, x in points:
    if taken[y, x]:
        continue
    # Reject objects with weak local crown support or inadequate sampled terrain. Both flight lines must agree when present.
    ys = slice(max(0, y - 2), min(h, y + 3))
    xs = slice(max(0, x - 2), min(w, x + 3))
    if np.sum(forest[ys, xs]) < 5:
        continue
    qa = [
        float(np.nanmax(v[ys, xs])) if np.isfinite(v[ys, xs]).sum() >= 3 else None
        for v in (a, b)
    ]
    if None not in qa and abs(qa[0] - qa[1]) > max(5.0, height * 0.34):
        continue
    if max(qa[0] or 0, qa[1] or 0) < height * 0.66:
        continue
    # Estimate horizontal extent from the connected elevated canopy around the summit.
    lengths = []
    for dy, dx in [
        (0, 1),
        (1, 1),
        (1, 0),
        (1, -1),
        (0, -1),
        (-1, -1),
        (-1, 0),
        (-1, 1),
    ]:
        length = 0.5
        for k in range(1, 6):
            Y = y + dy * k
            X = x + dx * k
            if not (
                0 <= Y < h
                and 0 <= X < w
                and local_top[Y, X] > height * 0.58
                and optical[Y, X] >= 2
            ):
                break
            length = (k + 0.35) * math.hypot(dx, dy)
        lengths.append(length * step)
    width = np.clip(np.median(lengths) * 1.6, 2.1, min(13.0, float(height) * 0.70))
    rad = max(1.45, float(width) * 0.48 / step)
    r = math.ceil(rad)
    Y, X = np.ogrid[
        max(0, y - r) : min(h, y + r + 1), max(0, x - r) : min(w, x + r + 1)
    ]
    taken[max(0, y - r) : min(h, y + r + 1), max(0, x - r) : min(w, x + r + 1)] |= (
        Y - y
    ) ** 2 + (X - x) ** 2 <= rad**2
    # A tree peak is not an independently surveyed trunk. Only prevailing evergreen stand cells get needleleaf form.
    # Estimate upper-to-lower crown width from the physical returns, even where a forest-type pixel is mixed.
    crownrad = max(2, int(round(width / step * 0.65)))
    ys1 = slice(max(0, y - crownrad), min(h, y + crownrad + 1))
    xs1 = slice(max(0, x - crownrad), min(w, x + crownrad + 1))
    radius = np.hypot(east[ys1, xs1] - east[y, x], north[ys1, xs1] - north[y, x])
    core = (radius <= width * 0.62) & forest[ys1, xs1]
    values = can[ys1, xs1]
    upper = radius[core & (values > height * 0.88)]
    lower = radius[core & (values > height * 0.50)]
    fraction = (
        float(np.quantile(upper, 0.8) / max(1, np.quantile(lower, 0.8)))
        if len(upper) >= 2 and len(lower) >= 5
        else 0.72
    )
    classes[int(evergreen[y, x])] += 1
    rows.append(
        [
            round(float(east[y, x]), 2),
            round(float(north[y, x]), 2),
            round(float(ground[y, x]), 2),
            round(float(height), 2),
            round(float(width), 2),
            2 if None not in qa else 1,
            bool(evergreen[y, x]),
            round(float(np.clip(fraction, 0.15, 1.0)), 2),
        ]
    )
d = np.hypot(np.array(rows)[:, 0], np.array(rows)[:, 1])
print(
    "peaks by stand",
    classes,
    "accepted by distance",
    [
        (a, b, int(((d >= a) & (d < b)).sum()))
        for a, b in [(0, 50), (50, 150), (150, 400), (400, 750), (750, 1700)]
    ],
    "flight both",
    sum(r[5] == 2 for r in rows),
)
(p / "forest-rows.json").write_text(json.dumps(rows, separators=(",", ":")))
