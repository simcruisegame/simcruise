import argparse
from pathlib import Path
import json
import numpy as np
from geometry import ground, plan, projection, r, st
from PIL import Image, ImageDraw
from scipy.ndimage import binary_erosion, sobel, gaussian_filter


def load(p):
    return np.asarray(Image.open(p).convert("RGB"), float)


parser = argparse.ArgumentParser(description="Score lossless captures from --bethel-facade-ab.")
parser.add_argument("frames", type=Path)
parser.add_argument("--photo", type=Path, default=r / "davidson-original.jpg")
args = parser.parse_args()
frames = args.frames
ctl = load(frames / "bethel-registered-stock.png")
cand = load(frames / "bethel-registered-survey.png")
ih, iw = ctl.shape[:2]
ref = np.asarray(
    Image.open(args.photo).convert("RGB").resize((iw, ih), Image.Resampling.LANCZOS),
    float,
)
# Use a conservative building-only mask eroded away from silhouettes. Game trees can still
# occlude the selected wall; include their error as part of the result.
labels = np.asarray(Image.open(r / "labels.png").resize((iw, ih), Image.Resampling.NEAREST))
classes = json.loads((r / "label-ids.json").read_text())
mask = np.isin(
    labels, [classes.index("building"), classes.index("wall"), classes.index("windowpane")]
)
mask = binary_erosion(mask, iterations=5)
roi = Image.new("L", (iw, ih))
dr = ImageDraw.Draw(roi)
for a, b in [(0, 7), (7, 6), (6, 5), (5, 4)]:
    floor = np.array([ground(plan[a]), ground(plan[b])])
    ends = plan[[a, b]]
    quad = np.r_[np.c_[ends, np.full(2, st[5])], np.c_[ends[::-1], floor[::-1]]]
    pt = projection(quad, st) * iw / 640
    dr.polygon([tuple(v) for v in pt], fill=255)
mask &= np.asarray(roi) > 0


def mae(A):
    return round(float(np.abs(A - ref)[mask].mean()), 3)


def lumin(A):
    return A @ [0.2126, 0.7152, 0.0722]


def edge(A):
    a = gaussian_filter(lumin(A), 1.0)
    return np.hypot(sobel(a, 0), sobel(a, 1))


ed = edge(ref)


def corr(A):
    return round(float(np.corrcoef(ed[mask], edge(A)[mask])[0, 1]), 3)


def channel(A):
    return [round(float(v), 1) for v in A[mask].mean(0)]


scores = dict(
    source_mask_pixels=int(mask.sum()),
    rgb_mae_255={"stock": mae(ctl), "survey": mae(cand)},
    photo_edges_correlation={"stock": corr(ctl), "survey": corr(cand)},
    colors={"photo": channel(ref), "stock": channel(ctl), "survey": channel(cand)},
    views={},
)
for mode in ["registered", "church-street"]:
    a = load(frames / f"bethel-{mode}-stock.png")
    b = load(frames / f"bethel-{mode}-stock-repeat.png")
    c = load(frames / f"bethel-{mode}-survey.png")
    diff = np.abs(c - a).max(2) >= 3
    scores["views"][mode] = {
        "repeat_control_different_pixels": int(np.any(a != b, 2).sum()),
        "changed_pixels_at_least_three_codes": int(diff.sum()),
    }
print(json.dumps(scores, indent=2))
(frames / "scores.json").write_text(json.dumps(scores, indent=2))
