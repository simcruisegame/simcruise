"""Realism score: how close a game capture comes to a licensed photograph of the same spot.

The Godot taste test (lab/taste/2026-11-10) ranked the differences by eye. This puts a number on
the same comparison so a visual change can be measured like a physics change. Both pictures are
cut to the same aspect and scaled to 640 px wide, then compared on six things that a person sees
first, each mapped to 0-100 (100 = the photograph itself):

  layout     where the colours are: mean CIELAB difference (Delta E 76) of a 16 x 8 grid of cell means
  tone       the brightness distribution: earth mover's distance of the L* histograms
  colour     how colourful: Hasler-Suesstrunk colourfulness, game against photo
  structure  where the edges run and which way: gradient-orientation histograms per grid cell
  detail     how much fine texture: band-pass energy at three scales, game against photo
  sky        the sky's colour, from the top fifth of both frames (Delta E 76)

The total is a weighted mean (layout 25, structure 25, tone 15, colour 10, detail 15, sky 10).
It is a tracker, not an absolute grade: the pose, sun, weather, season, people and cars in a real
photograph never match exactly, so compare scores of the same pose across builds, not across poses.

usage: python lab/realism/score.py GAME.png PHOTO.jpg [--json]
       python lab/realism/score.py --poses lab/realism/poses.json --captures DIR --photos DIR [--history CSV]
"""
import argparse
import csv
import datetime
import json
import math
import sys
from pathlib import Path

import numpy as np
from PIL import Image

WIDTH = 640
GRID = (16, 8)
WEIGHTS = {"layout": 25, "structure": 25, "tone": 15, "colour": 10, "detail": 15, "sky": 10}
# Distance at which a part scores exp(-1) = 37. Measured on docs/images/unreal/round2-day-chase.jpg:
# the same frame blurred 2 px scores 91, shifted 2 % 90, half the saturation 84, 41 % brighter 51;
# the same view a round later 87, another view or the night 26-46.
SCALE = {"layout": 18.0, "tone": 8.0, "colour": 0.35, "structure": 0.35, "detail": 0.5, "sky": 15.0}


