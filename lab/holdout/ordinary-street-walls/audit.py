"""Rank visible mapped walls by spatially distinct, coplanar two-camera observations."""

from poses import np, cv2

def rank(s, o):
    pts = o['p'][o['ok']]
    acc = []
    for _, k, *_ in s.candidates():
        a, b = s.e[k]
        length = np.linalg.norm(b - a)
        u = (b - a) / length
        side = [-u[1], u[0]]
        ac = (pts[:, :2] - a) @ u
        ds = (pts[:, :2] - a) @ side
        m = (ac > 0) & (ac < length) & (pts[:, 2] > 2.8) & (pts[:, 2] < s.height[k] + 0.8) & (abs(ds) < 2.6)
        bins = np.arange(-2.5, 2.6, 0.25)
        cc = [m & (abs(ds - v) < 0.5) for v in bins]
        m = max(cc, key=sum)
        uv = np.stack((ac[m], pts[m, 2]), axis=1)
        uniq = len(np.unique(np.round(uv, 0), axis=0))
        area = cv2.contourArea(cv2.convexHull(uv.astype('f'))) if len(uv) > 2 else 0
        acc.append(dict(k=k, n=int(sum(m)), uniq=uniq, area=round(area, 1), ds=np.median(ds[m]) if any(m) else 0, mask=m, uv=uv))
    return sorted(acc, key=lambda q: q['area'] * q['uniq'], reverse=True)
