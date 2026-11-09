"""Bake a two-source atlas and reframe the separate validation photograph."""

from audit import rank
from poses import CalSite, cal, D, np, cv2, Image, json
from dense import supported_surface

def run(site):
    s = CalSite(site)
    s.rot = np.zeros((3, 3))
    best = None
    options = []
    for off in (0, 180):
        for f in s.frames:
            f['azimuth'] = (f['azimuth'] + off) % 360
        c, err, obs = cal(s, 0, 1)
        ranked = rank(s, obs)
        score = ranked[0]['uniq'] * ranked[0]['area']
        options.append([off, score, int(sum(obs['ok'])), len(err)])
        if best is None or score > best[0]:
            best = (score, off, c.copy(), ranked, obs, err.copy())
        for f in s.frames:
            f['azimuth'] = (f['azimuth'] - off) % 360
    score, off, c, ranked, o, err = best
    if not score:
        return None
    for f in s.frames:
        f['azimuth'] = (f['azimuth'] + off) % 360
    options_top = [r for r in ranked if r['uniq'] >= 10 and r['area'] >= 30 and (np.ptp(r['uv'][:, 0]) >= 1.8) and (np.ptp(r['uv'][:, 1]) >= 3.0)]
    if not options_top:
        return None
    q = options_top[0]
    k = q['k']
    a, b = s.e[k]
    direct = b - a
    L = np.linalg.norm(direct)
    unit = direct / L
    side = np.array([-unit[1], unit[0]])
    ds = q['ds']
    support = q['uv']
    center = np.median(support, axis=0)
    p = a + unit * center[0] + side * ds
    delta = p - s.pos[2]
    heading = float(np.degrees(np.arctan2(delta[0], -delta[1])) % 360)
    h, err2, _ = cal(s, 0, 2, c[:3], heading)
    s.rot = np.array([c[:3], c[3:], h[3:]])
    if len(err2) < 16 or np.median(abs(err2)) > 0.65:
        return None
    obs_hull = cv2.convexHull(support.astype('f'))
    W = max(48, round(L * 12))
    H = max(80, round(s.height[k] * 12))
    sx = (np.arange(W) + 0.5) / W * L
    sz = (1 - (np.arange(H) + 0.5) / H) * s.height[k]
    physical = a + side * ds + unit * sx[:, None]
    in_hull = np.zeros((H, W), 'B')
    pix = obs_hull.copy()
    pix[:, :, 0] *= W / L
    pix[:, :, 1] = (1 - pix[:, :, 1] / s.height[k]) * H
    cv2.fillConvexPoly(in_hull, pix.astype('i'), 255)
    in_hull = cv2.dilate(in_hull, np.ones((14, 14), 'B'))

    def warped(fi):
        d = physical - s.pos[fi]
        up = sz[:, None] - 2.3
        col = s.project(fi, d[:, 0], d[:, 1], up)
        im = s.im[fi]
        m = np.array(Image.open(D / (s.frames[fi]['id'][:8] + '-static.png')).convert('RGB'))
        s.im[fi] = m
        mask = s.project(fi, d[:, 0], d[:, 1], up)[:, :, 0] > 230
        s.im[fi] = im
        return (col, mask)
    A, ma = warped(0)
    B, mb = warped(1)
    A = cv2.GaussianBlur(A, (0, 0), 0.7)
    B = cv2.GaussianBlur(B, (0, 0), 0.7)
    diff = np.mean(abs(A.astype('f') - B), axis=2)
    d0 = cv2.blur(diff, (9, 9))
    mask, alpha, dense = supported_surface(s, physical, sz, side, A, B, ma & mb, in_hull)
    rgb = np.round((A.astype('f') + B) * 0.5).astype('B')
    mid = (s.pos[0] + s.pos[1]) * 0.5
    denom = (a - mid) @ side
    scale = (denom + ds) / denom
    coords = mid + (a + unit * sx[:, None] - mid) * scale
    xp = (coords - (a + side * ds)) @ unit / L * W - 0.5
    yp = (1 - (2.3 + (sz - 2.3) * scale) / s.height[k]) * H - 0.5
    mx, my = np.meshgrid(xp.astype('f'), yp.astype('f'))
    buf = cv2.remap(np.dstack((rgb, np.round(alpha * 255).astype('B'))), mx, my, cv2.INTER_LINEAR)
    dest = D / (site + '-wall.png')
    Image.fromarray(buf).save(dest)
    pitch = float(np.degrees(np.arctan2(center[1] - 2.3, np.linalg.norm(delta))))
    fov = 55
    size = (900, 600)
    yy, xx = np.mgrid[:size[1], :size[0]]
    xp = (xx + 0.5 - size[0] / 2) / (size[0] / (2 * np.tan(np.radians(fov / 2))))
    yp = -(yy + 0.5 - size[1] / 2) / (size[0] / (2 * np.tan(np.radians(fov / 2))))
    bear = np.radians(heading)
    pit = np.radians(pitch)
    drag = np.cos(pit) - yp * np.sin(pit)
    e = np.sin(bear) * drag + np.cos(bear) * xp
    so = -np.cos(bear) * drag + np.sin(bear) * xp
    up = np.sin(pit) + yp * np.cos(pit)
    ref = s.project(2, e, so, up)
    Image.fromarray(ref).save(D / (site + '-withheld.png'))
    orig = s.im[2]
    m = np.array(Image.open(D / (s.frames[2]['id'][:8] + '-static.png')).convert('RGB'))
    s.im[2] = m
    ref_mask = s.project(2, e, so, up)[:, :, 0] > 230
    s.im[2] = orig
    Image.fromarray(ref_mask.astype('B') * 255).save(D / (site + '-static-held.png'))
    n = side * np.sign((s.pos[2] - (a + b) / 2) @ side)
    material = dict(dense=dense, support_uv_m=support.tolist(), site=site, bi=s.idx[k][0], ei=s.idx[k][1], p0=(a / s.lam).tolist(), p1=(b / s.lam).tolist(), side=s.lam, normal=n.tolist(), height=s.height[k], ground_uv=(s.pos[0] / s.lam).tolist(), texture=str(dest), source_plane_offset=float(ds), mask_area_m2=round(float(np.mean(alpha > 0) * s.height[k] * L), 1), sequence=[f['id'] for f in s.frames], photographer=s.frames[0]['author'], withheld=s.frames[2], heading=heading, pitch=pitch, fov=fov, dimensions=size, options=options, corrections=s.rot.tolist(), yaw_flip=off, train_epi=float(np.median(abs(err))), test_epi=float(np.median(abs(err2))), heldout_pose_matches=len(err2), station_baseline=float(np.linalg.norm(s.pos[1] - s.pos[0])), agreement_pixels=float(np.mean(d0[mask > 0])), features=q['uniq'], hull_area=q['area'], outside_angle=52)
    (D / (site + '-wall.json')).write_text(json.dumps(material, indent=2))
    return material
if __name__ == '__main__':
    for site in ('iowa', 'minneapolis'):
        m = run(site)
        print(site, {k: m[k] for k in ('mask_area_m2', 'features', 'hull_area', 'train_epi', 'test_epi', 'heldout_pose_matches', 'heading', 'pitch')} if m else 'withheld on geometry / feature support')
