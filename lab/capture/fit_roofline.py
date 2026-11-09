"""Register a street-camera sequence against surveyed roof silhouettes.

This is an offline authoring/QA tool. Input paths and camera/scene assumptions are
declared in a JSON study manifest. The terrain cloud is a projected metric NPZ
with one array called 'p': easting, northing, elevation, LAS classification,
flight ID, return number, total returns. No model inference or HTTP runs here.
"""
import argparse
import json
import math
from pathlib import Path

import numpy as np
from PIL import Image
from pyproj import Transformer
from scipy.optimize import differential_evolution
from scipy.spatial import cKDTree
import shapely
from shapely.geometry import Polygon


def roof_and_ground(config, directory):
    cloud = np.load(directory / config["points"])["p"]
    geo = Transformer.from_crs(config["crs"], 4326, always_xy=True)
    lon, lat = geo.transform(cloud[:, 0], cloud[:, 1])
    tile = json.loads((directory / config["tile"]).read_text())
    x, y = tile["id"]
    u = (lon + 180) / 360 * 65536 - x
    v = (1 - np.arcsinh(np.tan(np.deg2rad(lat))) / np.pi) * 32768 - y
    selected = np.zeros(len(cloud), dtype=bool)
    for index in config["footprints"]:
        polygon = Polygon(tile["buildings"][index][0][0])
        selected |= shapely.contains_xy(polygon, u, v)
    selected &= np.isin(cloud[:, 3], config["roofClasses"]) & (cloud[:, 5] == 1)
    points = cloud[selected, :3].copy()
    if len(points) < 100:
        raise ValueError("Too few survey returns in the selected footprints")
    points[:, :2] -= config["origin"]
    points = points[np.argsort(-points[:, 2])]
    _, cells = np.unique(np.round(points[:, :2] * 1.2).astype(int), axis=0, return_index=True)
    ground = cloud[cloud[:, 3] == 2, :3]
    if len(ground) < 10:
        raise ValueError("Terrain requires classified ground returns")
    return points[cells], ground


def read_frame(frame, config, directory, terrain):
    ground, tree = terrain
    position = np.array(Transformer.from_crs(4326, config["crs"], always_xy=True).transform(frame["lon"], frame["lat"]))
    elevation = np.mean(ground[tree.query(position, k=10)[1], 2]) + config["cameraHeightM"]
    labels = np.asarray(Image.open(directory / frame["labels"]))
    height, width = labels.shape
    columns = config["roiFraction"]
    roof = []
    for x in range(math.ceil(columns[0] * width), int(columns[1] * width), 2):
        column = labels[:, x]
        crossings = np.flatnonzero((column[:-3] == config["skyLabel"]) & np.isin(column[3:], config["wallLabels"]))
        if len(crossings) and 28 < crossings[0] < height * .62:
            roof.append((x, crossings[0] + 2))
    if len(roof) < 15:
        raise ValueError(f'{frame["frame"]} has insufficient clean sky/building boundary')
    return position - config["origin"], elevation, np.array(roof), (height, width), frame


def prediction(points, frame, model):
    fov, yaw, pitch, east, north = model
    xy, elevation, observed, (height, width), photo = frame
    bearing = math.radians(photo["heading"] + yaw)
    dx, dy = (points[:, :2] - xy - [east, north]).T
    depth = math.sin(bearing) * dx + math.cos(bearing) * dy
    side = math.cos(bearing) * dx - math.sin(bearing) * dy
    rise = points[:, 2] - elevation
    angle = math.radians(pitch)
    distance = depth * math.cos(angle) - rise * math.sin(angle)
    up = rise * math.cos(angle) + depth * math.sin(angle)
    focal = width / (2 * math.tan(math.radians(fov / 2)))
    x = width * .5 + focal * side / np.maximum(distance, .1)
    y = height * .5 - focal * up / np.maximum(distance, .1)
    valid = (distance > 4) & (x > 0) & (x < width) & (y < height * .78)
    bins = np.full(math.ceil(width / 3) + 5, np.inf)
    indices = np.round(x[valid] / 3).astype(int)
    np.minimum.at(bins, indices, y[valid])
    occupied = np.flatnonzero(np.isfinite(bins))
    if len(occupied) < 2:
        return 80., None
    predicted = np.interp(observed[:, 0], occupied * 3, bins[occupied], left=np.nan, right=np.nan)
    used = np.isfinite(predicted)
    if used.sum() < 15:
        return 80., None
    error = np.abs(predicted[used] - observed[used, 1])
    # Missing data cannot improve the score by simply moving the roof out of view.
    penalty = max(0., .7 - used.mean()) * 22
    return float(np.minimum(error, 35).mean() + penalty), float(error.mean())


def solve(manifest, destination):
    base = manifest.parent
    config = json.loads(manifest.read_text())
    points, ground = roof_and_ground(config, base)
    tree = cKDTree(ground[:, :2])
    frames = [read_frame(row, config, base, (ground, tree)) for row in config["frames"]]
    training = [fr for fr in frames if fr[-1]["use"] == "train"]
    held = [fr for fr in frames if fr[-1]["use"] == "holdout"]
    if len(training) < 2 or not held:
        raise ValueError("Use at least two training views and one separate held-out view")
    default = [config["initialFovH"], 0., 0., 0., 0.]
    def loss(p):
        scores = [prediction(points, fr, p)[0] for fr in training]
        return np.mean(scores) + .011*(p[0]-default[0])**2 + .08*p[1]**2 + .06*p[2]**2 + .045*(p[3]**2+p[4]**2)
    found = differential_evolution(loss, [(67, 100), (-10, 10), (-10, 10), (-10, 10), (-10, 10)],
                                   seed=1999, maxiter=35, popsize=7, tol=.006, polish=True)
    results = []
    for fr in frames:
        before, raw_before = prediction(points, fr, default)
        after, raw_after = prediction(points, fr, found.x)
        results.append(dict(frame=fr[-1]["frame"], use=fr[-1]["use"], usableColumns=len(fr[2]),
                            penalizedBefore=before, penalizedAfter=after, meanAbsoluteBefore=raw_before,
                            meanAbsoluteAfter=raw_after))
    record = dict(cameraParameters=dict(zip(["horizontalFovDeg","yawOffsetDeg","pitchDownDeg","eastOffsetM","northOffsetM"],found.x)),
                  views=results, pointCRS=config["crs"], cloudSource=config["cloudSource"], attribution=config["attribution"])
    destination.write_text(json.dumps(record, indent=2) + "\n")


if __name__ == "__main__":
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("manifest", type=Path)
    cli.add_argument("destination", type=Path)
    args = cli.parse_args()
    solve(args.manifest, args.destination)
