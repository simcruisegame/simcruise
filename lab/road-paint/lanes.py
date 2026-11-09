"""Measure recurring longitudinal paint in the local curvilinear road frame.
The road geometry defines a search corridor. All accepted vertices follow the orthoimagery.
"""

from extract import *
from scipy.signal import find_peaks
from scipy.ndimage import gaussian_filter1d, maximum_filter1d, binary_closing, label
from shapely.geometry import box
import itertools
from scipy.spatial.distance import cdist


def run(site):
    epoch, others, lon, lat, heading = SITES[site]
    d = Path(site)
    meta = json.loads((d / (epoch + "-receipt.json")).read_text())
    x0, y0, x1, y1 = meta["bounds"]
    m = meta["m_per_px"]
    w, h = meta["size"]
    unit = (x1 - x0) / w

    def load(epoch):
        return np.array(
            Image.open(next(d.glob(epoch + ".[jp][pn][g]*"))).convert("RGB")
        )

    def pix(x, y):
        return (np.array(x) - x0) / unit, (y1 - np.array(y)) / unit

    def latlon(p):
        return np.round(
            np.stack(WGS_INV.transform(x0 + p[:, 0] * unit, y1 - p[:, 1] * unit), 1), 8
        ).tolist()

    v = json.loads((d / "vector.json").read_text())
    _, fid, line, props = find_segment(v, heading, [lon, lat])
    road = shapely.transform(line, pix, interleaved=False)
    lo, hi = road.project(Point(*pix(*WGS.transform(lon, lat)))) * m + np.array(
        [-78, 78]
    )
    s = np.arange(max(0.0, lo), min(road.length * m, hi), 0.125)
    l = np.arange(-5.7, 5.7, 0.0625)
    ax = shapely.line_interpolate_point(road, s / m)
    xy = np.array([shapely.get_x(ax), shapely.get_y(ax)]).T
    dir = np.gradient(xy, axis=0)
    dir /= np.linalg.norm(dir, axis=1)[:, None]
    right = np.stack([-dir[:, 1], dir[:, 0]], axis=1)
    xx = xy[:, 0, None] + right[:, 0, None] * l[None, :] / m
    yy = xy[:, 1, None] + right[:, 1, None] * l[None, :] / m
    inbounds = (xx >= 2) & (xx < w - 2) & (yy >= 2) & (yy < h - 2)

    def sample(image, shift=(0, 0)):
        return cv.remap(
            image.astype("float32"),
            (xx - shift[0]).astype("float32"),
            (yy - shift[1]).astype("float32"),
            cv.INTER_LINEAR,
            borderMode=cv.BORDER_CONSTANT,
        )

    base = load(epoch)
    init = json.loads((d / "observed.json").read_text())
    reg = init["registration"]
    fitreg = [reg[0]]
    holdreg = [item for item in reg if item not in fitreg][0]
    heat = []
    for k, shift in [(epoch, (0, 0))] + [
        (v["epoch"], v["translation_px"]) for v in fitreg
    ]:
        im = load(k)
        gray = cv.cvtColor(im, cv.COLOR_RGB2GRAY).astype(float)
        bg = cv.medianBlur(gray.astype("uint8"), 23).astype(float)
        hi = gray - bg
        # Suppress yellow median lines, saturation and dark textured roadside pixels.
        neutral = im.max(2).astype(float) - im.min(2).astype(float) < 45
        neutral &= im[:, :, 0].astype(float) - im[:, :, 2].astype(float) < 24
        valid = neutral & (gray > 60)
        heat.append(
            sample(
                np.where(valid, np.maximum(hi - np.maximum(6, bg * 0.047), 0), 0), shift
            )
            * inbounds
        )
    baseheat = heat[0]
    otherheat = np.stack([maximum_filter1d(v, size=7, axis=1) for v in heat[1:]]).max(0)
    # Fraction of longitudinal observations in independent years for each signed lateral offset.
    consensus = (baseheat >= 5) & (otherheat >= 3)
    den = np.maximum(inbounds.sum(0), 1)
    profile = gaussian_filter1d(consensus.sum(0) / den, 2)
    peaks, _ = find_peaks(profile, distance=round(1.5 / 0.0625), prominence=0.065)
    print(site, "peaks", [(round(l[p], 2), round(profile[p], 2)) for p in peaks])
    lines = []
    for peak in peaks:
        if abs(l[peak]) > 5.2:
            continue
        # Road geometry can drift around curves. Follow observed paint within a narrow band and require local physical continuity.
        band = np.where(abs(l - l[peak]) < 0.66)[0]
        cand = baseheat[:, band]
        guide = gaussian_filter1d(cand * (otherheat[:, band] >= 3), 1.1, axis=0)
        # Viterbi-like tracking of offset bounded by local derivative; coasts through unobserved marks; output always masked.
        scores = np.minimum(guide, 60.0)
        prev = np.empty_like(scores, dtype="int16")
        dp = scores[0]
        for j in range(1, len(s)):
            shifts = np.arange(-2, 3)
            adj = np.stack([np.roll(dp, move) - abs(move) * 2 for move in shifts])
            adj[0, -2:] = -1e6
            adj[1, -1:] = -1e6
            adj[-1, :2] = -1e6
            adj[-2, :1] = -1e6
            k = adj.argmax(0)
            dp = adj[k, np.arange(len(band))] + scores[j]
            prev[j] = np.arange(len(band)) - shifts[k]
        ix = np.zeros(len(s), int)
        ix[-1] = dp.argmax()
        for j in range(len(s) - 1, 0, -1):
            ix[j - 1] = prev[j, ix[j]]
        best = band[ix]
        bestpix = baseheat[np.arange(len(s)), best]
        support = otherheat[np.arange(len(s)), best]
        flag = (bestpix >= 7) & (support >= 3) & inbounds[np.arange(len(s)), best]
        flags = binary_closing(flag, structure=np.ones(3))
        groups, n = label(flags)
        for i in range(1, n + 1):
            indices = np.where(groups == i)[0]
            length = len(indices) * 0.125
            if len(indices) < 13:
                continue
            seglat = []
            widths = []
            for j in indices:
                center = best[j]
                a = center
                b = center
                threshold = max(5, baseheat[j, center] * 0.55)
                while a > 0 and baseheat[j, a - 1] > threshold:
                    a -= 1
                while b + 1 < len(l) and baseheat[j, b + 1] > threshold:
                    b += 1
                seglat.append(0.5 * (l[a] + l[b]))
                widths.append((b - a + 1) * 0.0625)
            width = float(np.median(widths))
            occupancy = flag[indices].mean()
            if not (0.075 <= width <= 0.34 and occupancy >= 0.84):
                continue
            linepoints = xy[indices] + right[indices] * np.array(seglat)[:, None] / m
            actual = (
                LineString(linepoints)
                .simplify(0.07 / m)
                .buffer(width / (2 * m), cap_style="flat", join_style="round")
                .intersection(box(1, 1, w - 1, h - 1))
            )
            centers = np.array(actual.centroid.coords)[0]
            if actual.geom_type != "Polygon":
                continue
            lines.append(
                (
                    float(l[peak]),
                    dict(
                        kind="line",
                        length_m=round(length, 2),
                        width_m=round(width, 3),
                        repeat_support=round(float(occupancy), 3),
                        polygon=latlon(np.array(actual.exterior.coords)[:-1]),
                        center_px=centers.tolist(),
                        station_m=round(float(s[indices].mean()), 2),
                        road_vector_id=fid,
                    ),
                )
            )
    good = []
    for l0, item in lines:
        # Long continuous edge or repeated physically observed dashes at the same lateral offset.
        ds = item["length_m"]
        s0 = item["station_m"]
        neighbors = [
            abs(other["station_m"] - s0)
            for n, other in lines
            if n == l0 and 1.72 <= other["length_m"] <= 5.8
        ]
        dash = (
            profile[np.argmin(abs(l - l0))] < 0.44
            and 1.72 <= ds <= 5.8
            and any(8 < g < 16 for g in neighbors)
        )
        # Left boundary may be a sunlit raised median. A right edge must be one lane outside measured dashes.
        right_edge = (
            ds >= 7.5
            and props.get("oneway") == 1
            and any(
                2.65 <= l0 - off <= 4.4 and 1.72 <= other["length_m"] <= 5.8
                for off, other in lines
            )
        )
        if dash or right_edge:
            item["kind"] = "dash" if dash else "right_edge"
            good.append(item)
    # Reinstate a narrow measured contour where longitudinal tracking fragmented it.
    # Distinct dates must support the contour, and its position must continue a repeatedly observed dash series.
    fitindex = 0
    regular = [q for q in good if q["kind"] == "dash"]
    for q0 in init["measured"]:
        if (
            not regular
            or q0["kind"] != "line"
            or not 0.70 <= q0["repeat_support"][fitindex]
            or not 2.3 <= q0["length_m"] <= 5.4
            or q0["width_m"] > 0.48
        ):
            continue
        c = np.array(q0["center_px"])
        delta = c - np.array([q["center_px"] for q in regular])
        nearest = np.argmin(np.linalg.norm(delta, axis=1))
        if np.min(np.linalg.norm(delta, axis=1)) * m < 1.2:
            continue
        q1 = regular[nearest]
        point = Point(c)
        station = road.project(point) * m
        closest = shapely.line_interpolate_point(road, station / m)
        tangent = dir[np.argmin(abs(s - station))]
        diff = delta[nearest] * m
        if not (
            8 < abs(diff @ tangent) < 16
            and abs(diff @ np.array([-tangent[1], tangent[0]])) < 0.48
        ):
            continue
        q = dict(q0)
        q["kind"] = "dash"
        q["width_m"] = round(q["area_m2"] / q["length_m"], 3)
        q["station_m"] = round(station, 2)
        q["repeat_support"] = q0["repeat_support"][fitindex]
        good.append(q)
    # A twin symbol station rejects vehicle and shoulder clutter. The third acquisition is withheld.
    allow = []
    for a, b in itertools.combinations(
        [
            o
            for o in init["measured"]
            if o["kind"] == "symbol"
            and 0.68 <= o["width_m"] <= 1.65
            and 2.3 <= o["length_m"] <= 4.8
            and 0.55 <= o["area_m2"] <= 2.2
            and o["repeat_support"][fitindex] > 0.84
        ],
        2,
    ):
        delta = (np.array(a["center_px"]) - np.array(b["center_px"])) * m
        axis = np.array([np.sin(np.deg2rad(heading)), -np.cos(np.deg2rad(heading))])
        if (
            abs(delta @ axis) < 0.70
            and 2.75 <= np.linalg.norm(delta) <= 4.25
            and abs(a["length_m"] - b["length_m"]) < 0.65
        ):
            allow.extend([a, b])
    good += list({tuple(q["center_px"]): q for q in allow}.values())
    # Score the third epoch independently at the georeferenced footprint. It was excluded from candidate selection.
    holdimg = load(holdreg["epoch"])
    holdbright, _ = bright(holdimg, np.ones((h, w), dtype=bool))
    holdimg = warp(holdbright, holdreg["translation_px"])
    dist = distance_transform_edt(holdimg == 0)
    for q in good:
        ll = np.array(q["polygon"]).T
        xx0, yy0 = pix(*WGS.transform(*ll))
        poly = np.round(np.stack([xx0, yy0], 1)).astype("int32")
        mask = np.zeros((h, w), np.uint8)
        cv.fillPoly(mask, [poly], 255)
        q["withheld_ortho_coverage_0p29m"] = round(
            float((dist[mask > 0] < 0.29 / m).mean()), 3
        )
    for q in good:
        print(q["kind"], q["length_m"], q["width_m"], np.round(q["center_px"], 1))
    preview = base.copy()
    for p in good:
        ll = np.array(p["polygon"]).T
        xp, yp = pix(*WGS.transform(*ll))
        cv.polylines(
            preview,
            [np.round(np.stack([xp, yp], 1)).astype("int32")],
            True,
            (250, 30, 50) if p["kind"] == "symbol" else (30, 230, 80),
            2,
        )
    Image.fromarray(preview).save(d / "accepted.jpg", quality=86)
    (d / "accepted.json").write_text(
        json.dumps(
            {
                "features": good,
                "epochs": [epoch] + others,
                "fit_epochs": [epoch, fitreg[0]["epoch"]],
                "withheld_epoch": holdreg["epoch"],
                "registration": reg,
                "station_mode": "registered-observations",
            },
            indent=2,
        )
    )


if __name__ == "__main__":
    for site in SITES:
        run(site)
