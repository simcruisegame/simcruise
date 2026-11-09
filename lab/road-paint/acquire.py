"""Fetch the small recorded official aerial windows and processed public street views into a working copy."""

import concurrent.futures
import hashlib
import io
import json
import math
from pathlib import Path
from urllib.request import Request, urlopen
from PIL import Image

WORLD_HALF = 20037508.342789244


def fetch(url, target, limit):
    if target.exists():
        return
    with urlopen(
        Request(url, headers={"User-Agent": "SundayDriver-road-paint-research/1.0"}),
        timeout=25,
    ) as response:
        data = response.read(limit + 1)
    if len(data) > limit:
        raise ValueError("Response exceeds the documented local request limit")
    Image.open(io.BytesIO(data)).verify()
    target.write_bytes(data)


def build_aerial(receipt):
    doc = json.loads(receipt.read_text())
    name = receipt.name.removesuffix("-receipt.json")
    folder = receipt.parent
    if "request" in doc:
        fetch(doc["request"], folder / (name + ".jpg"), 1024 * 1024)
        return
    urls = doc["requests"]
    tile_size = 256
    rows = []
    for url in urls:
        zoom, y, x = map(int, url.split("/")[-3:])
        rows.append((x, y, url, folder / f"{name}_{x}_{y}.jpg"))
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
        for job in [pool.submit(fetch, url, path, 200_000) for x, y, url, path in rows]:
            job.result()
    tx0 = min(row[0] for row in rows)
    tx1 = max(row[0] for row in rows)
    ty0 = min(row[1] for row in rows)
    ty1 = max(row[1] for row in rows)
    mosaic = Image.new(
        "RGB", ((tx1 - tx0 + 1) * tile_size, (ty1 - ty0 + 1) * tile_size)
    )
    for x, y, url, path in rows:
        mosaic.paste(Image.open(path), ((x - tx0) * tile_size, (y - ty0) * tile_size))
    lo, bottom, hi, top = doc["bounds"]
    resolution = 2 * WORLD_HALF / (tile_size * (1 << zoom))
    bounds = (
        (lo + WORLD_HALF) / resolution - tx0 * tile_size,
        (WORLD_HALF - top) / resolution - ty0 * tile_size,
        (hi + WORLD_HALF) / resolution - tx0 * tile_size,
        (WORLD_HALF - bottom) / resolution - ty0 * tile_size,
    )
    mosaic.transform(
        tuple(doc["size"]), Image.Transform.EXTENT, bounds, Image.Resampling.BILINEAR
    ).save(folder / (name + ".png"))


def frames(folder):
    for j, frame in enumerate(json.loads((folder / "frames.json").read_text())):
        original = folder / (frame["id"] + ".jpg")
        fetch(frame["thumbnail"], original, 1024 * 1024)
        if hashlib.sha256(original.read_bytes()).hexdigest() != frame["imageSHA256"]:
            raise ValueError(
                "The upstream processed photograph has changed: " + frame["id"]
            )
        Image.open(original).convert("RGB").resize((820, 615)).save(
            folder / f"photo-{j}.jpg", quality=94
        )


if __name__ == "__main__":
    for place in [Path("key"), Path("cambridge")]:
        for r in sorted(place.glob("*-receipt.json")):
            build_aerial(r)
        frames(place)