def load(path, aspect):
    """RGB float array in 0..1, centre-cropped to the aspect (width / height) and 640 px wide."""
    with Image.open(path) as image:
        image = image.convert("RGB")
        w, h = image.size
        if w / h > aspect:
            nw = round(h * aspect)
            image = image.crop(((w - nw) // 2, 0, (w - nw) // 2 + nw, h))
        else:
            nh = round(w / aspect)
            image = image.crop((0, (h - nh) // 2, w, (h - nh) // 2 + nh))
        image = image.resize((WIDTH, round(WIDTH / aspect)), Image.Resampling.LANCZOS)
        return np.asarray(image, dtype=np.float64) / 255.0


def to_lab(rgb):
    """sRGB (0..1) to CIELAB, D65."""
    linear = np.where(rgb <= 0.04045, rgb / 12.92, ((rgb + 0.055) / 1.055) ** 2.4)
    m = np.array([[0.4124564, 0.3575761, 0.1804375], [0.2126729, 0.7151522, 0.0721750], [0.0193339, 0.1191920, 0.9503041]])
    xyz = linear @ m.T / np.array([0.95047, 1.0, 1.08883])
    f = np.where(xyz > (6 / 29) ** 3, np.cbrt(xyz), xyz / (3 * (6 / 29) ** 2) + 4 / 29)
    return np.stack([116 * f[..., 1] - 16, 500 * (f[..., 0] - f[..., 1]), 200 * (f[..., 1] - f[..., 2])], axis=-1)


def cells(array, grid=GRID):
    """Mean of each grid cell: shape (rows, cols, channels)."""
    h, w = array.shape[:2]
    cols, rows = grid
    ys = np.linspace(0, h, rows + 1).astype(int)
    xs = np.linspace(0, w, cols + 1).astype(int)
    return np.array([[array[ys[r]:ys[r + 1], xs[c]:xs[c + 1]].reshape(-1, array.shape[-1]).mean(axis=0)
                      for c in range(cols)] for r in range(rows)])


def colourfulness(rgb):
    rg = rgb[..., 0] - rgb[..., 1]
    yb = 0.5 * (rgb[..., 0] + rgb[..., 1]) - rgb[..., 2]
    return math.hypot(rg.std(), yb.std()) + 0.3 * math.hypot(rg.mean(), yb.mean())


def blur(gray, radius):
    """Box blur by an integral image (no SciPy needed)."""
    k = 2 * radius + 1
    padded = np.pad(gray, radius + 1, mode="edge")
    s = padded.cumsum(0).cumsum(1)
    out = s[k:, k:] - s[:-k, k:] - s[k:, :-k] + s[:-k, :-k]
    return out[: gray.shape[0], : gray.shape[1]] / (k * k)


def orientation_histograms(gray, bins=8):
    gy, gx = np.gradient(gray)
    magnitude = np.hypot(gx, gy)
    angle = np.mod(np.arctan2(gy, gx), math.pi)
    index = np.minimum((angle / math.pi * bins).astype(int), bins - 1)
    planes = np.stack([magnitude * (index == b) for b in range(bins)], axis=-1)
    hist = cells(planes)
    return hist / (np.linalg.norm(hist, axis=-1, keepdims=True) + 1e-6), cells(magnitude[..., None])[..., 0]


def detail_energy(gray):
    """Band-pass energy at 1, 3 and 9 px: fine texture the eye reads as 'real'."""
    out = []
    for r in (1, 3, 9):
        band = blur(gray, r) - blur(gray, 3 * r)
        out.append(float(np.sqrt((band ** 2).mean())))
    return np.array(out)


def score_pair(game, photo, mask=None):
    """Parts and total for two RGB arrays of the same shape. mask (bool HxW) limits the pixel parts."""
    lab_g, lab_p = to_lab(game), to_lab(photo)
    parts, raw = {}, {}

    raw["layout"] = float(np.linalg.norm(cells(lab_g) - cells(lab_p), axis=-1).mean())

    lg, lp = (lab_g[..., 0], lab_p[..., 0]) if mask is None else (lab_g[..., 0][mask], lab_p[..., 0][mask])
    hg, _ = np.histogram(lg, bins=100, range=(0, 100))
    hp, _ = np.histogram(lp, bins=100, range=(0, 100))
    raw["tone"] = float(np.abs(np.cumsum(hg / hg.sum()) - np.cumsum(hp / hp.sum())).sum())

    cg, cp = colourfulness(game), colourfulness(photo)
    raw["colour"] = abs(math.log((cg + 0.01) / (cp + 0.01)))

    og, mg = orientation_histograms(lab_g[..., 0] / 100)
    op, mp = orientation_histograms(lab_p[..., 0] / 100)
    weight = (mg + mp) / ((mg + mp).sum() + 1e-9)
    raw["structure"] = float((weight * (1 - (og * op).sum(axis=-1))).sum())

    dg, dp = detail_energy(lab_g[..., 0] / 100), detail_energy(lab_p[..., 0] / 100)
    raw["detail"] = float(np.abs(np.log((dg + 1e-4) / (dp + 1e-4))).mean())

    top = max(1, game.shape[0] // 5)
    raw["sky"] = float(np.linalg.norm(lab_g[:top].reshape(-1, 3).mean(0) - lab_p[:top].reshape(-1, 3).mean(0)))

    for name, value in raw.items():
        parts[name] = round(100 * math.exp(-value / SCALE[name]), 1)
    total = sum(parts[n] * w for n, w in WEIGHTS.items()) / sum(WEIGHTS.values())
    return round(total, 1), parts, {k: round(v, 4) for k, v in raw.items()}


def score_files(game_path, photo_path, aspect=None):
    if aspect is None:
        with Image.open(photo_path) as image:
            aspect = image.size[0] / image.size[1]
    return score_pair(load(game_path, aspect), load(photo_path, aspect))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("game", nargs="?", type=Path)
    parser.add_argument("photo", nargs="?", type=Path)
    parser.add_argument("--json", action="store_true", help="print one JSON line")
    parser.add_argument("--poses", type=Path, help="poses.json: score every pose with a capture and a photo")
    parser.add_argument("--captures", type=Path, help="folder with realism-<pose>.png")
    parser.add_argument("--photos", type=Path, help="folder with <pose>.jpg reference photographs")
    parser.add_argument("--history", type=Path, help="append one row per pose to this CSV")
    parser.add_argument("--label", default="", help="build label for the history (for example the git commit)")
    args = parser.parse_args(argv)

    if args.poses:
        poses = json.loads(args.poses.read_text(encoding="utf-8-sig"))["poses"]  # PowerShell writes a BOM
        rows, failures = [], 0
        for pose in poses:
            game = args.captures / f"realism-{pose['name']}.png"
            photo = args.photos / f"{pose['name']}.jpg"
            if not game.exists() or not photo.exists():
                print(f"SKIP {pose['name']}: missing {'capture ' + str(game) if not game.exists() else 'photo ' + str(photo)}")
                failures += 1
                continue
            total, parts, raw = score_files(game, photo, pose["width"] / pose["height"])
            rows.append({"pose": pose["name"], "total": total, **parts})
            print(f"REALISM {pose['name']}: {total} / 100  " + "  ".join(f"{k} {v}" for k, v in parts.items()))
        if rows:
            mean = round(sum(r["total"] for r in rows) / len(rows), 1)
            print(f"REALISM mean over {len(rows)} poses: {mean} / 100")
        if args.history and rows:
            new = not args.history.exists()
            with args.history.open("a", newline="") as handle:
                writer = csv.DictWriter(handle, fieldnames=["time", "label", "pose", "total", *WEIGHTS])
                if new:
                    writer.writeheader()
                stamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
                for row in rows:
                    writer.writerow({"time": stamp, "label": args.label, **row})
        return 0 if rows and not failures else 1

    if not args.game or not args.photo:
        parser.error("give GAME and PHOTO, or --poses with --captures and --photos")
    total, parts, raw = score_files(args.game, args.photo)
    if args.json:
        print(json.dumps({"total": total, "parts": parts, "raw": raw}))
    else:
        print(f"REALISM {total} / 100")
        for name in WEIGHTS:
            print(f"  {name:9s} {parts[name]:5.1f}  (raw {raw[name]})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
