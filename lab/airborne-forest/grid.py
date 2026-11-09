from config import root as p, cfg, swath
from scipy import ndimage
import numpy as np

raw = np.load(p / "forest-cloud.npz")["p"]
ground = raw[raw[:, 3] == 2]
if len(ground) < 500:
    raise ValueError("insufficient classified ground")
# A 2 m independently supported surface grid. Top returns are normalized at their
# own XY against the local classified ground, then aggregated, preserving local relief.
step = float(cfg["heightGridM"])
if step < 1.5 or step > 3:
    raise ValueError("unsupported sampling resolution")
origin = np.floor(np.asarray(swath[:2]) / step) * step - step
w, h = np.ceil((np.asarray(swath[2:]) - origin) / step).astype(int) + 2


def grid(pts):
    gx = np.floor((pts[:, 0] - origin[0]) / step).astype(int)
    gy = np.floor((pts[:, 1] - origin[1]) / step).astype(int)
    return gy * w + gx


ix = grid(ground)
used, cnt = np.unique(ix, return_counts=True)
order = np.lexsort((ground[:, 2], ix))
offset = np.r_[0, np.cumsum(cnt)]
gmean = np.full(w * h, np.nan, dtype="float32")
gmean[used] = ground[order[offset[:-1] + cnt // 2], 2]
gmean = gmean.reshape(h, w)
gy, gx = np.indices((h, w))
idx = ndimage.distance_transform_edt(~np.isfinite(gmean), return_indices=True)[1]
valid = np.hypot(idx[0] - gy, idx[1] - gx) < 3.5
g_near = gmean[tuple(idx)]
g_near[~valid] = np.nan
# Class 1 includes vegetation, so enforce multiple returns, measured ground and corroborating satellite evidence downstream.
pts = raw[(raw[:, 3] == 1) & (raw[:, 5] == 1) & (raw[:, 6] >= 2)]
ip = grid(pts)
ph = pts[:, 2] - g_near.ravel()[ip]
good = np.where((ph >= 2.7) & (ph <= 55))[0]
ip = ip[good]
pz = ph[good]
pts = pts[good]
print("measured first of multiple returns", len(pts), flush=True)


def surface(mask):
    key = ip[mask]
    hh = pz[mask]
    keys, count = np.unique(key, return_counts=True)
    rank = np.lexsort((hh, key))
    start = np.r_[0, np.cumsum(count)[:-1]]
    canopy = np.full(w * h, np.nan, dtype="float32")
    canopy[keys] = hh[rank[start + np.floor((count - 1) * 0.85).astype(int)]]
    return canopy.reshape(h, w)


# Independent strip evidence. Select the two largest flights with actual canopy returns for the diagnostic.
flight, counts = np.unique(pts[:, 4], return_counts=True)
if len(flight) < 2:
    raise ValueError("require repeated flight-line support")
first, second = flight[np.argsort(counts)[-2:]]
a = surface(pts[:, 4] == first)
b = surface(pts[:, 4] == second)
both = np.isfinite(a) & np.isfinite(b)
if both.sum() < 200:
    raise ValueError("insufficient airborne overlap")
allh = surface(np.ones(len(ip), dtype=bool))
np.savez_compressed(
    p / "surface.npz", chm=allh, ground=g_near, resolution=step, origin=origin, a=a, b=b
)
