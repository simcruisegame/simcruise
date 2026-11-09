"""Body surface for build_car.py: a smooth skin interpolated through a control
net of design lines (pure NumPy, no Blender).

The net is rows x levels. A *level* is a design line that runs the length of
the car (keel, rocker, shoulder, belt, roof rail, crown...). A *row* ties the
levels together across the body; rows may lean (a row runs up the A-pillar
and across the windscreen), which is what lets window and pillar edges fall
exactly on grid lines. The first and last rows are the nose and tail tips:
every level ends on the centre plane there, so the skin closes without caps.

Both directions use centripetal Catmull-Rom splines, so the skin passes
through every control point without overshoot. Points flagged as creases
break tangent continuity (hood/windscreen, shoulder lines). At the centre
plane the splines are mirrored, so the two halves meet smoothly.
"""
import math
import numpy as np


def _tj(ti, a, b, alpha):
    return ti + max(float(np.linalg.norm(b - a)), 1e-4) ** alpha


def cr_segment(P0, P1, P2, P3, ts, alpha=0.5):
    """Centripetal Catmull-Rom between P1 and P2 at local params ts (0..1)."""
    t0 = 0.0
    t1 = _tj(t0, P0, P1, alpha)
    t2 = _tj(t1, P1, P2, alpha)
    t3 = _tj(t2, P2, P3, alpha)
    t = (t1 + (t2 - t1) * np.asarray(ts, float))[:, None]
    A1 = (t1 - t) / (t1 - t0) * P0 + (t - t0) / (t1 - t0) * P1
    A2 = (t2 - t) / (t2 - t1) * P1 + (t - t1) / (t2 - t1) * P2
    A3 = (t3 - t) / (t3 - t2) * P2 + (t - t2) / (t3 - t2) * P3
    B1 = (t2 - t) / (t2 - t0) * A1 + (t - t0) / (t2 - t0) * A2
    B2 = (t3 - t) / (t3 - t1) * A2 + (t - t1) / (t3 - t1) * A3
    return (t2 - t) / (t2 - t1) * B1 + (t - t1) / (t2 - t1) * B2


def mirror_x(p):
    q = np.array(p, float)
    q[0] = -q[0]
    return q


def cr_curve(P, samples, crease=None, mirror_start=False, mirror_end=False):
    """Evaluate a Catmull-Rom curve through P (m x 3).

    samples[k] lists local parameters in [0, 1) for segment k (P[k] -> P[k+1]);
    the last point is appended. crease[k] breaks tangent continuity at P[k].
    mirror_start / mirror_end: the curve continues mirrored across x = 0
    (for curves that end on the centre plane)."""
    P = np.asarray(P, float)
    m = len(P)
    crease = crease if crease is not None else [False] * m
    out = []
    for k in range(m - 1):
        P1, P2 = P[k], P[k + 1]
        if k == 0:
            P0 = mirror_x(P[1]) if mirror_start else 2 * P1 - P2
        elif crease[k]:
            P0 = 2 * P1 - P2
        else:
            P0 = P[k - 1]
        if k == m - 2:
            P3 = mirror_x(P[m - 2]) if mirror_end else 2 * P2 - P1
        elif crease[k + 1]:
            P3 = 2 * P2 - P1
        else:
            P3 = P[k + 2]
        out.append(cr_segment(P0, P1, P2, P3, samples[k]))
    out.append(P[-1][None])
    return np.concatenate(out)


def segment_samples(lengths, spacing, inserts):
    """Per segment: uniform local params at about `spacing` metres, plus any
    inserted fractional positions (exact region boundaries)."""
    out = []
    for k, ln in enumerate(lengths):
        n = max(2, int(math.ceil(ln / spacing)))
        ts = set(round(i / n, 6) for i in range(n))
        for t in inserts.get(k, ()):
            if 0.0 < t < 1.0:
                ts.add(round(t, 6))
        ts = sorted(ts)
        # drop samples that would make slivers next to an insert
        clean = [ts[0]]
        for t in ts[1:]:
            if t - clean[-1] < 0.12 / n and t not in inserts.get(k, ()):
                continue
            if t - clean[-1] < 0.12 / n and clean[-1] not in inserts.get(k, ()) and clean[-1] != 0.0:
                clean[-1] = t
                continue
            clean.append(t)
        out.append(clean)
    return out


