"""Compare gridded elevation and local rise/run. Run with Python and NumPy."""
import json
from pathlib import Path

import numpy as np

root = Path(__file__).resolve().parent
catalog = json.loads((root / "sources.json").read_text())
for place, source in catalog.items():
    with np.load(root / (place.lower() + ".npz")) as grid:
        ref = grid["usgs"]
        spacing = source["spacingM"]
        baseline = 5 * spacing
        for zoom in ("z13", "z14"):
            error = grid[zoom] - ref
            east = (error[:, 5:] - error[:, :-5]) / baseline
            north = (error[5:, :] - error[:-5, :]) / baseline
            grade = 100 * np.mean([np.nanmean(abs(east)), np.nanmean(abs(north))])
            print(f"{place:12} {zoom}  elevation MAE {np.nanmean(abs(error)):.3f} m"
                  f"   gradient MAE {grade:.3f} percentage points")
