"""Reconstruct the pre-2023 glazing fraction with separately licensed views."""
import argparse
import json
import urllib.request
from pathlib import Path

import numpy as np
from PIL import Image
from scipy.ndimage import gaussian_filter1d, map_coordinates, median_filter, percentile_filter

HERE = Path(__file__).resolve().parent


def photograph(record, cache):
    path = cache / (record["id"] + ".jpg")
    if not path.exists():
        req = urllib.request.Request(record["pixel_url"], headers={
            "User-Agent": "SundayDriver-HistoricalMaterial/1.0 (five documented files)"
        })
        with urllib.request.urlopen(req, timeout=20) as response:
            data = response.read(3_000_001)
        if len(data) > 3_000_000:
            raise ValueError("Unexpected image size")
        # Verify the capped response before storing it, including on retries.
        from io import BytesIO
        with Image.open(BytesIO(data)) as decoded:
            decoded.verify()
        path.write_bytes(data)
    with Image.open(path) as im:
        return np.asarray(im.convert("RGB").resize(record["source_size"], Image.Resampling.LANCZOS))


def rectified_profile(image, corners):
    # The four image points are ordered TL, TR, BR, BL. Solve the homography
    # once, then sample only the interior of this one observable wall plane.
    system, values = [], []
    for (u, v), (x, y) in zip(((0, 0), (1, 0), (1, 1), (0, 1)), corners):
        system.extend(((u,v,1,0,0,0,-x*u,-x*v), (0,0,0,u,v,1,-y*u,-y*v)))
        values.extend((x, y))
    transform = np.r_[np.linalg.solve(system, values), 1.0].reshape(3, 3)
    u, v = np.meshgrid((np.arange(160) + .5) / 160, (np.arange(400) + .5) / 400)
    points = transform @ np.stack((u.ravel(), v.ravel(), np.ones(u.size)))
    x, y = points[:2] / points[2]
    # Keep the source's 8-bit bilinear interpolation for reproducibility.
    rgb = np.stack([map_coordinates(image[:, :, i], (y, x), order=1).reshape(v.shape)
                    for i in range(3)], axis=2)
    intensity = rgb @ np.array([.2126, .7152, .0722])
    return gaussian_filter1d(np.median(intensity[:, 10:-10], axis=1), 2.0)


def bands(profile):
    bright = percentile_filter(profile, 85, 151, mode="nearest")
    dim = percentile_filter(profile, 15, 151, mode="nearest")
    dark = median_filter((profile < (bright + dim) * .5).astype(int), 13, mode="nearest") > 0
    starts = np.flatnonzero(dark[1:] & ~dark[:-1]) + 1
    ends = np.flatnonzero(~dark[1:] & dark[:-1]) + 1
    fractions, pitches = [], []
    for start, next_start in zip(starts, starts[1:]):
        end = ends[(ends > start) & (ends < next_start)]
        if len(end) != 1 or min(end[0] - start, next_start - end[0]) < 12:
            continue
        contrast = np.median(profile[end[0]:next_start]) - np.median(profile[start:end[0]])
        if contrast < 24:
            continue
        fractions.append((end[0] - start) / (next_start - start))
        pitches.append(next_start - start)
    if len(fractions) < 2 or np.std(pitches) / np.median(pitches) >= .14:
        raise ValueError("No reliable repeating light and dark walls in this crop")
    return {"glazing_fraction": round(float(np.median(fractions)), 3),
            "normalized_pitch_px": float(np.median(pitches)), "cycles": len(fractions)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("cache", type=Path)
    args = parser.parse_args()
    args.cache.mkdir(parents=True, exist_ok=True)
    records = json.loads((HERE / "sources.json").read_text())
    measurements = {}
    for record in records:
        image = photograph(record, args.cache)
        measurements[record["id"]] = bands(rectified_profile(image, record["crop"]))
    source_fractions = [measurements[record["id"]]["glazing_fraction"] for record in records[:3]]
    # The 2009 short north wing spans normalized map x 0.67480..0.69434, or
    # 5.75 m at this latitude. Its 67 px width fixes scale in this plane.
    map_width = .01954 * (40075016.68558 / 65536) * np.cos(np.radians(61.21768))
    scale = map_width / 67.0
    measurements["material"] = {
        "glazing_fraction": float(np.median(source_fractions)),
        "pitch_m": float(measurements["96140231"]["normalized_pitch_px"] * 269 / 400 * scale),
        "eave_cap_m": float((490 - 471) * scale),
    }
    print(json.dumps(measurements, indent=2))


if __name__ == "__main__":
    main()
