"""Clip independently supported paint to z16 driving tiles."""
import argparse
import json
import math
from collections import defaultdict
from pathlib import Path


def tile_xy(location):
    lon, lat = location
    return (lon + 180) / 360 * 65536, (1 - math.asinh(math.tan(math.radians(lat))) / math.pi) * 32768


def clip(a, b, x, y, margin):
    low, high = 0.0, 1.0
    dx, dy = b[0] - a[0], b[1] - a[1]
    for p, q in ((-dx, a[0]-x+margin), (dx, x+1+margin-a[0]), (-dy, a[1]-y+margin), (dy, y+1+margin-a[1])):
        if abs(p) < 1e-10:
            if q < 0:
                return None
            continue
        if p < 0:
            low = max(low, q/p)
        else:
            high = min(high, q/p)
    if low >= high:
        return None
    return [[round(a[0] + t*dx - x, 7), round(a[1] + t*dy - y, 7)] for t in (low, high)]


def add_line(tiles, start, end, style=None):
    margin = 0 if style else .025
    for x in range(math.floor(min(start[0], end[0])-.025), math.floor(max(start[0], end[0])+.025)+1):
        for y in range(math.floor(min(start[1], end[1])-.025), math.floor(max(start[1], end[1])+.025)+1):
            points = clip(start, end, x, y, margin)
            if points:
                if style:
                    tiles[x,y]["strokes"].append([*points, style])
                else:
                    tiles[x,y]["bounds"].append(points)


def bake(tiles, record):
    axis = [tile_xy(p) for p in record["axis"]]
    side = 40075016.68557849 / 65536 * math.cos(math.radians(record["axis"][0][1]))
    spans = []
    total = 0.0
    for a, b in zip(axis, axis[1:]):
        length = math.dist(a, b) * side
        if length < .01:
            continue
        spans.append((total, total + length, a, b, length))
        total += length
        add_line(tiles, a, b)

    def stroke(start, end, offset, style):
        for low, high, a, b, length in spans:
            u, v = max(start, low), min(end, high)
            if v - u < .01:
                continue
            dx, dy = (b[0] - a[0]) / length, (b[1] - a[1]) / length
            p = (a[0] + dx*(u-low) - dy*offset, a[1] + dy*(u-low) + dx*offset)
            q = (a[0] + dx*(v-low) - dy*offset, a[1] + dy*(v-low) + dx*offset)
            # Geometry follows the existing road elevation at least every six metres.
            steps = math.ceil((v-u)/6)
            for step in range(steps):
                a0,b0 = (tuple(p[j]+(q[j]-p[j])*t for j in (0,1)) for t in (step/steps, (step+1)/steps))
                add_line(tiles, a0, b0, style)

    left, right = record["edgeOffsets"]
    center = (left + right) / 2
    for offset in (left, right):
        stroke(0, total, offset, "whitepaint")
    if record["center"] == "double-solid-yellow":
        for offset in (center-.11, center+.11):
            stroke(0, total, offset, "yellowpaint")
    elif record["center"] == "broken-yellow":
        period = record["period"]
        position = record["onset"] - period
        while position < total:
            stroke(max(0,position), min(total,position+period*.25), center, "yellowpaint")
            position += period
    else:
        raise ValueError("Unknown paint layout")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("measurements", nargs="+", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    tiles = defaultdict(lambda: {"bounds": [], "strokes": []})
    for path in args.measurements:
        for record in json.loads(path.read_text()):
            bake(tiles, record)
    args.output.mkdir(parents=True, exist_ok=True)
    for (x,y), data in tiles.items():
        doc = {"version": 1, "tile": [x,y], **data}
        path = args.output / f"road-survey-{x}-{y}.json"
        path.write_text(json.dumps(doc, separators=(",",":"))+"\n")


if __name__ == "__main__":
    main()
