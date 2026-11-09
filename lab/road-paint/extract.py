"""Conservative repeated road paint observations from registered airborne surveys."""

import argparse, json, sys
from pathlib import Path
import cv2 as cv
import numpy as np
from PIL import Image
from shapely.geometry import shape, Point, Polygon, LineString, MultiLineString
from shapely.ops import nearest_points
import shapely
from pyproj import Transformer
from scipy.ndimage import distance_transform_edt, map_coordinates, binary_dilation

WGS = Transformer.from_crs(4326, 3857, always_xy=True)
WGS_INV = Transformer.from_crs(3857, 4326, always_xy=True)
H = 20037508.342789244


def find_segment(doc, camera, latlon):
    loc = Point(*WGS.transform(*latlon))
    d = np.array([np.sin(np.deg2rad(camera)), -np.cos(np.deg2rad(camera))])
    candidates = []
    for r in doc["road"]:
        props = r["properties"]
        if props["class"] not in [
            "trunk",
            "primary",
            "secondary",
            "tertiary",
            "minor",
            "motorway",
        ]:
            continue
        g = shapely.transform(shape(r["geometry"]), WGS.transform, interleaved=False)
        for ln in [g] if g.geom_type == "LineString" else g.geoms:
            ds = ln.project(loc)
            gap = ln.distance(loc)
            a, b = (
                ln.interpolate(max(0, ds - 2)),
                ln.interpolate(min(ln.length, ds + 2)),
            )
            v = np.array([b.x - a.x, a.y - b.y])
            v /= max(np.linalg.norm(v), 1e-9)
            alignment = v @ d
            if props.get("oneway", 0) == -1:
                alignment = -alignment
            if props.get("oneway", 0) == 0:
                alignment = abs(alignment)
            if alignment > 0.82:
                candidates.append(
                    (gap + 12 * (1 - alignment), int(r.get("id", 0)), ln, props)
                )
    return min(candidates, key=lambda h: h[0])


def bright(im, mask):
    gray = cv.cvtColor(im, cv.COLOR_RGB2GRAY)
    bg = cv.medianBlur(gray, 23).astype(float)
    high = gray.astype(float) - bg
    neutral = np.ptp(im.astype(float), axis=2) < 55
    return (
        mask & (high > np.maximum(9.0, bg * 0.073)) & neutral & (gray >= 66)
    ).astype("uint8"), high


def warp(image, dxy):
    return cv.warpAffine(
        image,
        np.float32([[1, 0, dxy[0]], [0, 1, dxy[1]]]),
        (image.shape[1], image.shape[0]),
        flags=cv.INTER_NEAREST,
        borderMode=cv.BORDER_CONSTANT,
    )


def register(a, b, roi, scale):
    best = (0, None)
    aa = (a > 0) & roi
    bb = b > 0
    for x in range(-8, 9):
        for y in range(-8, 9):
            if np.hypot(x, y) * scale > 1.05:
                continue
            moved = warp(bb.astype("uint8"), (x, y)) > 0
            intersection = np.sum(aa & moved)
            score = 2 * intersection / max(1, (aa.sum() + (moved & roi).sum()))
            if score > best[0]:
                best = score, (x, y)
    return best[1], best[0]


