"""Acquire two bounded strata of a USGS EPT forest survey around a WGS84 camera position."""

import math, json, urllib.request, io, hashlib
from concurrent.futures import ThreadPoolExecutor
from collections import Counter
import numpy as np, laspy
from config import root, cfg, lat, lon, source, swath, detail, zrange

cache = root / "source"
cache.mkdir(exist_ok=True)
base = source
cos = math.cos(math.radians(lat))
R = 6378137.0
eye = np.array([R * math.radians(lon), R * math.asinh(math.tan(math.radians(lat)))])


def fetch(path, limit):
    disk = cache / path.replace("/", "__")
    if not disk.exists():
        with urllib.request.urlopen(base + path, timeout=14) as r:
            b = r.read(limit + 1)
        if len(b) > limit:
            raise ValueError("Response byte cap " + path)
        disk.write_bytes(b)
    return disk.read_bytes()


meta = json.loads(fetch("ept.json", 100000))
if meta.get("srs", {}).get("horizontal") != "3857" or meta.get("dataType") != "laszip":
    raise ValueError("Require EPSG:3857 LAZ EPT")
lo = np.array(meta["bounds"][:3])
span = meta["bounds"][3] - lo[0]


def box(key):
    depth, *idx = map(int, key.split("-"))
    w = span / 2**depth
    xyz = lo + np.array(idx) * w
    return depth, *(tuple((xyz[:2] - eye) * cos)), xyz[2], w * cos, w


def hit(key, roi):
    d, e, n, z, w, h = box(key)
    x0, y0, x1, y1 = roi
    return (
        e < x1
        and e + w > x0
        and n < y1
        and n + w > y0
        and z < zrange[1]
        and z + h > zrange[0]
    )


all_extent = swath
near = detail
roots = ["0-0-0-0"]
seen = set()
keys = {}
while roots:
    nxt = set()
    for r in roots:
        if r in seen:
            continue
        seen.add(r)
        for k, count in json.loads(
            fetch("ept-hierarchy/" + r + ".json", 800000)
        ).items():
            depth = int(k.split("-")[0])
            roi = all_extent if depth <= max(cfg["cubeDepths"]["broad"]) else near
            if depth > max(cfg["cubeDepths"]["detail"]) or not hit(k, roi):
                continue
            if count < 0:
                nxt.add(k)
            elif (depth in cfg["cubeDepths"]["broad"] and hit(k, all_extent)) or (
                depth in cfg["cubeDepths"]["detail"] and hit(k, near)
            ):
                keys[k] = count
    roots = list(nxt)
print(
    "Potential nodes",
    Counter(k.split("-")[0] for k in keys),
    sum(keys.values()),
    flush=True,
)
# Keep two complementary levels for the whole swath and tighter detail for close camera terrain.
# A hard upper bound protects the source when regional node density changes.
if len(keys) > 90 or sum(keys.values()) > 4500000:
    raise ValueError("Source area too large")


def read(k):
    b = fetch("ept-data/" + k + ".laz", 1900000)
    las = laspy.read(io.BytesIO(b))
    e = (np.asarray(las.x) - eye[0]) * cos
    n = (np.asarray(las.y) - eye[1]) * cos
    depth = int(k.split("-")[0])
    x0, y0, x1, y1 = all_extent if depth <= max(cfg["cubeDepths"]["broad"]) else near
    take = (e > x0) & (e < x1) & (n > y0) & (n < y1)
    return np.column_stack(
        [
            e[take],
            n[take],
            np.asarray(las.z)[take],
            np.asarray(las.classification)[take],
            np.asarray(las.point_source_id)[take],
            np.asarray(las.return_number)[take],
            np.asarray(las.number_of_returns)[take],
        ]
    ).astype("float32"), dict(
        key=k, depth=depth, bytes=len(b), hash=hashlib.sha256(b).hexdigest()
    )


with ThreadPoolExecutor(3) as worker:
    out = list(worker.map(read, sorted(keys)))
p = np.vstack([v for v, _ in out])
print(
    "bytes",
    sum(d["bytes"] for _, d in out),
    "clipped points",
    len(p),
    "class",
    np.unique(p[:, 3], return_counts=True),
    "flight",
    np.unique(p[:, 4], return_counts=True),
    "z",
    np.percentile(p[:, 2], [0, 10, 50, 90, 100]),
    flush=True,
)
np.savez_compressed(root / "forest-cloud.npz", p=p)
(root / "manifest.json").write_text(
    json.dumps(
        dict(
            source=base,
            center=[lat, lon],
            near=near,
            swath=all_extent,
            nodes=[d for _, d in out],
        ),
        indent=2,
    )
)
