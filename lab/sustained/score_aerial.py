"""Compare matched engine pixels at road ray intersections with the same public aerial texels."""

import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image


def score(control, candidate, cache):
    imagery = {}
    rows = {}
    for rays in sorted(candidate.glob("*.jpg.rays.json")):
        name = rays.name.removesuffix(".rays.json")
        before = np.asarray(Image.open(control / name))
        after = np.asarray(Image.open(candidate / name))
        samples = []
        for x, y, tx, ty, u, v, *_ in json.loads(rays.read_text()):
            key = (tx // 4, ty // 4)
            if key not in imagery:
                imagery[key] = np.asarray(Image.open(cache / ("a-%d-%d.jpg" % key)))
            aerial = imagery[key]
            px = round((tx % 4 + u) * aerial.shape[1] / 4 - 0.5)
            py = round((ty % 4 + v) * aerial.shape[0] / 4 - 0.5)
            samples.append([aerial[py, px], before[y, x], after[y, x]])
        if not samples:
            continue
        source, prior, revised = np.asarray(samples, dtype=float).transpose(1, 0, 2)
        rows[name] = {
            "ray_count": len(samples),
            "aerial_median_srgb": np.rint(np.median(source, axis=0)).tolist(),
            "control_median_srgb": np.median(prior, axis=0).tolist(),
            "candidate_median_srgb": np.median(revised, axis=0).tolist(),
            "control_mean_abs_srgb_error": round(float(abs(prior - source).mean()), 2),
            "candidate_mean_abs_srgb_error": round(
                float(abs(revised - source).mean()), 2
            ),
            "control_median_abs_srgb_error": round(
                float(np.median(abs(prior - source))), 2
            ),
            "candidate_median_abs_srgb_error": round(
                float(np.median(abs(revised - source))), 2
            ),
        }
    return rows


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ["control", "candidate", "cache"]:
        parser.add_argument(key, type=Path)
    args = parser.parse_args()
    print(json.dumps(score(args.control, args.candidate, args.cache), indent=2))
