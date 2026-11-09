import numpy as np, json
from PIL import Image, ImageDraw
from scipy.spatial import cKDTree
from collections import Counter
from config import root as p, data, stem, side, cx, cy, aerials

d = data / ("als-forest-" + stem + ".json")
doc = json.loads(d.read_text())
raw = np.load(p / "forest-cloud.npz")["p"]
water = raw[raw[:, 3] == 9]
kw = cKDTree(water[:, :2])
stats = Counter()
colors = []
directory = aerials
for xt, yt, rows in doc["tiles"]:
    src = json.loads((p / f"vectors/{xt}-{yt}.json").read_text())
    mask = Image.new("1", (1024, 1024))
    draw = ImageDraw.Draw(mask)

    def xy(q):
        return tuple(np.round(np.asarray(q) * 1024).astype(int))

    for road in src["roads"]:
        if len(road) > 4:
            draw.line(
                [xy(q) for q in road[3]],
                fill=1,
                width=round((road[4] + 0.5) / side * 1024),
            )
    for b in src["buildings"]:
        for ring in b[0]:
            draw.polygon([xy(q) for q in ring], fill=1)
    for typ, sub, rings in src["land"]:
        if typ == "water":
            for ring in rings:
                draw.polygon([xy(q) for q in ring], fill=1)
    with Image.open(directory / f"a-{xt // 4}-{yt // 4}.jpg") as photo:
        rgb = np.array(photo.convert("RGB"))
        py, px = rgb.shape[:2]
    keep = []
    for row in rows:
        u, v = row[0] / 65535, row[1] / 65535
        x = (xt + u - cx) * side
        y = (cy - yt - v) * side
        if (
            mask.getpixel((min(1023, int(u * 1024)), min(1023, int(v * 1024))))
            or kw.query([x, y])[0] < 2.5
        ):
            stats["blocked_road_water_building"] += 1
            continue
        i = min(px - 1, int((xt % 4 + u) * px / 4))
        j = min(py - 1, int((yt % 4 + v) * py / 4))
        radius = max(1, min(3, round(row[4] * 0.1 / 2 * px / (side * 4))))
        sample = rgb[
            max(0, j - radius) : j + radius + 1, max(0, i - radius) : i + radius + 1
        ].reshape(-1, 3)
        # Upper half of foliage RGB samples avoids baking very dark cast shadows twice.
        lum = sample @ [0.21, 0.72, 0.07]
        seed = sample[lum >= np.quantile(lum, 0.35)]
        color = np.median(seed, axis=0)
        colors.append(color.tolist())
        keep.append(row + list(map(int, color)))
    rows[:] = keep
doc["records"] = sum(len(v[2]) for v in doc["tiles"])
d.write_text(json.dumps(doc, separators=(",", ":")))
print(stats, doc["records"], np.percentile(colors, [10, 50, 90], axis=0))
