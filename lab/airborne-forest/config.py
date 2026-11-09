from pathlib import Path
import json, math, os

root = Path(os.environ["FOREST_WORK"]).resolve()
cfg = json.loads((root / "survey.json").read_text())
lat, lon = map(float, cfg["origin"])
if cfg["tileZoom"] != 16 or cfg["regionalZoom"] != 13:
    raise ValueError("unsupported map tile basis")
side = 40075016.68557849 / 65536 * math.cos(math.radians(lat))
cx = (lon + 180) / 360 * 65536
cy = (1 - math.asinh(math.tan(math.radians(lat))) / math.pi) * 32768
region = [int(cx) // 8, int(cy) // 8]
swath = cfg["swath"]
detail = cfg["detail"]
zrange = cfg["absoluteElevationWindow"]
source = cfg["ept"].rstrip("/") + "/"
if (
    not source.startswith("https://s3-us-west-2.amazonaws.com/usgs-lidar-public/")
    or ".." in source
):
    raise ValueError("invalid USGS source")
if not (
    swath[0] < swath[2]
    and swath[1] < swath[3]
    and (swath[2] - swath[0]) * (swath[3] - swath[1]) <= 1.6e6
):
    raise ValueError("survey area budget")
data = Path(cfg["gameDataDirectory"])
if not data.is_absolute():
    data = root / data
aerials = root / cfg["aerialDirectory"]
stem = "-".join(map(str, region))
