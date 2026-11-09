"""Compare a ground-truth street photograph and paired renders in the Charleston study ROIs."""

import argparse
import json

import numpy as np
from PIL import Image
from scipy.ndimage import label, median_filter


def load_image(path, size, labels=False):
    im = Image.open(path)
    if labels:
        return np.asarray(im.resize(size, Image.Resampling.NEAREST))
    return np.asarray(im.convert("RGB").resize(size), dtype=float)


def skyline(mask):
    connected, _ = label(mask)
    top = np.unique(connected[0, :])
    sky = np.isin(connected, top[top != 0])
    return median_filter(np.argmax(~sky, axis=0), size=5)


def score(reference, mask, original, updated):
    labels = load_image(mask, (640, 480), labels=True)
    truth = skyline(labels == 3)
    column = np.arange(640)
    valid = (column >= 12) & (column <= 265) & (truth > 40) & (truth < 310)
    small = (513, 385)
    photo = load_image(reference, small)
    material = load_image(mask, small, labels=True)
    y, x = np.mgrid[: small[1], : small[0]]
    region = (x >= 5) & (x < 226) & (y > 50) & (y < 197)
    results = {}
    for name, path in (("control", original), ("candidate", updated)):
        red, green, blue = (load_image(path, (640, 480)) / 255.0).transpose(2, 0, 1)
        sky = (blue > green * 1.032) & (green > red * 1.095) & (blue - red > 0.105)
        error = skyline(sky)[valid] - truth[valid]
        difference = abs(load_image(path, small) - photo)
        results[name] = dict(
            columns=int(valid.sum()),
            skyline_mae=float(np.mean(abs(error))),
            skyline_p90=float(np.percentile(abs(error), 90)),
            building_rgb_mae=float(difference[(material == 2) & region].mean()),
            sky_rgb_mae=float(difference[(material == 3) & region].mean()),
            region_rgb_mae=float(difference[region].mean()),
        )
    return results


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ("reference", "mask", "original", "updated"):
        parser.add_argument(key)
    args = parser.parse_args()
    print(json.dumps(score(**vars(args)), indent=2))
