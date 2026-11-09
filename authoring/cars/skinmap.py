"""Place things on the body skin (pure NumPy).

SkinMap evaluates the dense right-half grid at any net coordinate (s, l) by
bilinear interpolation, and inverts view-plane points (side, front, rear or
top projections, in metres) back to (s, l). Features such as lamps, grilles
and shut lines are outlines in (s, l), so they follow the skin around
corners exactly and never float or sink.
"""
import math
import numpy as np

VIEWS = {
    # view: (axis that is dropped, 2D axes (u, v), facing direction)
    "side": ((1, 2), np.array([1.0, 0.0, 0.0])),     # (y, z), seen from +x (right side)
    "front": ((0, 2), np.array([0.0, 1.0, 0.0])),    # (x, z), seen from the front
    "rear": ((0, 2), np.array([0.0, -1.0, 0.0])),    # (x, z), seen from behind
    "top": ((0, 1), np.array([0.0, 0.0, 1.0])),      # (x, y), seen from above
}


class SkinMap:
    def __init__(self, grid, s, l):
        self.G = np.asarray(grid, float)            # (Ns, Nl, 3) right half
        self.s = np.asarray(s, float)
        self.l = np.asarray(l, float)
        di = np.gradient(self.G, axis=0)
        dj = np.gradient(self.G, axis=1)
        n = np.cross(di, dj)
        self.N = n / np.maximum(np.linalg.norm(n, axis=2, keepdims=True), 1e-9)

    def _locate(self, arr, v):
        k = int(np.clip(np.searchsorted(arr, v) - 1, 0, len(arr) - 2))
        t = (v - arr[k]) / max(arr[k + 1] - arr[k], 1e-12)
        return k, float(np.clip(t, 0.0, 1.0))

    def eval(self, s, l, field=None):
        """Interpolate on the same two triangles per cell that the body mesh
        uses (diagonal from (i, j) to (i+1, j+1)), so patches lifted a few
        millimetres always stay above the rendered skin."""
        F = self.G if field is None else field
        i, a = self._locate(self.s, s)
        j, b = self._locate(self.l, l)
        p00, p10, p11, p01 = F[i, j], F[i + 1, j], F[i + 1, j + 1], F[i, j + 1]
        if a >= b:
            return p00 + a * (p10 - p00) + b * (p11 - p10)
        return p00 + b * (p01 - p00) + a * (p11 - p01)

    def point(self, s, l, lift=0.0, side=1):
        p = self.eval(s, l)
        n = self.eval(s, l, self.N)
        n = n / max(np.linalg.norm(n), 1e-9)
        q = p + n * lift
        if side < 0:
            q = q * np.array([-1.0, 1.0, 1.0])
        return q

    def normal(self, s, l, side=1):
        n = self.eval(s, l, self.N)
        n = n / max(np.linalg.norm(n), 1e-9)
        return n * np.array([side, 1.0, 1.0])

    def to_sl(self, view, pts):
        """View-plane points (metres) -> net coordinates (s, l), using only the
        part of the skin that faces the view."""
        (ax_u, ax_v), facing = VIEWS[view]
        G2 = self.G[..., [ax_u, ax_v]]
        face = (self.N @ facing) > 0.05
        out = []
        for (u, v) in pts:
            d = np.sqrt(np.sum((G2 - np.array([u, v])) ** 2, axis=2))
            d[~face] = np.inf
            # of the skin points that project near the target, take the one
            # nearest the viewer (a rising underbody also "faces" forward)
            near = d <= max(float(np.min(d)) * 1.5, 0.02)
            depth = np.where(near, self.G @ facing, -np.inf)
            i, j = np.unravel_index(int(np.argmax(depth)), d.shape)
            best = None
            for di in (-1, 0):
                for dj in (-1, 0):
                    ii, jj = i + di, j + dj
                    if ii < 0 or jj < 0 or ii + 1 >= self.G.shape[0] or jj + 1 >= self.G.shape[1]:
                        continue
                    a, b = _inv_bilinear(G2[ii, jj], G2[ii + 1, jj], G2[ii + 1, jj + 1], G2[ii, jj + 1], np.array([u, v]))
                    err = max(0, -a, a - 1, -b, b - 1)
                    if best is None or err < best[0]:
                        best = (err, ii, jj, a, b)
            _, ii, jj, a, b = best
            a = min(max(a, 0), 1); b = min(max(b, 0), 1)
            out.append((self.s[ii] + a * (self.s[ii + 1] - self.s[ii]), self.l[jj] + b * (self.l[jj + 1] - self.l[jj])))
        return out


    def nearest_sl(self, pts3):
        """3D points (right side) -> net coordinates of the nearest skin point."""
        out = []
        G = self.G
        for q in np.asarray(pts3, float):
            d = np.sum((G - q) ** 2, axis=2)
            i, j = np.unravel_index(int(np.argmin(d)), d.shape)
            best = None
            for di in (-1, 0):
                for dj in (-1, 0):
                    ii, jj = i + di, j + dj
                    if ii < 0 or jj < 0 or ii + 1 >= G.shape[0] or jj + 1 >= G.shape[1]:
                        continue
                    p00, p10, p11, p01 = G[ii, jj], G[ii + 1, jj], G[ii + 1, jj + 1], G[ii, jj + 1]
                    e1 = p10 - p00; e2 = p01 - p00
                    n = np.cross(e1, e2)
                    if np.linalg.norm(n) < 1e-12:
                        continue
                    b1 = e1 / max(np.linalg.norm(e1), 1e-12)
                    b2 = np.cross(n / np.linalg.norm(n), b1)
                    P2 = lambda p: np.array([np.dot(p - p00, b1), np.dot(p - p00, b2)])
                    a, b = _inv_bilinear(P2(p00), P2(p10), P2(p11), P2(p01), P2(q))
                    err = max(0, -a, a - 1, -b, b - 1)
                    if best is None or err < best[0]:
                        best = (err, ii, jj, a, b)
            if best is None:
                out.append((self.s[i], self.l[j])); continue
            _, ii, jj, a, b = best
            a = min(max(a, 0), 1); b = min(max(b, 0), 1)
            out.append((self.s[ii] + a * (self.s[ii + 1] - self.s[ii]), self.l[jj] + b * (self.l[jj + 1] - self.l[jj])))
        return out


