"""Compare two frames of the same pose (for example windowed against off screen).

Usage: python -I scripts/compare-frames.py A.png B.png [diff.png]

Prints the mean absolute difference (0-255), the share of pixels that differ by more than 25,
the mean luminance of each (exposure), and the variance of the Laplacian of each (sharpness:
anti-aliasing and motion blur lower it). Writes an amplified difference image when asked.
"""
import sys

import numpy as np
from PIL import Image


def load(path):
    return np.asarray(Image.open(path).convert('RGB'), dtype=np.float32)


def luma(img):
    return 0.2126 * img[..., 0] + 0.7152 * img[..., 1] + 0.0722 * img[..., 2]


def sharpness(img):
    y = luma(img)
    lap = -4 * y[1:-1, 1:-1] + y[:-2, 1:-1] + y[2:, 1:-1] + y[1:-1, :-2] + y[1:-1, 2:]
    return float(lap.var())


a, b = load(sys.argv[1]), load(sys.argv[2])
if a.shape != b.shape:
    print(f'COMPARE size differs: {a.shape} against {b.shape}')
    sys.exit(1)
d = np.abs(a - b).max(axis=2)
print(f'COMPARE mean_abs_diff={float(np.abs(a - b).mean()):.2f} changed_over_25={100.0 * float((d > 25).mean()):.2f}% '
      f'luma_a={float(luma(a).mean()):.1f} luma_b={float(luma(b).mean()):.1f} sharp_a={sharpness(a):.1f} sharp_b={sharpness(b):.1f}')
if len(sys.argv) > 3:
    Image.fromarray(np.clip(d * 4, 0, 255).astype(np.uint8)).save(sys.argv[3])
