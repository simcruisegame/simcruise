"""Build checked roof and ground packets from a pinned district point-cloud leaf."""

import argparse
import hashlib
import json
import math
from pathlib import Path
from urllib.request import Request, urlopen

import laspy
import numpy as np
import shapely
from scipy.interpolate import RegularGridInterpolator
from scipy.optimize import least_squares
from scipy.sparse.csgraph import connected_components
from scipy.spatial import Delaunay, cKDTree
from shapely.geometry import LineString, Point, Polygon
from shapely.ops import nearest_points


def generate(leaf, destination):
    source_dir = Path(__file__).resolve().parent
    settings = json.loads((source_dir / "source.json").read_text())
    maptile = json.loads((source_dir / "base-tile.json").read_text())
    if hashlib.sha256(leaf.read_bytes()).hexdigest() != settings["sha256"]:
        raise ValueError("The source laser leaf changed")
    las = laspy.read(leaf)
    tx, ty = maptile["id"]
    latitude = settings["local_origin"]["latitude"]
    longitude = settings["local_origin"]["longitude"]
    side = 611.496226 * math.cos(math.radians(latitude))
    cx = (longitude + 180) / 360 * 65536 - tx
    cy = (1 - math.asinh(math.tan(math.radians(latitude))) / math.pi) * 32768 - ty
    source = {
        "survey": settings["id"],
        "ept": settings["ept"],
        "acquisition": settings["acquisition"],
    }
    candidates = settings["candidate_indices"]
    tw = 40075016.68557849 / 65536
    xy = np.c_[
        ((np.asarray(las.x) + 20037508.342789244) / tw - tx - cx) * side,
        (np.asarray(las.y) / tw - 32768 + ty + cy) * side,
    ]
    z = np.asarray(las.z)
    cls = np.asarray(las.classification)
    cloud = np.c_[xy, z]
    poly = []
    for bldg in maptile["buildings"]:
        poly.append(Polygon((np.array(bldg[0][0]) - [cx, cy]) * [side, -side]))

    def check(p):
        shapely.contains_xy(p.buffer(9), *xy.T)
        ring = shapely.contains_xy(p.buffer(9).difference(p.buffer(0.8)), *xy.T) & (
            cls == 2
        )
        mg = np.c_[xy, np.ones(len(xy))]
        g = ring.copy()
        if g.sum() < 60:
            return {"reason": "low ground support"}
        for _ in range(3):
            gp = np.linalg.lstsq(mg[g], z[g], rcond=None)[0]
            g = ring & (np.abs(z - mg @ gp) < 0.25)
        h = z - mg @ gp
        core = shapely.contains_xy(p.buffer(-0.8), *xy.T)
        elevated = core & (cls != 2) & (h > 1.5) & (h < 25)
        if elevated.sum() < 60:
            return {
                "reason": "few elevated returns",
                "ground": int(g.sum()),
                "elevated": int(elevated.sum()),
                "total_inside": int(core.sum()),
            }
        # Robust consensus bounded to plausible commercial roof slopes, independent of image interpretation.
        ids = np.flatnonzero(elevated)
        cell = np.floor(xy / 1.5).astype(int)
        hold = (cell[:, 0] + 2 * cell[:, 1]) % 5 == 0
        train = ids[~hold[ids]]
        test = ids[hold[ids]]
        best = np.zeros(len(train), dtype=bool)
        rng = np.random.default_rng(1210)
        for three in rng.choice(train, (400, 3)):
            if np.linalg.cond(mg[three]) > 1e6:
                continue
            rp = np.linalg.solve(mg[three], h[three])
            ok = abs(h[train] - mg[train] @ rp) < 0.28
            if np.linalg.norm(rp[:2]) < 0.7 and ok.sum() > best.sum():
                best = ok
        if best.sum() < 40 or len(test) < 20:
            return {"reason": "small independent sample"}
        rp = np.linalg.lstsq(mg[train[best]], h[train[best]], rcond=None)[0]
        err = abs(h[test] - mg[test] @ rp)
        test[err < 0.28]
        support = train[best]
        # Whole polygon area vs convex hull alone would hide gaps. Count occupied eligible 2m cells.

        x0, y0, x1, y1 = p.bounds
        eligible = 0
        covered = 0
        kd = cKDTree(xy[support])
        for xx in np.arange(x0, x1, 2):
            for yy in np.arange(y0, y1, 2):
                if p.buffer(-0.8).contains(Point(xx + 1, yy + 1)):
                    eligible += 1
                    covered += int(kd.query((xx + 1, yy + 1))[0] < 1.7)
        return {
            "ground": int(g.sum()),
            "roof_class": int((core & (cls == 6)).sum()),
            "all_inside": int(core.sum()),
            "elevated": len(ids),
            "train": len(train),
            "train_plane_fraction": round(float(best.mean()), 3),
            "holdout": len(test),
            "holdout_fraction": round(float((err < 0.28).mean()), 3),
            "holdout_p50": round(float(np.median(err)), 3),
            "holdout_p90": round(float(np.percentile(err, 90)), 3),
            "core_cell_coverage": round(covered / max(1, eligible), 3),
            "ground_spatial_rms": round(float(np.std(z[g] - mg[g] @ gp)), 3),
            "median_plane_ground_height_m": round(
                float(np.median(mg[support] @ rp)), 2
            ),
            "height_p10_p90_all": np.round(np.percentile(h[ids], [10, 90]), 2).tolist(),
            "slope": round(float(np.linalg.norm(rp[:2])), 3),
            "classes": dict(
                zip(
                    map(str, np.unique(cls[core]).tolist()),
                    map(int, np.unique(cls[core], return_counts=True)[1]),
                )
            ),
        }

    receipt = {str(i): check(poly[i]) for i in candidates}
    doc = {
        "schema": 1,
        "tile": [tx, ty],
        "source": "USGS 3D Elevation Program, OK_Statewide_1_D22",
        "source_url": source["ept"],
        "acquired": source["acquisition"],
        "roofs": [],
    }
    floor_geometry = np.c_[xy, np.ones(len(xy))]
    cell = np.floor(xy / 1.5).astype(int)
    hold = (cell[:, 0] + 2 * cell[:, 1]) % 5 == 0

    def profile(i):
        p = poly[i]
        q = receipt[str(i)]
        if q.get("core_cell_coverage", 0) < 0.9 or q.get("holdout_fraction", 0) < 0.8:
            return
        raw_ring = maptile["buildings"][i][0][0]
        ring = np.asarray(p.exterior.coords)[:-1]
        core = shapely.contains_xy(p.buffer(0.3), *xy.T)
        ringground = shapely.contains_xy(
            p.buffer(9).difference(p.buffer(0.8)), *xy.T
        ) & (cls == 2)
        g = ringground.copy()
        for _ in range(3):
            gp = np.linalg.lstsq(floor_geometry[g], z[g], rcond=None)[0]
            g = ringground & (abs(z - floor_geometry @ gp) < 0.25)
        h = z - floor_geometry @ gp
        ok = core & (cls != 2) & (h > 1.5) & (h < 18)
        use = ok & ~hold
        test = ok & hold
        cloud_index = np.flatnonzero(use)
        kd = cKDTree(xy[use])
        median = np.median(h[use])
        baseuse = use & (abs(h - median) < 0.5)
        reg = np.linalg.lstsq(floor_geometry[baseuse], h[baseuse], rcond=None)[0]
        flat = q["holdout_fraction"] > 0.97
        size = 1.05
        perim = []
        for a, b in zip(ring, np.roll(ring, -1, axis=0)):
            cnt = max(1, int(np.ceil(np.linalg.norm(b - a) / size)))
            perim.extend(a + t / cnt * (b - a) for t in range(cnt))
        gx, gy, gx2, gy2 = p.bounds
        points = list(perim)
        for xx in np.arange(gx + size / 2, gx2, size):
            for yy in np.arange(gy + size / 2, gy2, size):
                if p.buffer(-0.55).contains(Point(xx, yy)):
                    points.append([xx, yy])
        points = np.asarray(points)
        eaves = np.c_[points, np.ones(len(points))] @ reg
        heights = eaves.copy()
        if not flat:
            for k, pt in enumerate(points):
                count = kd.query_ball_point(pt, 1.45)
                vv = h[cloud_index[count]]
                if len(vv) >= 3:
                    # A high isolated tree or chimney does not establish a roof surface.
                    high = float(np.percentile(vv, 70))
                    neighbors = vv[abs(vv - high) < 0.55]
                    if len(neighbors) >= 3 and high > eaves[k] + 0.27:
                        heights[k] = np.median(neighbors)
        tris = Delaunay(points)
        faces = [
            f for f in tris.simplices if p.buffer(0.007).covers(Polygon(points[f]))
        ]
        if not faces:
            return
        # Check the withheld spatial cells against the actual exported triangles, including the raised entrance.
        ids = np.flatnonzero(test)
        simplex = tris.find_simplex(xy[ids])
        allow = {tuple(sorted(t)) for t in faces}
        err = []
        upper = []
        control = []
        roofheight = []
        tested = []
        for j, t in zip(ids, simplex):
            if t < 0 or tuple(sorted(tris.simplices[t])) not in allow:
                continue
            transform = tris.transform[t]
            bary = transform[:2] @ (xy[j] - transform[2])
            hh = np.r_[bary, 1 - bary.sum()] @ heights[tris.simplices[t]]
            e = abs(hh - h[j])
            err.append(e)
            control.append(abs(5.0 - h[j]))
            tested.append(int(j))
            if h[j] > np.r_[xy[j], 1.0] @ reg + 1:
                upper.append(e)
                roofheight.append(h[j])
        assert len(err) > 60
        uv = points / [side, -side] + [cx, cy]
        verts = np.c_[uv * 100000, heights * 100].round().astype(int)
        # The airborne material alone cannot constrain the roof's visible wall finish.
        classes = "0" * len(faces)
        palette = {}
        edge = np.c_[np.arange(len(perim)), np.roll(np.arange(len(perim)), -1)].ravel()
        qa = {
            "independent_height_cells": q,
            "withheld_mesh_points": len(err),
            "mesh_p50_p90_cm": np.rint(np.percentile(err, [50, 90]) * 100)
            .astype(int)
            .tolist(),
            "baseline_p50_p90_cm": np.rint(np.percentile(control, [50, 90]) * 100)
            .astype(int)
            .tolist(),
            "raised_holdout_points": len(upper),
            "raised_p50_p90_cm": np.rint(np.percentile(upper, [50, 90]) * 100)
            .astype(int)
            .tolist()
            if upper
            else [],
            "raised_baseline_p50_p90_cm": np.rint(
                np.percentile(np.abs(np.array(roofheight) - 5), [50, 90]) * 100
            )
            .astype(int)
            .tolist()
            if upper
            else [],
            "highest_mesh_m": round(max(heights), 2),
            "raised_airborne_color_faces": 0,
        }
        sourcekey = hashlib.sha256(
            np.asarray(raw_ring, dtype=np.float64).tobytes()
        ).hexdigest()
        return {
            "reference": "USGS " + source["survey"] + " " + sourcekey[:10],
            "footprint": [(np.asarray(raw_ring) * 100000).round().astype(int).tolist()],
            "eave_m": float(np.percentile(eaves, 5)),
            "v": verts.tolist(),
            "f": np.asarray(faces).ravel().astype(int).tolist(),
            "edge": edge.astype(int).tolist(),
            "qa": qa,
            "face_finish": classes if palette else "",
            "color_observations": palette,
        }

    for i in candidates:
        candidate = profile(i)
        if candidate:
            doc["roofs"].append(candidate)
    input_doc = doc
    for i, rec in zip(candidates, input_doc["roofs"]):
        p = poly[i]
        vert = np.array(rec["v"], float)
        xx = (vert[:, :2] / 100000 - [cx, cy]) * [side, -side]
        np.asarray(p.exterior.coords)[:-1]
        hull = p.buffer(2.9)
        inside = shapely.contains_xy(hull, *xy.T)
        g = shapely.contains_xy(p.buffer(9).difference(p.buffer(0.8)), *xy.T) & (
            cls == 2
        )
        gp = np.linalg.lstsq(floor_geometry[g], z[g], rcond=None)[0]
        for _ in range(3):
            gg = g & (abs(z - floor_geometry @ gp) < 0.25)
            gp = np.linalg.lstsq(floor_geometry[gg], z[gg], rcond=None)[0]
        height = z - floor_geometry @ gp
        elevated = (
            inside
            & (cls != 2)
            & (height > rec["eave_m"] + 1.4)
            & (height < rec["eave_m"] + 9)
        )
        ss = np.flatnonzero(elevated & ~hold)
        kd = cKDTree(xy[ss]) if len(ss) else None
        if not kd:
            continue
        count, lab = connected_components(
            kd.sparse_distance_matrix(kd, 1.8, output_type="coo_matrix").tocsr(),
            directed=False,
        )
        for k in range(count):
            ids = ss[lab == k]
            if len(ids) < 40:
                continue
            pts = xy[ids]
            zz = height[ids]
            xb, yb = pts.min(0)
            xt, yt = pts.max(0)
            if not (3 < xt - xb < 14 and 3 < yt - yb < 14):
                continue

            def model(v, c):
                xo, yo, H, mx, my = v
                return H - np.maximum(abs(c[:, 0] - xo) * mx, abs(c[:, 1] - yo) * my)

            peak = pts[np.argmax(zz)]
            start = [*peak, max(zz), 0.55, 0.55]
            opt = least_squares(
                lambda v, sample=pts, observed=zz: model(v, sample) - observed,
                start,
                bounds=(
                    [xb, yb, max(zz) - 0.35, 0.2, 0.2],
                    [xt, yt, max(zz) + 1.15, 1.5, 1.5],
                ),
                loss="soft_l1",
                f_scale=0.13,
            )
            v = opt.x
            xo, yo, H, mx, my = v
            clipped = (
                (xy[:, 0] >= xb - 0.25)
                & (xy[:, 0] <= xt + 0.25)
                & (xy[:, 1] >= yb - 0.25)
                & (xy[:, 1] <= yt + 0.25)
            )
            test = np.flatnonzero(elevated & hold & clipped)
            if len(test) < 12:
                continue
            errors = abs(model(v, xy[test]) - height[test])
            p90 = float(np.percentile(errors, 90))
            p50 = float(np.median(errors))
            if p90 > 0.22 or np.percentile(abs(opt.fun), 90) > 0.2:
                continue
            # Derive the eave from actual joined-component extents on all sides.
            low = float(np.quantile(model(v, pts), 0.05))
            hx = (H - low) / mx
            hy = (H - low) / my
            edge = np.array(
                [
                    [xo - hx, yo - hy],
                    [xo + hx, yo - hy],
                    [xo + hx, yo + hy],
                    [xo - hx, yo + hy],
                ]
            )
            im = Polygon(edge)
            if im.difference(p.buffer(2.9)).area > 0.01:
                continue
            # Remove the old stepped approximation in the exact reconstructed component,
            # leaving a mostly planar base underneath the independently measured solid.
            v0 = rec["eave_m"]
            new_v = vert.copy()
            inside_targets = shapely.contains_xy(im.buffer(1.15), *xx.T)
            new_v[inside_targets, 2] = np.round(v0 * 100)
            triangles = np.asarray(rec["f"]).reshape(-1, 3)
            fin = (
                list(rec["face_finish"])
                if rec["face_finish"]
                else ["0"] * len(triangles)
            )
            xy_new = np.vstack([edge, edge, [xo, yo]])
            z_new = np.r_[np.full(4, v0 - 0.05), np.full(4, low), H]
            uv = xy_new / [side, -side] + [cx, cy]
            added = np.rint(np.c_[uv * 100000, z_new * 100]).astype(int)
            offset = len(new_v)
            newtri = []
            newmat = []
            for n1 in range(4):
                n2 = (n1 + 1) % 4
                newtri.extend(
                    [
                        [offset + n1, offset + n2, offset + 4 + n2],
                        [offset + n1, offset + 4 + n2, offset + 4 + n1],
                        [offset + 4 + n1, offset + 4 + n2, offset + 8],
                    ]
                )
                newmat.extend(["2", "2", "1"])
            rec["v"] = np.vstack([new_v, added]).astype(int).tolist()
            rec["f"] = np.vstack([triangles, newtri]).ravel().tolist()
            rec["face_finish"] = "".join(fin + newmat)
            rec["color_observations"]["1"] = settings["roof_srgb"]
            rec["color_observations"]["2"] = settings["wall_srgb"]
            rec["qa"]["fitted_hip"] = {
                "measured_east_north_origin": np.round(v[:2], 2).tolist(),
                "apex_m": round(H, 2),
                "eave_m": round(low, 2),
                "dimensions_m": np.round([2 * hx, 2 * hy], 2).tolist(),
                "train": len(pts),
                "spatial_holdout": len(test),
                "p50_p90_m": [round(p50, 3), round(p90, 3)],
                "source": source["survey"],
                "max_overhang_m": round(max(p.distance(Point(q)) for q in edge), 2),
            }
            rec["qa"]["fitted_hip"]["color_reference"] = {
                "solar_conditions": "two observed seasons",
                "samples": settings["hip_color_samples"],
                "median_srgb": settings["roof_srgb"],
            }
    destination.mkdir(parents=True, exist_ok=True)
    tile_id = f"{tx}-{ty}"
    (destination / f"lidar-roofs-{tile_id}.json").write_text(
        json.dumps(input_doc, separators=(",", ":"))
    )
    ground = cls == 2
    xyz = cloud[ground]
    kd = cKDTree(xyz[:, :2])
    N = maptile["grid"]
    DEM = np.asarray(maptile["h"]).reshape((N + 1, N + 1))
    model = RegularGridInterpolator(
        (np.arange(N + 1),) * 2, DEM, bounds_error=False, fill_value=np.nan
    )

    def game_ground(pt):
        return float(
            model(((np.asarray(pt) / [side, -side] + [cx, cy]) * N)[::-1]).item()
        )

    roads = [
        LineString((np.array(r[3]) - [cx, cy]) * [side, -side])
        for r in maptile["roads"]
        if r[0] in ["primary", "secondary", "tertiary", "trunk", "motorway"]
    ]
    lane = shapely.union_all(roads)
    area = shapely.union_all([*(poly[i] for i in candidates)])
    gx = xyz[:, :2]
    zv = xyz[:, 2]
    sp = (np.floor(gx[:, 0] / 1.5) + 2 * np.floor(gx[:, 1] / 1.5)).astype(int) % 5 == 0
    train = gx[~sp]
    trainz = zv[~sp]
    tree = cKDTree(train)

    def locate(q, span):
        ids = tree.query_ball_point(q, span)
        if len(ids) < 10:
            return None
        ds = train[ids] - q
        ZZ = trainz[ids]
        M = np.c_[ds, np.ones(len(ds))]
        keep = np.ones(len(ds), bool)
        if abs(Polygon(ds).convex_hull.area) < span * 0.8:
            return None
        for _ in range(3):
            pars = np.linalg.lstsq(M[keep], ZZ[keep], rcond=None)[0]
            res = abs(ZZ - M @ pars)
            keep = res < max(0.12, np.percentile(res, 72))
            if keep.sum() < 6:
                return None
        if np.percentile(res, 70) > 0.25 or np.linalg.norm(pars[:2]) > 0.48:
            return None
        return float(pars[-1])

    N = maptile["grid"]
    old = np.asarray(maptile["h"])
    raw = old.reshape((N + 1, N + 1)).copy()
    correct = raw.copy()
    # The nearest road crossing has independent federal returns. Preserve its existing absolute vehicle datum.
    anchor = Point(0, 0)
    nearest = (
        lane.interpolate(lane.project(anchor))
        if lane.geom_type == "LineString"
        else nearest_points(lane, anchor)[0]
    )
    controls = []
    for q in train[::8]:
        if (
            np.linalg.norm(q - np.array(nearest.coords[0])) <= 18
            and lane.distance(Point(q)) < 1.65
        ):
            est = locate(q, 2.6)
            if est is not None:
                controls.append(game_ground(q) - est)
    if len(controls) < 5:
        raise RuntimeError("Not enough reference road datum samples")
    bias = float(np.median(controls))
    rows = []
    for iy in range(2, N - 1):
        for ix in range(2, N - 1):
            q = (np.array([ix, iy]) / N - [cx, cy]) * [side, -side]
            pt = Point(q)
            distance = area.distance(pt)
            if (
                distance > 74
                or xy[:, 0].min() + 6 > q[0]
                or xy[:, 0].max() - 6 < q[0]
                or xy[:, 1].min() + 6 > q[1]
                or xy[:, 1].max() - 6 < q[1]
            ):
                continue
            est = locate(q, 3.6)
            if est is None and any(
                p.buffer(-1).contains(pt) for p in [*(poly[i] for i in candidates)]
            ):
                est = locate(q, 14)
            if est is None:
                continue
            fromedge = min(
                q[0] - xy[:, 0].min(),
                xy[:, 0].max() - q[0],
                q[1] - xy[:, 1].min(),
                xy[:, 1].max() - q[1],
            )
            weight = np.clip((fromedge - 6) / 18, 0, 1) * np.clip(
                (74 - distance) / 22, 0, 1
            )
            # Road and shoulder keep their independently conditioned support. Approach smoothly as the road recedes.
            weight *= np.clip((lane.distance(pt) - 4) / 12, 0, 1)
            if weight < 0.12:
                continue
            before = raw[iy, ix]
            delta = est + bias - before
            if abs(delta) > 9:
                continue
            after = before + delta * weight
            if abs(after - before) > 0.05:
                rows.append([iy * (N + 1) + ix, round(before, 2), round(after, 2)])
                correct[iy, ix] = after
    sam = RegularGridInterpolator(
        (np.arange(N + 1),) * 2, correct, bounds_error=False, fill_value=np.nan
    )
    old_err = []
    new_err = []
    for q, ztrue in zip(gx[sp], zv[sp]):
        if not (-54 <= q[0] <= -12 and 8 < q[1] < 55):
            continue
        coord = ((q / [side, -side] + [cx, cy]) * N)[::-1]
        curr = game_ground(q)
        after = sam(coord).item()
        old_err.append(abs(curr - (ztrue + bias)))
        new_err.append(abs(after - (ztrue + bias)))
    checks = {
        "tile": maptile["id"],
        "grid": N,
        "road_anchor_samples": len(controls),
        "local_road_vertical_datum_offset_m": round(bias, 3),
        "source_survey": "USGS " + settings["id"],
        "class2_spatial_holdout_points": len(old_err),
        "old_error_p50_p90_m": np.round(np.percentile(old_err, [50, 90]), 2).tolist(),
        "new_error_p50_p90_m": np.round(np.percentile(new_err, [50, 90]), 2).tolist(),
        "first_or_last_row_changed": any(
            entry[0] // (N + 1) in (0, N) or entry[0] % (N + 1) in (0, N)
            for entry in rows
        ),
    }
    packet = {
        "schema": 1,
        "tile": maptile["id"],
        "grid": N,
        "nodes": rows,
        "receipt": checks,
    }
    (destination / f"district-surface-{tile_id}.json").write_text(
        json.dumps(packet, separators=(",", ":"))
    )
    print(f"{len(input_doc['roofs'])} building roofs and {len(rows)} terrain samples")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path)
    parser.add_argument(
        "--leaf", type=Path, help="Use a local copy of the exact published LAZ leaf"
    )
    parser.add_argument(
        "--fetch", action="store_true", help="Download the pinned leaf once"
    )
    args = parser.parse_args()
    source = json.loads((Path(__file__).parent / "source.json").read_text())
    leaf = args.leaf or args.destination / "inputs" / "11-1298-1358-1021.laz"
    if not leaf.exists():
        if not args.fetch:
            parser.error("Supply --leaf with the local LAZ or use --fetch")
        with urlopen(
            Request(
                source["leaf"],
                headers={"User-Agent": "Sunday Driver USGS survey reproduction"},
            ),
            timeout=8,
        ) as response:
            data = response.read(source["size"] + 1)
        if (
            len(data) != source["size"]
            or hashlib.sha256(data).hexdigest() != source["sha256"]
        ):
            raise ValueError("Source leaf failed byte count or digest validation")
        leaf.parent.mkdir(parents=True, exist_ok=True)
        leaf.write_bytes(data)
    generate(leaf, args.destination)
