"""Check road-relative relief against a registered 1 m source raster at held-out XY.

python validate.py baseline-tile.json measured-tile.json source-1m.tif [previous-tile.json]
"""
import argparse
import json
import math
from pathlib import Path

import numpy as np
from pyproj import Transformer
import rasterio
from scipy.ndimage import map_coordinates
from scipy.spatial import cKDTree


def triangle_at(mesh, points):
    size = len(mesh) - 1
    pos = np.clip(points, 0, 1) * size
    index = np.minimum(size - 1, pos.astype(int))
    x, y = index.T
    fx, fy = (pos - index).T
    a, b = mesh[y, x], mesh[y, x + 1]
    c, d = mesh[y + 1, x], mesh[y + 1, x + 1]
    return np.where(
        fx + fy <= 1,
        a + (b - a) * fx + (c - a) * fy,
        d + (c - d) * (1 - fx) + (b - d) * (1 - fy),
    )


def sample_lines(roads, side):
    centers, protected = [], []
    majors = {"primary", "secondary", "tertiary", "motorway", "trunk"}
    for road in roads:
        coords = np.asarray(road[3]) * side
        segments = np.diff(coords, axis=0)
        lengths = np.linalg.norm(segments, axis=1)
        if lengths.sum() < .001:
            continue
        distance = np.r_[0., np.cumsum(lengths)]
        samples = np.linspace(0, distance[-1], math.ceil(distance[-1] / .2) + 1)
        points = np.c_[np.interp(samples, distance, coords[:, 0]),
                       np.interp(samples, distance, coords[:, 1])]
        protected.append(np.c_[points, np.full(len(points), road[4] / 2)])
        if road[0] in majors:
            centers.append(points)
    return np.vstack(centers), np.vstack(protected)


def compare(base, enriched, raster_path, previous):
    half_earth = 20037508.342789244
    tile_step = 2 * half_earth / 65536
    x, y = base["id"]
    if base["id"] != enriched["id"] or base["h"] != enriched["h"]:
        raise ValueError("Tile IDs and baseline heights must match")
    side = tile_step / math.cosh(math.pi * (1 - 2 * (y + .5) / 65536))
    centers, protected = sample_lines(base["roads"], side)
    posts = np.indices((129, 129))[::-1].reshape(2, -1).T / 128
    old = np.asarray(base["h"]).reshape(65, 65)
    fine = triangle_at(old, posts)
    deltas = np.asarray(enriched["road_banks"]["deltas"]).reshape(-1, 2)
    fine[deltas[:, 0].astype(int)] += deltas[:, 1] * .001
    fine = fine.reshape(129, 129)
    points = np.random.default_rng(19990515).uniform(0, side, (13000, 2))
    distance, near = cKDTree(centers).query(points)
    clear, road_index = cKDTree(protected[:, :2]).query(points)
    ref = centers[near]
    with rasterio.open(raster_path) as raster:
        project = Transformer.from_crs(3857, raster.crs, always_xy=True)
        native = raster.read(1, masked=True).astype(float).filled(np.nan)

        def ground(position):
            mx = (x + position[:, 0] / side) * tile_step - half_earth
            my = half_earth - (y + position[:, 1] / side) * tile_step
            cc, rr = ~raster.transform * project.transform(mx, my)
            return map_coordinates(native, [rr - .5, cc - .5], order=1, cval=np.nan)

        direct = ground(points) - ground(ref)
    meshes = [old, np.asarray(previous["h"]).reshape(65, 65), fine]
    differences = np.array([triangle_at(mesh, points / side) - triangle_at(mesh, ref / side)
                            for mesh in meshes])
    observed = np.isfinite(direct) & (abs(direct) < 9) & (distance >= 10) & (distance < 43)
    results = {}
    for name, mask in [("all", observed), ("changed", observed & (abs(differences[2] - differences[0]) > .06))]:
        errors = abs(differences[:, mask] - direct[mask])
        results[name] = {"samples": int(mask.sum()), "meanAbsoluteMeters": errors.mean(1).tolist(),
                         "p90Meters": np.percentile(errors, 90, axis=1).tolist()}
    wheels = clear <= protected[road_index, 2]
    displacement = triangle_at(fine, points[wheels] / side) - triangle_at(old, points[wheels] / side)
    results["wheelSamples"] = int(wheels.sum())
    results["maxWheelDeltaMeters"] = float(np.abs(displacement).max())
    return results


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for arg in ("baseline", "enriched", "source"):
        parser.add_argument(arg, type=Path)
    parser.add_argument("previous", nargs="?", type=Path)
    args = parser.parse_args()
    base = json.loads(args.baseline.read_text())
    prev = json.loads(args.previous.read_text()) if args.previous else base
    print(json.dumps(compare(base, json.loads(args.enriched.read_text()), args.source, prev), indent=2))