def process(folder, epoch, others, frame):
    d = Path(folder)
    ref = json.loads((d / (epoch + "-receipt.json")).read_text())
    w, h = ref["size"]
    bounds = np.array(ref["bounds"])
    unit = (bounds[2] - bounds[0]) / w
    m = ref["m_per_px"]

    def pix(x, y):
        return (np.array(x) - bounds[0]) / unit, (bounds[3] - np.array(y)) / unit

    def world(p):
        p = np.asarray(p)
        return np.c_[bounds[0] + p[:, 0] * unit, bounds[3] - p[:, 1] * unit]

    def load(k):
        p = d / (k + ".jpg")
        if not p.exists():
            p = d / (k + ".png")
        return np.array(Image.open(p).convert("RGB"))

    latlon = (frame["lon"], frame["lat"])
    selected = find_segment(
        json.loads((d / "vector.json").read_text()), frame["heading"], latlon
    )
    _, fid, road, props = selected
    row = shapely.transform(road, pix, interleaved=False)
    wide = np.zeros((h, w), np.uint8)
    cv.polylines(
        wide, [np.round(row.coords).astype("int32")], False, 255, int(10.9 / m)
    )
    inside = wide > 0
    mask = np.zeros((h, w), np.uint8)
    cv.polylines(mask, [np.round(row.coords).astype("int32")], False, 255, int(9.6 / m))
    road_roi = mask > 0
    ys, xs = np.where(road_roi)
    print("selected", fid, props, "width", round(m, 3))
    base_im = load(epoch)
    base, score = bright(base_im, inside)
    supports = []
    reg = []
    for other in others:
        comp, _ = bright(load(other), inside)
        shift, ncc = register(base, comp, road_roi, m)
        aligned = warp(comp, shift)
        dist = distance_transform_edt(~(aligned > 0))
        supports.append(dist <= 0.29 / m)
        reg.append(dict(epoch=other, translation_px=list(shift), dice=ncc))
    candidate = base * road_roi
    num, lbl, stats, cens = cv.connectedComponentsWithStats(candidate, connectivity=8)
    measured = []
    preview = base_im.copy()
    for i in range(1, num):
        component = lbl == i
        area_px = int(stats[i, cv.CC_STAT_AREA])
        if area_px < 8:
            continue
        ys, xs = np.where(component)
        pts = np.c_[xs, ys].astype("float32")
        rect = cv.minAreaRect(pts)
        length, width = sorted([(v + 1) * m for v in rect[1]], reverse=True)
        coverage = [float(s[component].mean()) for s in supports]
        if max(coverage) < 0.76:
            continue
        if not (0.7 < length < 140 and 0.075 < width < 2.6):
            continue
        # Classify narrow lines, wide arrow-like shapes. Neither class substitutes a synthetic stencil.
        hull = cv.convexHull(pts)
        solidity = area_px / (cv.contourArea(hull) + area_px * 0.03)
        vx, vy = cv.fitLine(pts, cv.DIST_L2, 0, 0.01, 0.01).ravel()[:2]
        cross_alignment = abs(
            vx * np.sin(np.deg2rad(frame["heading"]))
            - vy * np.cos(np.deg2rad(frame["heading"]))
        )
        if cross_alignment < 0.93:
            continue
        if width > 0.55 and (not 1.8 < length < 6.3 or not 0.31 < solidity < 0.89):
            continue
        if width < 0.55 and length / width < 3.2:
            continue
        kind = "symbol" if width > 0.55 else "line"
        ctrs, _ = cv.findContours(
            component.astype("uint8"), cv.RETR_EXTERNAL, cv.CHAIN_APPROX_SIMPLE
        )
        contour = max(ctrs, key=cv.contourArea)
        cp = cv.approxPolyDP(contour, 0.62, True).reshape(-1, 2)
        if len(cp) < 3:
            continue
        # Pixels sample area. Include a half-pixel rim so one-pixel-wide surveyed lines have physical width.
        geom = Polygon(cp).buffer(0.43, join_style="mitre").simplify(0.55)
        if geom.geom_type != "Polygon" or geom.area < 1:
            continue
        rings = np.array(geom.exterior.coords)[:-1]
        coords = world(rings)
        lon, lat = WGS_INV.transform(coords[:, 0], coords[:, 1])
        data = dict(
            kind=kind,
            polygon=np.round(np.c_[lon, lat], 8).tolist(),
            length_m=round(length, 2),
            width_m=round(width, 2),
            area_m2=round(area_px * m * m, 2),
            repeat_support=[round(v, 3) for v in coverage],
            center_px=np.mean(pts, axis=0).tolist(),
            road_vector_id=fid,
        )
        measured.append(data)
        cv.polylines(
            preview,
            [cp.astype("int32")],
            True,
            (250, 25, 40) if kind == "symbol" else (0, 240, 80),
            2,
        )
    (d / "observed.json").write_text(
        json.dumps(
            dict(measured=measured, registration=reg, primary=epoch, epochs=others),
            indent=2,
        )
    )
    cv.imwrite(str(d / "overlay.jpg"), cv.cvtColor(preview, cv.COLOR_RGB2BGR))
    print(
        folder,
        reg,
        "markers",
        [
            (
                q["kind"],
                q["length_m"],
                q["width_m"],
                q["center_px"],
                q["repeat_support"],
            )
            for q in measured
        ],
    )


SURVEYS = json.loads((Path(__file__).resolve().parent / "sites.json").read_text())
SITES = {
    name: (q["primary"], [q["repeat"], q["withheld"]], q["lon"], q["lat"], q["bearing"])
    for name, q in SURVEYS.items()
}

if __name__ == "__main__":
    for location, (primary, other, lon, lat, bearing) in SITES.items():
        process(location, primary, other, dict(lon=lon, lat=lat, heading=bearing))
