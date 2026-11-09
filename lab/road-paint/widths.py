"""Photographic cross-check of surveyed line width from co-planar transverse ratios.
Two outer KartaView views establish the scale independently of the middle view and the orthophoto PSF.
"""

from optics import *
from scipy.ndimage import gaussian_filter1d


def profile_width(gray, x, y, rad=10):
    x = int(round(x))
    y = int(y)
    row = gray[y]
    pad = max(rad, 20)
    low = max(x - pad, 0)
    high = min(x + pad + 1, len(row))
    region = gaussian_filter1d(row[low:high].astype(float), 0.6)
    if len(region) != 2 * pad + 1:
        return None
    ref = np.median(np.r_[region[:5], region[-5:]])
    j = pad - rad + np.argmax(region[pad - rad : pad + rad + 1])
    amp = region[j] - ref
    if amp < 12:
        return None
    threshold = ref + amp * 0.52
    a = b = j
    while a > 0 and region[a - 1] > threshold:
        a -= 1
    while b + 1 < len(region) and region[b + 1] > threshold:
        b += 1
    if a < 3 or b >= len(region) - 3 or b - a < 1:
        return None
    return ((a + b) / 2 + low, b - a + 1, amp)


def measure(site):
    geoms, _, a, r = load_site(site)
    m = ANNOT[site]
    dashes = [g for g in geoms if g["kind"] == "dash"]
    edges = [g for g in geoms if g["kind"] == "right_edge"]
    seps = []
    for g in dashes:
        p = Point(g["cen"])
        q = min(edges, key=lambda gg: LineString(gg["poly"]).distance(p))
        pt = nearest_points(p, LineString(q["poly"]))[1]
        n = abs((np.array(pt.coords[0]) - g["cen"]) @ r)
        if 2.65 < n < 4.35:
            seps.append(n)
    sep = np.median(seps)
    out = []
    for j, row in enumerate(m):
        im = np.array(Image.open(f"{site}/photo-{j}.jpg").convert("L"))
        ds = np.array(row["dash"])
        es = np.array(row["edge"])
        observations = []
        for y in np.arange(max(460, min(ds[:, 1]) + 3), min(601, max(ds[:, 1]) - 3), 1):
            line = np.interp(y, ds[::-1, 1], ds[::-1, 0])
            e = es[0, 0] + (y - es[0, 1]) * (es[1, 0] - es[0, 0]) / (
                es[1, 1] - es[0, 1]
            )
            val = profile_width(im, line, y, 8)
            val2 = profile_width(im, e, y, 11)
            if val is None or val2 is None:
                continue
            xc, wd, _ = val
            xe, we, _ = val2
            sep_photo = xe - xc
            if sep_photo < 45:
                continue
            observations.append((wd / sep_photo * sep, we / sep_photo * sep))
        arr = np.array(observations)
        print(site, j, len(arr), np.quantile(arr, [0.25, 0.5, 0.75], axis=0))
        out.append(
            {
                "frame": j,
                "used_rows": len(arr),
                "dash_m": round(float(np.median(arr[:, 0])), 3),
                "edge_m": round(float(np.median(arr[:, 1])), 3),
            }
        )
    (Path(site) / "width.json").write_text(
        json.dumps(
            {
                "site": site,
                "aerial_lane_spacing_m": float(sep),
                "independent_photo_estimates": out,
            },
            indent=2,
        )
    )


if __name__ == "__main__":
    for site in SITES:
        measure(site)
