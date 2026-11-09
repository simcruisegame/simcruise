import numpy as np, json
from scipy.spatial import cKDTree
from config import root as p, cfg

train, test = cfg["groundFlightPair"]
v = np.load(p / "forest-cloud.npz")["p"]
ground = v[v[:, 3] == 2]
r99, r100 = ground[ground[:, 4] == train], ground[ground[:, 4] == test][::11]
kd = cKDTree(r99[:, :2])
ds, ii = kd.query(r100[:, :2], k=7)
keep = ds[:, -1] < 4
error = []
for j in np.where(keep)[0]:
    rel = r99[ii[j], :2] - r100[j, :2]
    a = np.c_[rel, np.ones(len(rel))]
    if np.linalg.cond(a) > 60:
        continue
    coef = np.linalg.lstsq(a, r99[ii[j], 2], rcond=None)[0]
    error.append(abs(float(r100[j, 2] - coef[-1])))
sf = np.load(p / "surface.npz")
a, b = sf["a"], sf["b"]
aa = np.isfinite(a)
bb = np.isfinite(b)
v1 = np.nan_to_num(a)
v2 = np.nan_to_num(b)
from scipy.ndimage import maximum_filter

a = maximum_filter(v1, size=5)
b = maximum_filter(v2, size=5)
use = (a > 5) & (b > 5) & aa & bb
result = {
    "heldGroundFlight": test,
    "groundTrainFlight": train,
    "groundTestPoints": len(error),
    "groundAbsMedianM": float(np.median(error)),
    "groundAbsP90M": float(np.percentile(error, 90)),
    "crownComparisonWindowM": 10,
    "crownCellsBothFlights": int(use.sum()),
    "crownTopDeltaMedianM": float(np.median(abs(a[use] - b[use]))),
    "crownTopDeltaP90M": float(np.percentile(abs(a[use] - b[use]), 90)),
}
(p / "flight-holdout.json").write_text(json.dumps(result, indent=2))
print(result)
