"""Cut the Beck et al. (2023) 1 km Koppen-Geiger map down to the game's global class grid.

Source: Beck, H.E. et al. (2023), "High-resolution (1 km) Koppen-Geiger maps for 1901-2099
based on constrained CMIP6 projections", Scientific Data 10, 724,
doi:10.6084/m9.figshare.21789074 (CC BY 4.0). Download koppen_geiger_tif.zip from that page
and unpack it anywhere outside the repository; this script reads
1991_2020/koppen_geiger_0p00833333.tif (43200 x 21600, uint8, 0 = sea or no data,
1 Af ... 30 EF; row 0 at 90 N, column 0 at 180 W).

Each output cell takes the class most of its land pixels have (sea pixels do not vote, so a
coastal cell keeps its land class); a cell with no land stays 0. The result is one 8-bit grey
PNG, lossless: data/climate/koppen-1991-2020.png, read by
Source/SimCruise/Private/Knowledge/SimCruiseClimate.cpp.

Usage: python -I lab/koppen/make_grid.py <path to koppen_geiger_0p00833333.tif> [factor]
factor 6 (default) gives 0.05 degree cells (7200 x 3600). Needs numpy and Pillow.
"""
import os
import sys

import numpy as np
from PIL import Image


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    src = sys.argv[1]
    factor = int(sys.argv[2]) if len(sys.argv) > 2 else 6
    Image.MAX_IMAGE_PIXELS = None
    img = Image.open(src)
    a = np.asarray(img, dtype=np.uint8)
    h, w = a.shape
    print(f"read {src}: {w} x {h}, classes {np.unique(a)[:40]}")
    assert h % factor == 0 and w % factor == 0, "factor must divide the grid"
    oh, ow = h // factor, w // factor
    blocks = a.reshape(oh, factor, ow, factor)
    best = np.zeros((oh, ow), dtype=np.uint8)
    best_n = np.zeros((oh, ow), dtype=np.uint16)
    for c in range(1, 31):
        n = (blocks == c).sum(axis=(1, 3), dtype=np.uint16)
        win = n > best_n  # ties keep the lower class number (deterministic)
        best[win] = c
        best_n[win] = n[win]
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "data", "climate", "koppen-1991-2020.png")
    out = os.path.normpath(out)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    Image.fromarray(best, mode="L").save(out, optimize=True)
    land = int((best > 0).sum())
    print(f"wrote {out}: {ow} x {oh}, {land} land cells, {os.path.getsize(out) / 1e6:.2f} MB")
    # Spot checks the tests and the frames lean on.
    for name, lat, lon in [("Miami Beach", 25.78, -80.13), ("Deals Gap", 35.47, -83.93), ("Big Sur", 36.36, -121.9), ("Lincoln NE", 40.57, -96.59), ("Sydney", -33.87, 151.21)]:
        y = int((90.0 - lat) / 180.0 * oh)
        x = int((lon + 180.0) / 360.0 * ow)
        print(f"  {name}: class {best[y, x]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
