"""Spherical two-station registration and static feature triangulation. Rotations are in degrees."""

from planes import D, Site, Image, np, cv2, json
from scipy.spatial.transform import Rotation
from scipy.optimize import least_squares

def get_matches(s, ai, bi):
    fn = D / (s.slug + f'-matches-{ai}{bi}.npz')
    if fn.exists():
        with np.load(fn) as k:
            return (k['a'], k['b'])
    features = []
    for i in (ai, bi):
        img = s.im[i]
        h, w = img.shape[:2]
        mask = np.zeros((h, w), np.uint8)
        mask[int(h * 0.16):int(h * 0.64)] = 255
        feat, desc = cv2.SIFT_create(nfeatures=5500, contrastThreshold=0.011).detectAndCompute(cv2.cvtColor(img, cv2.COLOR_RGB2GRAY), mask)
        features.append((np.array([k.pt for k in feat]), desc))
    match = cv2.BFMatcher()
    f, b = features
    ff = match.knnMatch(f[1], b[1], k=2)
    bb = match.knnMatch(b[1], f[1], k=2)
    rec = {x.queryIdx: x.trainIdx for x, y in bb if x.distance < 0.81 * y.distance}
    a, b = zip(*[(f[0][x.queryIdx], b[0][x.trainIdx]) for x, y in ff if x.distance < 0.81 * y.distance and rec.get(x.trainIdx) == x.queryIdx])
    a, b = (np.array(a), np.array(b))
    np.savez(fn, a=a, b=b)
    return (a, b)

def rays(f, coords):
    az = (coords[:, 0] + 0.5) / 2048 * (2 * np.pi) - np.pi + np.radians(f['azimuth'])
    alt = (0.5 - (coords[:, 1] + 0.5) / 1024) * np.pi
    return np.stack([np.sin(az) * np.cos(alt), -np.cos(az) * np.cos(alt), np.sin(alt)], axis=1)

def triangulate(r1, r2, base):
    g = np.sum(r1 * r2, axis=1)
    s = r1 @ base
    t = r2 @ base
    d1 = (s - g * t) / (1 - g * g)
    d2 = (g * s - t) / (1 - g * g)
    p = (r1 * d1[:, None] + r2 * d2[:, None] + base) * 0.5
    return (d1, d2, p)

def cal(s, ai, bi, knownA=None, exempt=None, use_static=True):
    a, b = get_matches(s, ai, bi)
    if use_static:

        def stable(im, coords):
            data = np.array(Image.open(D / (s.frames[im]['id'][:8] + '-static.png')))
            xy = np.round(coords).astype(int)
            return data[xy[:, 1], xy[:, 0]] > 0
        keep = stable(ai, a) & stable(bi, b)
        a, b = (a[keep], b[keep])
    r1, r2 = (rays(s.frames[ai], a), rays(s.frames[bi], b))
    base = np.append(s.pos[bi] - s.pos[ai], 0)
    n = base / np.linalg.norm(base)
    if exempt is not None:
        r = rays(s.frames[ai], a)
        az = np.degrees(np.arctan2(r[:, 0], -r[:, 1]))
        ang = abs((az - exempt + 180) % 360 - 180)
        mask = ang > 52
        r1, r2 = (r1[mask], r2[mask])
        a, b = (a[mask], b[mask])

    def apply(r, c):
        return Rotation.from_euler('xyz', c, degrees=True).apply(r)

    def metric(c):
        q1 = apply(r1, c[:3])
        q2 = apply(r2, c[3:])
        normal = np.cross(q1, n)
        normal /= np.maximum(1e-06, np.linalg.norm(normal, axis=1, keepdims=True))
        return np.degrees(np.arcsin(np.clip(np.sum(normal * q2, axis=1), -1, 1)))
    prior = np.zeros(6)
    sort = np.ones(6) * 0.35
    if knownA is not None:
        prior[:3] = knownA
        sort[:3] = 10000

    def loss(c):
        return np.append(metric(c), (c - prior) * sort)
    opt = least_squares(loss, prior, loss='cauchy', f_scale=0.65, max_nfev=240)
    c = opt.x
    err = metric(c)
    m = abs(err) < 1
    q1, q2 = (apply(r1, c[:3]), apply(r2, c[3:]))
    d1, d2, p = triangulate(q1, q2, base)
    ok = m & (d1 > 4) & (d2 > 4) & (d1 < 130) & (d2 < 130)
    return (c, err, dict(a=a, b=b, ok=ok, p=p + np.append(s.pos[ai], 2.3), r1=q1, r2=q2))

class CalSite(Site):

    def __init__(self, slug):
        super().__init__(slug)
        self.rot = np.array(json.loads((D / (slug + '-pose.json')).read_text())['rot']) if (D / (slug + '-pose.json')).exists() else np.zeros((3, 3))

    def project(self, fi, e, s, up):
        a, b, c = np.broadcast_arrays(e, s, up)
        r = np.stack((a, b, c), axis=-1) @ Rotation.from_euler('xyz', self.rot[fi], degrees=True).as_matrix()
        return super().project(fi, r[:, :, 0], r[:, :, 1], r[:, :, 2])
