"""Score a rain-free model change or other ground-cover A/B at the documented poses."""
import argparse
from pathlib import Path
import json
import numpy as np
from PIL import Image, ImageDraw

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("site", choices=("bethel", "lewisburg"))
parser.add_argument("original", type=Path, help="1280 px licensed source photograph")
parser.add_argument("prefix", type=Path, help="Godot --ground-cover-ab file prefix")
args = parser.parse_args()

def load(path):
    with Image.open(path) as image:
        return np.asarray(image.convert("RGB").resize((640, 480), Image.Resampling.LANCZOS), dtype=float)

def frame(state):
    return Path(str(args.prefix) + "-" + state + ".png")

def polygon(points):
    mask = Image.new("L", (640, 480))
    ImageDraw.Draw(mask).polygon(points, fill=255)
    return np.asarray(mask) > 0

with Image.open(frame("off")) as first, Image.open(frame("off-repeat")) as repeat:
    if first.size != repeat.size or first.tobytes() != repeat.tobytes():
        raise SystemExit("The repeated control frame changed; freeze the scene and recapture.")
photo, off, on = (load(p) for p in (args.original, frame("off"), frame("on")))
y, x = np.indices((480, 640))
if args.site == "bethel":
    green = (y >= 318) & (photo[:,:,1] > 1.11*photo[:,:,0]) & (photo[:,:,1] > 1.30*photo[:,:,2]) & (photo[:,:,1] > 25)
    masks = {"green_ground": green, "near_grass": green & (y >= 405),
             "paved_access": polygon([(560,340),(603,338),(627,354),(638,362),(638,420),(598,417),(547,395),(512,375),(501,367)])}
else:
    masks = {"motor_road": polygon([(8,420),(241,370),(590,313),(636,311),(639,479),(0,479)])}
for name, mask in masks.items():
    error = [round(float(np.abs(model[mask]-photo[mask]).mean()), 3) for model in (off, on)]
    changed = int((np.abs(off[mask]-on[mask]).max(axis=1) >= 3).sum())
    print(json.dumps({"region": name, "pixels": int(mask.sum()), "rgb_mae_off_on": error, "changed_by_3": changed}))
