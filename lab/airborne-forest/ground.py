"""Bounded airborne classified ground/water support. Missing cells remain transparent."""

import json
import numpy as np
from scipy.spatial import cKDTree
from PIL import Image, ImageDraw
from config import root as p, data, stem, side, cx, cy, aerials, swath

cloud = np.load(p / "forest-cloud.npz")["p"]
ground = cloud[cloud[:, 3] == 2]
water = cloud[cloud[:, 3] == 9]
kg = cKDTree(ground[:, :2])
kw = cKDTree(water[:, :2]) if len(water) >= 4 else None
directory = aerials
target = bytearray()
tiles = []
N = 68
yy, xx = np.indices((N + 1, N + 1))
uu, vv = xx / N, yy / N
report = []
for f in sorted((p / "vectors").glob("*.json")):
    obj = json.loads(f.read_text())
    tx, ty = obj["id"]
    E = (tx + uu - cx) * side
    NN = (cy - ty - vv) * side
    xy = np.stack((E, NN), axis=-1)
    roi = (E >= swath[0]) & (E <= swath[2]) & (NN >= swath[1]) & (NN <= swath[3])
    if not roi.any():
        continue
    gids, inds = kg.query(xy, k=7)
    z = np.median(ground[inds, 2], axis=-1)
    ok = (gids[:, :, -1] < 7) & roi
    # Cross-check every candidate water surface against the mapped boundary and multiple local classified-water returns.
    im = Image.new("1", (513, 513))
    draw = ImageDraw.Draw(im)
    for kind, _, rings in obj["land"]:
        if kind == "water":
            for ring in rings:
                draw.polygon(
                    [tuple(np.rint(np.array(pt) * 512).astype(int)) for pt in ring],
                    fill=1,
                )
    wm = np.asarray(im)[np.rint(vv * 512).astype(int), np.rint(uu * 512).astype(int)]
    water_ok = np.zeros_like(wm)
    if kw is not None:
        wd, wi = kw.query(xy, k=4)
        water_ok = (
            wm & roi & (wd[:, :, -1] < 70) & (np.ptp(water[wi, 2], axis=-1) < 0.36)
        )
        z[water_ok] = np.median(water[wi[water_ok], 2], axis=-1)
    ok = (ok & ~wm) | water_ok
    with Image.open(directory / f"a-{tx // 4}-{ty // 4}.jpg") as photo:
        rgb = np.array(photo.convert("RGB"))
        h, w = rgb.shape[:2]
        col = rgb[
            np.clip(((ty % 4 + vv) * h / 4).astype(int), 0, h - 1),
            np.clip(((tx % 4 + uu) * w / 4).astype(int), 0, w - 1),
        ]
    corners = np.stack([ok[:-1, :-1], ok[:-1, 1:], ok[1:, :-1], ok[1:, 1:]])
    heights = np.stack([z[:-1, :-1], z[:-1, 1:], z[1:, :-1], z[1:, 1:]])
    cells = np.all(corners, axis=0) & (np.ptp(heights, axis=0) <= 8.5)
    yi, xi = np.nonzero(cells)
    a = yi * (N + 1) + xi
    ind = (
        np.column_stack([a, a + 1, a + N + 2, a, a + N + 2, a + N + 1])
        .astype("uint32")
        .ravel()
    )
    if not len(ind):
        continue
    unq, inv = np.unique(ind, return_inverse=True)
    idx = inv.astype("<u2")
    if np.abs(z.ravel()[unq] * 10).max() >= 32768:
        raise ValueError("Survey ground exceeds signed-decimetre encoding")
    verts = np.rec.fromarrays(
        [
            np.rint(uu.ravel()[unq] * 65535).astype("<u2"),
            np.rint(vv.ravel()[unq] * 65535).astype("<u2"),
            np.rint(z.ravel()[unq] * 10).astype("<i2"),
            *col.reshape(-1, 3)[unq].T,
            np.int8(water_ok.ravel()[unq]),
        ],
        names="u,v,z,r,g,b,water",
    )
    offset = len(target)
    target.extend(verts.tobytes())
    target.extend(idx.tobytes())
    tiles.append([tx, ty, offset, len(unq), len(ind)])
    report.append(
        dict(
            tile=[tx, ty],
            vertices=len(unq),
            triangles=len(ind) // 3,
            water=int(water_ok.ravel()[unq].sum()),
        )
    )
(data / ("als-ground-" + stem + ".meshdata")).write_bytes(target)
j = json.loads((data / ("als-forest-" + stem + ".json")).read_text())
j["groundTiles"] = tiles
(data / ("als-forest-" + stem + ".json")).write_text(
    json.dumps(j, separators=(",", ":"))
)
(p / "ground-statistics.json").write_text(json.dumps(report, indent=2))
print(
    "ground bytes",
    len(target),
    "triangles",
    sum(r["triangles"] for r in report),
    "water vertices",
    sum(r["water"] for r in report),
)
