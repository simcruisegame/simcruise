import numpy as np
from PIL import Image
from pyproj import Transformer
from scipy.ndimage import binary_erosion
from scipy.spatial import cKDTree


def finish(doc, p, site, directory, sphere):
    outer = site["training"]
    rec = doc["roofs"][0]
    raw = np.array(rec["v"])
    origin = 20037508.342789244
    step = origin * 2 / 65536
    tile = np.array(doc["tile"])
    uv = raw[:, :2] / 1e5 + tile
    tr = Transformer.from_crs(3857, site["metric_crs"], always_xy=True)
    x, y = tr.transform(uv[:, 0] * step - origin, origin - uv[:, 1] * step)
    base = rec["qa"]["local_ground_datum_m"]
    v = np.c_[x, y, base + raw[:, 2] / 100]
    f = np.array(rec["f"]).reshape(-1, 3)
    tri = v[f]
    center = tri.mean(1)
    N = np.cross(tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0])
    nz = abs(N[:, 2]) / np.maximum(0.0001, np.linalg.norm(N, axis=1))
    # 1 confirmed roof, 2 steep surface, 3 independently identified spherical cap.
    group = np.where(nz < 0.42, 2, 1)
    if sphere:
        c = np.array(sphere["center_metric"])
        radius = sphere["radius"]
        is_cap = (abs(np.linalg.norm(center - c, axis=1) - radius) < 0.6) & (
            center[:, 2] > c[2] + 0.20 * radius
        )
        group[is_cap] = 3
    raw_faces = list(f)
    colors = list(group)
    for a, b in np.array(rec["edge"]).reshape(-1, 2):
        va, vb = v[a], v[b]
        va0 = va.copy()
        vb0 = vb.copy()
        va0[2] = vb0[2] = base
        i = len(v)
        v = np.vstack((v, va0, vb0))
        raw_faces.extend([[a, b, i + 1], [a, i + 1, i]])
        colors.extend([2, 2])
    f = np.array(raw_faces)
    labels = np.array(colors)
    g = p[p[:, 3] == 2]
    ground = cKDTree(g[:, :2])
    wgs = Transformer.from_crs(4326, site["metric_crs"], always_xy=True)
    fov, yawoff, poff, de, dn = site["camera_fit"]
    train = {}

    def samples(fr):
        img = np.array(
            Image.open(directory / fr["rgb"])
            .convert("RGB")
            .resize((513, 385), Image.Resampling.BILINEAR)
        )
        h, w = img.shape[:2]
        mask = np.array(
            Image.open(directory / fr["mask"]).resize((w, h), Image.Resampling.NEAREST)
        )
        pos = np.array(wgs.transform(fr["lon"], fr["lat"]))
        height = g[ground.query(pos, k=10)[1], 2].mean() + 1.65
        dv = v[:, :2] - pos - [de, dn]
        hd = np.deg2rad(fr["heading"] + yawoff)
        pt = np.deg2rad(poff)
        front = dv[:, 0] * np.sin(hd) + dv[:, 1] * np.cos(hd)
        right = dv[:, 0] * np.cos(hd) - dv[:, 1] * np.sin(hd)
        up = v[:, 2] - height
        z = front * np.cos(pt) - up * np.sin(pt)
        pix = np.c_[
            (w / 2) + (w / 2 / np.tan(np.deg2rad(fov / 2))) * right / z,
            h / 2
            - (w / 2 / np.tan(np.deg2rad(fov / 2)))
            * (up * np.cos(pt) + front * np.sin(pt))
            / z,
        ]
        zbuf = np.full((h, w), np.inf)
        gbuf = np.zeros((h, w), int)
        for i, abc in enumerate(f):
            if min(z[abc]) < 3:
                continue
            uv = pix[abc]
            xmin, ymin = np.maximum(0, np.floor(uv.min(0)).astype(int))
            xmax, ymax = np.minimum([w - 1, h - 1], np.ceil(uv.max(0)).astype(int))
            if xmin > xmax or ymin > ymax:
                continue
            va, vb, vc = uv
            ba = vb - va
            ca = vc - va
            det = ba[0] * ca[1] - ba[1] * ca[0]
            if abs(det) < 0.03:
                continue
            yy, xx = np.mgrid[ymin : ymax + 1, xmin : xmax + 1]
            dx = xx + 0.5 - va[0]
            dy = yy + 0.5 - va[1]
            b = (dx * ca[1] - dy * ca[0]) / det
            c = (ba[0] * dy - ba[1] * dx) / det
            a = 1 - b - c
            take = (a >= 0) & (b >= 0) & (c >= 0)
            depth = 1 / (a / z[abc[0]] + b / z[abc[1]] + c / z[abc[2]])
            zz = zbuf[ymin : ymax + 1, xmin : xmax + 1]
            take &= depth < zz
            zz[take] = depth[take]
            gbuf[ymin : ymax + 1, xmin : xmax + 1][take] = labels[i]
        sel = (mask == 2) & (img.max(2) < 245) & (img.min(2) > 15)
        return {
            str(i): img[binary_erosion((gbuf == i) & sel, iterations=2)]
            for i in (1, 2, 3)
        }

    for fr in outer:
        pix = samples(fr)
        train[str(fr["sequenceIndex"])] = {
            k: {
                "n": len(vv),
                "median_srgb": np.median(vv, axis=0).astype(int).tolist()
                if len(vv) > 9
                else None,
            }
            for k, vv in pix.items()
        }
        print(fr["sequenceIndex"], train[str(fr["sequenceIndex"])], flush=True)
    colors = {}
    for code in ["1", "2", "3"]:
        a, b = [train[str(v["sequenceIndex"])][code] for v in outer]
        if (
            min(a["n"], b["n"]) < 24
            or max(abs(np.array(a["median_srgb"]) - b["median_srgb"])) > 24
        ):
            continue
        colors[code] = (
            np.rint(np.median([a["median_srgb"], b["median_srgb"]], axis=0))
            .astype(int)
            .tolist()
        )
    rec["face_finish"] = "".join(str(n) for n in group)
    rec["color_observations"] = colors
    return dict(samples=train, accepted=colors)
