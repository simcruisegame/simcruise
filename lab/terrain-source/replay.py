"""Print terrain-relative errors from the Alaska and multistate reference measurements."""
from pathlib import Path
import numpy as np

root = Path(__file__).parent
with np.load(root / "road-evidence.npz", allow_pickle=False) as observations:
    for site in observations.files:
        # Per occupied 5 m road cell: local east/south, class-2 median,
        # previous mesh elevation and revised mesh elevation, all in meters.
        values = observations[site].astype(float)
        residual = values[:, 3:] - values[:, 2, None]
        error = np.abs(residual - np.median(residual, axis=0))
        print(site, len(values), np.round(np.percentile(error, [50, 90], axis=0), 3).tolist())
