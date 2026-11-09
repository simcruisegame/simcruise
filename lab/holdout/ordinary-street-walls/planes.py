"""Mapped wall geometry and spherical image sampling. World axes are east, south and up."""

import math, json, sys
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw
import cv2
D = Path(__file__).parent

def cross(a, b):
    return a[..., 0] * b[..., 1] - a[..., 1] * b[..., 0]

class Site:

    def __init__(self, slug):
        self.slug = slug
        self.frames = json.loads((D / 'frames.json').read_text())[slug]
        x, y, t = (*json.loads(next(D.glob(slug + '-1*.json')).read_text())['id'], json.loads(next(D.glob(slug + '-1*.json')).read_text()))
        self.tile = t
        self.x = x
        self.y = y
        self.lam = 40075016.68558 / 65536 * math.cos(math.radians(self.frames[0]['lat']))
        self.pos = []
        self.im = []
        for f in self.frames:
            self.pos.append(self.xy(f['lat'], f['lon']))
            self.im.append(np.array(Image.open(D / f['file']).convert('RGB')))
        self.e = []
        self.idx = []
        self.height = []
        for bi, b in enumerate(t['buildings']):
            if b[3] > 0.2:
                continue
            ring = b[0][0]
            for ei, (a, z) in enumerate(zip(ring, ring[1:] + ring[:1])):
                a, z = (np.array(a) * self.lam, np.array(z) * self.lam)
                if np.linalg.norm(a - z) > 0.9:
                    self.e.append([a, z])
                    self.idx.append((bi, ei))
                    self.height.append(b[2])
        self.e = np.array(self.e)

    def xy(self, lat, lon):
        return np.array([(lon + 180.0) / 360 * 65536 - self.x, (1.0 - math.asinh(math.tan(math.radians(lat))) / math.pi) * 32768 - self.y]) * self.lam

    def invxy(self, xy):
        x, y = xy / self.lam + [self.x, self.y]
        return (np.degrees(np.arctan(np.sinh(np.pi * (1 - y / 32768)))), x / 65536 * 360 - 180)

    def rayhits(self, pos, dirs):
        a = self.e[:, 0] - pos
        side = self.e[:, 1] - self.e[:, 0]
        d = cross(dirs[:, None], side)
        d = np.where(abs(d) < 1e-08, np.nan, d)
        dist = cross(a, side) / d
        u = cross(a, dirs[:, None]) / d
        out = np.where((dist > 0) & (u >= 0) & (u <= 1), dist, np.inf)
        return (np.argmin(out, axis=1), out.min(axis=1))

    def rect(self, fi, k, w=512, h=256, aspect=None, cz=2.3):
        a, b = self.e[k]
        length = np.linalg.norm(b - a)
        u = (np.arange(w) + 0.5) / w
        height = self.height[k] if not aspect else aspect
        z = (1 - (np.arange(h) + 0.5) / h) * height
        xy = a + (b - a) * u[:, None] - self.pos[fi]
        return (self.project(fi, xy[:, 0], xy[:, 1], z[:, None] - cz), length, height)

    def project(self, fi, e, s, up):
        f = self.frames[fi]
        im = self.im[fi]
        az = np.radians(f['azimuth'])
        d = np.hypot(e, s)
        angle = (np.arctan2(e, -s) - az + np.pi) % (2 * np.pi)
        gx = np.broadcast_to(angle / (np.pi * 2) * im.shape[1] - 0.5, np.broadcast_shapes(np.shape(angle), np.shape(up)))
        gy = im.shape[0] * (0.5 - np.arctan2(up, d) / np.pi) - 0.5
        return cv2.remap(im, gx.astype('f'), np.broadcast_to(gy, gx.shape).astype('f'), cv2.INTER_LINEAR, borderMode=cv2.BORDER_WRAP)

    def candidates(self):
        out = []
        for k, (a, b) in enumerate(self.e):
            d = b - a
            L = np.linalg.norm(d)
            if L < 4:
                continue
            dim = self.height[k]
            target = (a + b) / 2
            distances = [np.linalg.norm(target - p) for p in self.pos[:2]]
            if max(distances) > 115 or min(distances) < 4:
                continue
            points = a + d * np.linspace(0.12, 0.88, 9)[:, None]
            norm = d / L
            tests = []
            for p in self.pos[:2]:
                delta = points - p
                depth = np.linalg.norm(delta, axis=1)
                hits, dep = self.rayhits(p, delta / depth[:, None])
                normal = abs(cross((target - p) / np.linalg.norm(target - p), norm))
                tests.append(np.mean(hits == k) * normal)
            if min(tests) < 0.42:
                continue
            out.append((round(min(tests) * min(L / 12, 1) * min(25 / max(distances), 1), 3), k, self.idx[k], round(L, 1), dim, [round(x, 1) for x in distances]))
        return sorted(out, reverse=True)