def global_params(samples):
    """Fractional net coordinates (segment index + local t) for each sample."""
    g = []
    for k, ts in enumerate(samples):
        g += [k + t for t in ts]
    g.append(float(len(samples)))
    return g


class Skin:
    """Evaluate the right half of the skin on a grid.

    net: array (rows, levels, 3). row_crease / level_crease: boolean arrays of
    the same (rows, levels) shape. s_inserts / l_inserts: fractional net
    coordinates that must be exact grid lines."""

    def __init__(self, net, row_crease, level_crease, ds=0.03, dl=0.022, s_inserts=(), l_inserts=(), l_coarse=None):
        self.net = net = np.asarray(net, float)
        R, Lv, _ = net.shape
        self.R, self.Lv = R, Lv
        s_ins = {}
        for s in s_inserts:
            k = min(int(math.floor(s)), R - 2)
            s_ins.setdefault(k, set()).add(round(s - k, 6))
        l_ins = {}
        for l in l_inserts:
            k = min(int(math.floor(l)), Lv - 2)
            l_ins.setdefault(k, set()).add(round(l - k, 6))
        # along the car: sample every level through the rows
        seg_len = [max(float(np.linalg.norm(net[k + 1, j] - net[k, j])) for j in range(Lv)) for k in range(R - 1)]
        self.s_samples = segment_samples([x * 1.15 for x in seg_len], ds, s_ins)
        self.s = global_params(self.s_samples)
        lines = []
        for j in range(Lv):
            lines.append(cr_curve(net[:, j], self.s_samples, crease=row_crease[:, j], mirror_start=True, mirror_end=True))
        lines = np.stack(lines, axis=1)            # (Ns, Lv, 3)
        # across the levels: the section through each sample
        lvl_len = [float(np.max(np.linalg.norm(lines[:, j + 1] - lines[:, j], axis=1))) for j in range(Lv - 1)]
        # the underbody needs far fewer samples than the visible sides
        coarse = l_coarse or [4.0, 2.0] + [1.0] * (Lv - 3)
        self.l_samples = segment_samples([x * 1.1 / coarse[j] for j, x in enumerate(lvl_len)], dl, l_ins)
        self.l = global_params(self.l_samples)
        # crease flags across levels are looked up at the nearest row
        grid = []
        for i, s in enumerate(self.s):
            r = min(int(round(s)), R - 1)
            grid.append(cr_curve(lines[i], self.l_samples, crease=level_crease[r], mirror_start=True, mirror_end=True))
        self.grid = np.stack(grid)                  # (Ns, Nl, 3), right half

    def index_s(self, s):
        return int(np.argmin(np.abs(np.asarray(self.s) - s)))

    def index_l(self, l):
        return int(np.argmin(np.abs(np.asarray(self.l) - l)))

    def ring(self):
        """Full section ring: right half keel->crown, then the left half back
        down to (but not including) the keel. Returns (grid, n_right)."""
        G = self.grid
        left = G[:, -2:0:-1].copy()
        left[..., 0] *= -1
        return np.concatenate([G, left], axis=1), G.shape[1]


def normals(G):
    """Outward normals of a (rows, ring) grid (ring wraps)."""
    di = np.gradient(G, axis=0)
    dj = np.roll(G, -1, axis=1) - np.roll(G, 1, axis=1)
    n = np.cross(di, dj)
    ln = np.linalg.norm(n, axis=2, keepdims=True)
    return n / np.maximum(ln, 1e-9)
