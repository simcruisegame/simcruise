import json
from config import root as p, lat, lon, side, cx, cy, region, stem, data, cfg

rows = json.loads((p / "forest-rows.json").read_text())
bytile = {}
for e, n, ground, h, w, support, conifer, upper_width in rows:
    x, y = cx + e / side, cy - n / side
    tile = (int(x), int(y))
    bytile.setdefault(tile, []).append(
        [
            round((x % 1) * 65535),
            round((y % 1) * 65535),
            round(ground * 10),
            round(h * 10),
            round(w * 10),
            1 if conifer else 0,
            round(upper_width * 100),
        ]
    )
# Exact survey bounds and compact per-tile optical classifications. Store as a regional group for visibility beyond the near tile ring.
head = {
    "version": 1,
    "region": region,
    "tiles": [[*k, v] for k, v in sorted(bytile.items())],
    "center": [lat, lon],
    "source": cfg["workunit"] + "; USGS lidar, Meta/WRI CHMv2, LANDFIRE and NLCD",
    "records": len(rows),
}
(data / ("als-forest-" + stem + ".json")).write_text(
    json.dumps(head, separators=(",", ":"))
)
