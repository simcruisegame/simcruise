"""Tiny NumPy raster painter for lamp, grille and gauge textures.

Images are float RGBA arrays (h, w, 4) in linear-ish sRGB values 0..1,
drawn with 4x supersampled coverage so edges are smooth. Coordinates are
normalised: u right 0..1, v up 0..1 (v = 0 is the bottom row).
"""
import math
import numpy as np


def hexc(h, a=1.0):
    h = h.lstrip("#")
    return np.array([int(h[i:i + 2], 16) / 255 for i in (0, 2, 4)] + [a])


class Canvas:
    def __init__(self, w, h, color="#000000", alpha=1.0, ss=3):
        self.w, self.h, self.ss = w, h, ss
        self.img = np.zeros((h, w, 4)); self.img[:] = hexc(color, alpha)
        ys = (np.arange(h * ss) + 0.5) / (h * ss)
        xs = (np.arange(w * ss) + 0.5) / (w * ss)
        self.U, self.V = np.meshgrid(xs, 1.0 - ys)

    def _blend(self, cover_ss, color, alpha=1.0):
        h, w, ss = self.h, self.w, self.ss
        cov = cover_ss.reshape(h, ss, w, ss).mean(axis=(1, 3)) * alpha
        c = color if isinstance(color, np.ndarray) else hexc(color)
        self.img[..., :3] = self.img[..., :3] * (1 - cov[..., None]) + c[:3] * cov[..., None]
        self.img[..., 3] = self.img[..., 3] * (1 - cov) + c[3] * cov
        return cov

    def poly(self, pts, color, alpha=1.0):
        P = np.asarray(pts, float)
        inside = np.zeros(self.U.shape, bool)
        x, y = self.U, self.V
        for (ax, ay), (bx, by) in zip(P, np.roll(P, -1, axis=0)):
            cond = (ay > y) != (by > y)
            with np.errstate(divide="ignore", invalid="ignore"):
                xint = (bx - ax) * (y - ay) / (by - ay + 1e-12) + ax
            inside ^= cond & (x < xint)
        return self._blend(inside.astype(float), color, alpha)

    def ellipse(self, cx, cy, rx, ry, color, alpha=1.0, aspect=None):
        a = aspect if aspect is not None else self.w / self.h
        d = ((self.U - cx) / rx) ** 2 + ((self.V - cy) / ry) ** 2
        return self._blend((d <= 1).astype(float), color, alpha)

    def rect(self, u0, v0, u1, v1, color, alpha=1.0, r=0.0):
        if r <= 0:
            m = (self.U >= u0) & (self.U <= u1) & (self.V >= v0) & (self.V <= v1)
            return self._blend(m.astype(float), color, alpha)
        cx = np.clip(self.U, u0 + r, u1 - r); cy = np.clip(self.V, v0 + r * self.w / self.h, v1 - r * self.w / self.h)
        d = ((self.U - cx) / r) ** 2 + ((self.V - cy) / (r * self.w / self.h)) ** 2
        return self._blend((d <= 1).astype(float), color, alpha)

    def line(self, pts, width, color, alpha=1.0):
        """Polyline with a width in normalised u units (scaled for v by aspect)."""
        asp = self.w / self.h
        m = np.zeros(self.U.shape, bool)
        P = np.asarray(pts, float)
        for (ax, ay), (bx, by) in zip(P[:-1], P[1:]):
            vx, vy = bx - ax, (by - ay) / asp
            L2 = vx * vx + vy * vy + 1e-12
            t = np.clip(((self.U - ax) * vx + (self.V - ay) / asp * vy) / L2, 0, 1)
            dx = self.U - (ax + t * vx); dy = (self.V - ay) / asp - t * vy
            m |= (dx * dx + dy * dy) <= (width / 2) ** 2
        return self._blend(m.astype(float), color, alpha)

    def gradient_v(self, top, bottom, alpha=1.0, mask=None):
        t = self.V
        c = hexc(bottom)[None, None, :] * (1 - t[..., None]) + hexc(top)[None, None, :] * t[..., None]
        h, w, ss = self.h, self.w, self.ss
        cs = c.reshape(h, ss, w, ss, 4).mean(axis=(1, 3))
        cov = np.ones((h, w)) * alpha if mask is None else mask * alpha
        self.img[..., :3] = self.img[..., :3] * (1 - cov[..., None]) + cs[..., :3] * cov[..., None]

    def pattern(self, kind, scale, color, alpha=1.0, mask_poly=None, line=0.25):
        """Repeating grille pattern: honeycomb, slats, mesh (diamond)."""
        asp = self.w / self.h
        x, y = self.U * scale, self.V * scale / asp
        if kind == "slats":
            f = (y % 1.0)
            m = f < line
        elif kind == "mesh":
            a = ((x + y) % 1.0); b = ((x - y) % 1.0)
            m = (a < line) | (b < line)
        elif kind == "honeycomb":
            # hexagon cell walls via distance to nearest hex centre
            s3 = math.sqrt(3)
            q = np.stack([x, y * 1.0], -1)
            c1 = np.stack([np.round(x / s3) * s3, np.round(y)], -1)
            c2 = np.stack([(np.floor(x / s3) + 0.5) * s3, np.floor(y) + 0.5], -1)
            d1 = np.linalg.norm(q - c1, axis=-1); d2 = np.linalg.norm(q - c2, axis=-1)
            d = np.minimum(d1, d2); e = np.abs(d1 - d2)
            m = e < line
        else:
            m = np.zeros_like(x, bool)
        if mask_poly is not None:
            P = np.asarray(mask_poly, float); inside = np.zeros(self.U.shape, bool)
            for (ax, ay), (bx, by) in zip(P, np.roll(P, -1, axis=0)):
                cond = (ay > self.V) != (by > self.V)
                with np.errstate(divide="ignore", invalid="ignore"):
                    xint = (bx - ax) * (self.V - ay) / (by - ay + 1e-12) + ax
                inside ^= cond & (self.U < xint)
            m &= inside
        return self._blend(m.astype(float), color, alpha)

    def result(self):
        return np.clip(self.img, 0, 1)
