"""Turn the generated detail photographs into the game's detail kit (CPU only).

Each slice becomes a ratio map with mean 1: the photograph divided by a large blur of
itself, so it only carries fine detail. Absolute colour always comes from the de-lit aerial
photograph or the class colour in the shaders; the kit only modulates it. Each slice is also
made tileable (blended with its half-period shift through a smooth window), so the shader's
hex tiling has no seams. Height (log luminance ratio) gives a normal map by Sobel.

Output (data/art/detail-kit/):
  detail-albedo.png  2048 x 1024, 4 x 2 slices of 512 px (480 px + a 16 px wrapped border),
                     RGB = ratio - 0.5 (ratio clamped to the slice's range, inside 0.5..1.5),
                     A = height 0..1; stored sRGB-encoded (the game creates every texture sRGB,
                     so the sampler returns the linear value).
  detail-normal.png  same layout; RG = tangent-space normal x, y (0.5 = flat), B = 1, A = 1.
Usage: python make_kit.py --raw <dir with the generated pngs> --out ../../data/art/detail-kit
"""
import argparse
import json
import os

import numpy as np
from PIL import Image
from scipy import ndimage

# name, index, ratio clamp (albedo ranges: a ratio can only move a material inside its real
# range), chroma kept (0 = luminance only), normal strength
SLICES = [
    ("asphalt", (0.60, 1.50), 0.3, 2.0),
    ("concrete", (0.75, 1.30), 0.3, 1.2),
    ("stucco", (0.85, 1.15), 0.2, 0.8),
    ("brick", (0.60, 1.40), 0.6, 2.0),
    ("soil", (0.60, 1.50), 0.5, 2.0),
    ("sand", (0.75, 1.30), 0.3, 1.0),
    ("litter", (0.50, 1.50), 0.6, 2.5),
    ("bark", (0.50, 1.50), 0.4, 3.0),
]
CELL, PAD = 512, 16
INNER = CELL - 2 * PAD
W_LUM = np.array([0.2126, 0.7152, 0.0722])


def srgb_to_linear(x):
    return np.where(x <= 0.04045, x / 12.92, ((x + 0.055) / 1.055) ** 2.4)


def linear_to_srgb(x):
    x = np.clip(x, 0.0, 1.0)
    return np.where(x <= 0.0031308, x * 12.92, 1.055 * x ** (1 / 2.4) - 0.055)


def tileable(img):
    """Blend with the half-period shifted copy through a window that is 0 at the edges."""
    n = img.shape[0]
    t = np.sin(np.linspace(0, np.pi, n)) ** 2
    w = np.outer(t, t)[..., None] if img.ndim == 3 else np.outer(t, t)
    shifted = np.roll(np.roll(img, n // 2, 0), n // 2, 1)
    ws = np.roll(np.roll(w, n // 2, 0), n // 2, 1)
    return (img * w + shifted * ws) / np.maximum(w + ws, 1e-6)


def slice_maps(path, clamp, chroma, strength):
    rgb = np.asarray(Image.open(path).convert("RGB").resize((INNER, INNER), Image.LANCZOS), dtype=np.float64) / 255.0
    lin = srgb_to_linear(rgb)
    lin = tileable(lin)
    # low frequencies out: divide by a wide blur (wrap: the slice tiles)
    blur = np.stack([ndimage.gaussian_filter(lin[..., c], INNER / 10, mode="wrap") for c in range(3)], -1)
    ratio = lin / np.maximum(blur, 1e-4)
    lum = ratio @ W_LUM
    tint = ratio / np.maximum(lum[..., None], 1e-4)
    tint = 1.0 + (tint - 1.0) * chroma
    lum = lum / lum.mean()
    ratio = np.clip(lum[..., None] * tint, clamp[0], clamp[1])
    ratio /= (ratio @ W_LUM).mean()
    ratio = np.clip(ratio, 0.5, 1.5)
    height = np.log(np.maximum(lum, 1e-3))
    height = ndimage.gaussian_filter(height, 0.8, mode="wrap")
    gx = ndimage.sobel(height, 1, mode="wrap") / 8.0
    gy = ndimage.sobel(height, 0, mode="wrap") / 8.0
    n = np.stack([-gx * strength, -gy * strength, np.ones_like(gx)], -1)
    n /= np.linalg.norm(n, axis=-1, keepdims=True)
    h01 = (height - height.min()) / max(height.max() - height.min(), 1e-6)
    return ratio, n, h01


def pad_wrap(a):
    return np.pad(a, ((PAD, PAD), (PAD, PAD)) + ((0, 0),) * (a.ndim - 2), mode="wrap")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--raw", required=True)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    alb = np.zeros((2 * CELL, 4 * CELL, 4))
    nrm = np.zeros((2 * CELL, 4 * CELL, 4))
    stats = []
    for i, (name, clamp, chroma, strength) in enumerate(SLICES):
        ratio, n, h = slice_maps(os.path.join(a.raw, f"{name}.png"), clamp, chroma, strength)
        y, x = (i // 4) * CELL, (i % 4) * CELL
        alb[y:y + CELL, x:x + CELL, :3] = pad_wrap(ratio - 0.5)
        alb[y:y + CELL, x:x + CELL, 3] = pad_wrap(h)
        nrm[y:y + CELL, x:x + CELL, :2] = pad_wrap(n[..., :2] * 0.5 + 0.5)
        nrm[y:y + CELL, x:x + CELL, 2:] = 1.0
        lum = ratio @ W_LUM
        stats.append({"slice": i, "name": name, "ratio_mean": round(float(lum.mean()), 4),
                      "ratio_p5": round(float(np.percentile(lum, 5)), 3), "ratio_p95": round(float(np.percentile(lum, 95)), 3)})
    # sRGB-encode RGB and A alike: the game makes the texture sRGB and its sampler decodes RGB only,
    # so A is stored linear.
    a8 = np.concatenate([linear_to_srgb(alb[..., :3]), alb[..., 3:]], -1)
    n8 = np.concatenate([linear_to_srgb(nrm[..., :3]), nrm[..., 3:]], -1)
    Image.fromarray((a8 * 255 + 0.5).astype(np.uint8), "RGBA").save(os.path.join(a.out, "detail-albedo.png"), optimize=True)
    Image.fromarray((n8 * 255 + 0.5).astype(np.uint8), "RGBA").save(os.path.join(a.out, "detail-normal.png"), optimize=True)
    with open(os.path.join(a.out, "kit.json"), "w") as f:
        json.dump({"cell": CELL, "pad": PAD, "grid": [4, 2], "slices": stats}, f, indent=2)
    print(json.dumps(stats, indent=1))


if __name__ == "__main__":
    main()