def _inv_bilinear(p00, p10, p11, p01, q):
    """(a, b) with bilinear(p00, p10, p11, p01; a, b) = q (Newton)."""
    a = b = 0.5
    for _ in range(12):
        p = (1 - a) * (1 - b) * p00 + a * (1 - b) * p10 + a * b * p11 + (1 - a) * b * p01
        da = (1 - b) * (p10 - p00) + b * (p11 - p01)
        db = (1 - a) * (p01 - p00) + a * (p11 - p10)
        J = np.array([[da[0], db[0]], [da[1], db[1]]])
        r = q - p
        try:
            step = np.linalg.solve(J, r)
        except np.linalg.LinAlgError:
            break
        a += step[0]; b += step[1]
        if abs(step[0]) + abs(step[1]) < 1e-7:
            break
    return a, b


def resample(poly, n, closed=True):
    """Evenly resample a polyline/polygon of 2D points to n points."""
    P = np.asarray(poly, float)
    if closed:
        P = np.vstack([P, P[:1]])
    seg = np.linalg.norm(np.diff(P, axis=0), axis=1)
    t = np.concatenate([[0], np.cumsum(seg)])
    total = t[-1]
    ts = np.linspace(0, total, n, endpoint=not closed)
    out = np.stack([np.interp(ts, t, P[:, 0]), np.interp(ts, t, P[:, 1])], axis=1)
    return out


def round_poly(poly, radius, steps=6):
    """Round the corners of a 2D polygon (quadratic fillets)."""
    out = []
    n = len(poly)
    for i in range(n):
        p0, p1, p2 = np.array(poly[i - 1], float), np.array(poly[i], float), np.array(poly[(i + 1) % n], float)
        a, b = p0 - p1, p2 - p1
        r = min(radius, np.linalg.norm(a) / 2.2, np.linalg.norm(b) / 2.2)
        if r < 1e-6:
            out.append(tuple(p1)); continue
        s0 = p1 + a / np.linalg.norm(a) * r
        s1 = p1 + b / np.linalg.norm(b) * r
        for k in range(steps + 1):
            t = k / steps
            q = s0 * (1 - t) ** 2 + p1 * 2 * (1 - t) * t + s1 * t * t
            out.append((q[0], q[1]))
    return out
