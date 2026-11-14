"""Detail in frames: edge density and fine-detail energy, for the standstill test.

Usage: python -I scripts/detail-metrics.py <out.json> <frame.png> [<frame.png> ...]

Edge density: the share of pixels whose Sobel gradient of luma is above 0.1 (0..1 luma).
Fine-detail energy: the mean absolute value of the luma minus its 5x5 box blur (the detail a
blur removes). Both rise when the world adds detail to a still view. The HUD is not masked: take
the frames with the same HUD in each.
"""
import json
import sys

import numpy as np
from PIL import Image


def luma(path):
    a = np.asarray(Image.open(path).convert('RGB'), dtype=np.float32) / 255.0
    return 0.2126 * a[..., 0] + 0.7152 * a[..., 1] + 0.0722 * a[..., 2]


def box(y, r=2):
    p = np.pad(y, r, mode='edge')
    c = p.cumsum(0).cumsum(1)
    c = np.pad(c, ((1, 0), (1, 0)))
    n = 2 * r + 1
    h, w = y.shape
    return (c[n:n + h, n:n + w] - c[:h, n:n + w] - c[n:n + h, :w] + c[:h, :w]) / (n * n)


def metrics(path):
    y = luma(path)
    gx = np.zeros_like(y)
    gy = np.zeros_like(y)
    gx[1:-1, 1:-1] = (y[:-2, 2:] + 2 * y[1:-1, 2:] + y[2:, 2:]) - (y[:-2, :-2] + 2 * y[1:-1, :-2] + y[2:, :-2])
    gy[1:-1, 1:-1] = (y[2:, :-2] + 2 * y[2:, 1:-1] + y[2:, 2:]) - (y[:-2, :-2] + 2 * y[:-2, 1:-1] + y[:-2, 2:])
    mag = np.hypot(gx, gy) / 4.0
    return {'frame': path, 'edge_density': float((mag > 0.1).mean()), 'fine_energy': float(np.abs(y - box(y)).mean())}


def main():
    out = sys.argv[1]
    rows = [metrics(p) for p in sys.argv[2:]]
    rising = all(b['edge_density'] > a['edge_density'] and b['fine_energy'] > a['fine_energy'] for a, b in zip(rows, rows[1:]))
    with open(out, 'w', encoding='utf-8') as f:
        json.dump({'frames': rows, 'rising': rising}, f, indent=2)
    print('DETAIL', 'RISING' if rising else 'NOT RISING', ' '.join('%.4f/%.5f' % (r['edge_density'], r['fine_energy']) for r in rows))


if __name__ == '__main__':
    main()
