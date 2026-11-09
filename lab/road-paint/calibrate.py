"""Optical pose from two separate training images and measured dash endpoints/edge, withholding the middle camera."""

from widths import *


def calibrate(site):
    geoms, poses, a, r = load_site(site)
    cal = []
    dashes = [(i, g) for i, g in enumerate(geoms) if g["kind"] == "dash"]
    for j in [0, 2]:
        im = np.array(Image.open(f"{site}/photo-{j}.jpg").convert("L"))
        marks = ANNOT[site][j]
        ds = np.array(marks["dash"])
        es = np.array(marks["edge"])
        observed = []
        sfit = []
        for line, lower, upper in [
            (ds, min(ds[:, 1]) + 3, max(ds[:, 1]) - 3),
            (es, 465, 562),
        ]:
            pts = []
            for y in np.arange(lower, upper, 1.0):
                x = line[0, 0] + (y - line[0, 1]) * (line[1, 0] - line[0, 0]) / (
                    line[1, 1] - line[0, 1]
                )
                v = profile_width(im, x, y, 10)
                if v is not None:
                    pts.append([v[0], y])
            pts = np.array(pts)
            pp = np.polyfit(pts[:, 1], pts[:, 0], 1)
            sfit.append(pp)
            observed.append(pts)
        van_y = (sfit[1][1] - sfit[0][1]) / (sfit[0][0] - sfit[1][0])
        vp = np.array([np.polyval(sfit[0], van_y), van_y])
        targetEnds = np.stack([np.polyval(sfit[0], ds[:, 1]), ds[:, 1]], 1)
        # Actual near road tangent and edge polyline supplied by independent overhead imagery.
        pts = np.array(
            sorted(
                (g["cen"] for _, g in dashes),
                key=lambda p: np.linalg.norm(p - poses[j]),
            )[:5]
        )
        _, _, V = np.linalg.svd(pts - pts.mean(0))
        d = V[0] if V[0] @ a > 0 else -V[0]
        n = np.array([d[1], -d[0]])
        edg = []
        for g in geoms:
            if g["kind"] != "right_edge":
                continue
            ring = g["poly"]
            st = ring @ d
            for ss in np.arange(st.min() + 0.15, st.max(), 0.75):
                clip = LineString([ss * d - 240 * n, ss * d + 240 * n]).intersection(
                    Polygon(ring)
                )
                if clip.is_empty:
                    continue
                c = np.array(clip.centroid.coords[0])
                if -2 < (c - poses[j]) @ d < 40:
                    edg.append(c)
        edg = np.array(edg)
        _, _, V = np.linalg.svd(edg - edg.mean(0))
        v = V[0] if V[0] @ a > 0 else -V[0]
        anchor = edg.mean(0)
        E = anchor + ((poses[j] - anchor) @ v + np.array([[15], [40]])) * v
        cal.append((j, targetEnds, E, sfit[1], vp, d))
    if site == "key":
        control = json.loads(Path("key/pole-control.json").read_text())
        photo_ground = control["road_elevations_at_three_geotags_m"]
    p0 = np.array([7, -11.0, 72.0, 1.4, -1.6, -0.2, -1.6, -0.2])

    def residual(p, ids):
        yaw, pitch, fov, h, sideA, distA, sideC, distC = p
        rr = []
        for k, (j, ds, E, ef, vp, d) in enumerate(cal):
            pars = np.array(
                [yaw, pitch, fov, *([(sideA, distA), (sideC, distC)][k]), h]
            )
            g = geoms[ids[k]]
            got = project(g["ends"], poses[j], a, r, pars)
            yp = project(poses[j][None, :] + 5000 * d, poses[j], a, r, pars)[0]
            if site == "cambridge" and j == 0:
                rr.extend(got[1] - ds[1])
            else:
                rr.extend((got - ds).ravel())
            e = project(E, poses[j], a, r, pars)
            pp = np.polyfit(e[:, 1], e[:, 0], 1)
            targets = np.array([485, 520, 551])
            rr.extend((np.polyval(pp, targets) - np.polyval(ef, targets)) * 0.7)
            rr.extend((yp - vp) * 0.6)
            if site == "key":
                for location, z, photo in [
                    ("center_plan_m", "base_z_m", [(461, 438), (618, 453)]),
                    ("top_plan_m", "top_z_m", [(455, 246), (584, 28)]),
                ]:
                    v = pars.copy()
                    v[-1] = h - (control[z] - photo_ground[j])
                    uv = project(np.array([control[location]]), poses[j], a, r, v)[0]
                    rr.extend((uv - photo[k]) * 0.70)
            if site == "key" and j == 0:
                # Fixed, non-periodic image tie. Both measured parallel arrowheads are visible ahead in outer photo 20.
                ar = sorted(
                    (
                        project(q["cen"][None, :], poses[j], a, r, pars)[0]
                        for q in geoms
                        if q["kind"] == "symbol"
                    ),
                    key=lambda center: center[0],
                )
                rr.extend(
                    ((np.array(ar) - np.array([[284, 450], [360, 447]])) * 0.75).ravel()
                )
        # Variable endpoint GPS errors, focal and height otherwise unmeasured. These weak priors do not constrain the validation view.
        rr.extend(
            [
                (fov - 74) / 7,
                (h - 1.5) * 7,
                sideA * 0.5,
                sideC * 0.5,
                distA * 0.65,
                distC * 0.65,
                (sideA - sideC) * 1.2,
                (distA - distC) * 1.7,
            ]
        )
        return np.nan_to_num(rr, nan=200)

    def ids(j):
        rows = []
        for i, g in dashes:
            c = (g["cen"] - poses[j]) @ a
            if 3 < c < 38:
                rows.append(i)
        return rows

    fits = []
    for ii in itertools.product(ids(0), ids(2)):
        obj = least_squares(
            residual,
            p0,
            args=(ii,),
            bounds=(
                [-16, -25, 48, 0.9, -4.6, -8, -4.6, -8],
                [24, 0, 102, 2.2, 4.6, 8, 4.6, 8],
            ),
            loss="soft_l1",
            f_scale=3.5,
            max_nfev=100,
        )
        fits.append((float(np.mean(np.minimum(abs(obj.fun), 80))), ii, obj.x))
    fits.sort(key=lambda z: z[0])
    score, idx, p = fits[0]
    yaw, pitch, fov, h, sa, aa, sc, ac = p
    pars = [yaw, pitch, fov, (sa + sc) / 2, (aa + ac) / 2, h]
    targets = ANNOT[site][1]
    ds = np.array(targets["dash"])
    dc = []
    symbol = []
    for i, g in enumerate(geoms):
        uv = project(g["ends"], poses[1], a, r, pars)
        if np.isfinite(uv).all() and g["kind"] == "dash":
            dc.append((float(np.linalg.norm(ds - uv, axis=1).mean()), i))
        if g["kind"] == "symbol":
            symbol.append(project(g["cen"][None, :], poses[1], a, r, pars)[0])
    metric = {"nearest_dash_mean_endpoint_error_px": min(dc)[0], "dash_id": min(dc)[1]}
    if symbol:
        s = sorted(symbol, key=lambda x: x[0])
        metric["arrow_centroids_px"] = [q.tolist() for q in s]
        metric["mean_arrow_centroid_error_px"] = float(
            np.linalg.norm(np.array(s) - targets["symbols"], axis=1).mean()
        )
    result = {
        "model": "shared focal, yaw, pitch, eye height; independent outer GPS offsets; middle offset linear interpolation",
        "training_images": [0, 2],
        "middle_withheld": True,
        "pixel_size": [820, 615],
        "parameters": pars,
        "outer_offsets_m": [[sa, aa], [sc, ac]],
        "training_loss": score,
        "outer_vanishing_pts_px": [k[-2].tolist() for k in cal],
        "correspondences": idx,
        "heldout": metric,
        "runner_up_training_loss": fits[1][0],
    }
    (Path(site) / "camera-fit.json").write_text(json.dumps(result, indent=2))
    print(site, result, flush=True)


if __name__ == "__main__":
    for site in SITES:
        calibrate(site)
