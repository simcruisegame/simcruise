"""Compare fixed-pose tree silhouette to pre-marked crown edges in the real photograph."""

from PIL import Image, ImageDraw
import numpy as np, json, sys
from pathlib import Path
from scipy import ndimage

p = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else Path.cwd()
prefix = sys.argv[1] if len(sys.argv) > 1 else "candidate"
# Clearly visible foreground tree ridge in the 960×640 source, marked before the A/B render.
# Avoid ambiguous fine lateral branches and background mountain forests. Each mark admits a 5 px horizontal neighbourhood.
landmarks = [
    (12, 156),
    (52, 166),
    (72, 198),
    (93, 226),
    (111, 250),
    (133, 268),
    (152, 277),
    (174, 284),
    (199, 324),
    (220, 284),
    (246, 329),
    (282, 300),
    (306, 316),
    (331, 350),
    (360, 380),
    (417, 365),
    (514, 378),
    (574, 413),
]


def img(name):
    im = Image.open(p / name).convert("RGB")
    w, h = im.size
    if (w, h) == (960, 720):
        im = im.crop((0, 40, 960, 680))
    return im


def score(name):
    im = img(prefix + "-" + name + ".png")
    back = (
        img(prefix + "-after-empty.png")
        if name == "after" and (p / (prefix + "-after-empty.png")).is_file()
        else img(prefix + "-empty.png")
    )
    a = np.asarray(im).astype(float)
    b = np.asarray(back).astype(float)
    hit = np.max(abs(a - b), axis=2) > 17
    # Exclude single-pixel noise, with 3 neighbouring forest pixels required out of 9.
    mask = hit & (ndimage.convolve(hit.astype(int), np.ones((3, 3), int)) >= 3)
    ys = []
    d = ImageDraw.Draw(im)
    for x, y in landmarks:
        reg = mask[90:620, max(0, x - 5) : min(960, x + 6)]
        rows = np.where(reg.sum(axis=1) >= 2)[0]
        obs = (rows.min() + 90) if len(rows) else 620
        d.line((x, y, x, obs), fill="#ec8851")
        d.ellipse((x - 2, y - 2, x + 2, y + 2), fill="magenta")
        d.ellipse((x - 2, obs - 2, x + 2, obs + 2), fill="cyan")
        ys.append(int(obs))
    im.save(p / (prefix + "-" + name + "-score.jpg"), quality=90)
    e = abs(np.array(ys) - np.array(landmarks)[:, 1])
    return {
        "observedEdges": ys,
        "medianAbsPixels": float(np.median(e)),
        "meanAbsPixels": float(np.mean(e)),
        "within30": int((e < 30).sum()),
        "foliagePixelsBetween400and520": int(mask[400:520].sum()),
    }


if __name__ == "__main__":
    a, b = score("before"), score("after")
    out = {"controlSites": landmarks, "before": a, "after": b}
    (p / (prefix + "-scores.json")).write_text(json.dumps(out, indent=2))
    print(a, b)
