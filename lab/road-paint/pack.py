"""Export verified paint geometry. The historical aerial and unused photograph must pass area-wide tests."""

from lanes import *
from collections import defaultdict
import math
from shapely.geometry import MultiPolygon, MultiLineString
from shapely.ops import unary_union
from collections import Counter


def geomring(geom):
    return np.array(geom.exterior.coords)[:-1]


def ribbons(poly, a, n, width):
    pts = np.array(poly.exterior.coords)
    dist = pts @ a
    start, end = dist.min() + 0.012, dist.max() - 0.012
    if end - start < 0.15:
        return []
    sections = []
    for ss in np.linspace(start, end, max(2, int(np.ceil((end - start) / 1.2)) + 1)):
        piece = poly.intersection(LineString([ss * a - 300 * n, ss * a + 300 * n]))
        if piece.is_empty:
            continue
        c = np.array(piece.centroid.coords)[0]
        sections.append([c - n * width * 0.5, c + n * width * 0.5])
    return [Polygon([u[0], u[1], v[1], v[0]]) for u, v in zip(sections, sections[1:])]


def save_final(output):
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    bytile = defaultdict(lambda: {"features": [], "replacement": []})
    tile_provenance = {}
    results = {}
    for name in SITES:
        d = Path(name)
        doc = json.loads((d / "accepted.json").read_text())
        w = json.loads((d / "width.json").read_text())
        cal = json.loads((d / "lane-registration.json").read_text())
        feats = doc["features"]
        stats = {}
        for kind in ["symbol", "dash", "right_edge"]:
            cov = [
                o["withheld_ortho_coverage_0p29m"] for o in feats if o["kind"] == kind
            ]
            if cov:
                stats[kind] = {
                    "count": len(cov),
                    "median_coverage": round(float(np.median(cov)), 3),
                    "min_coverage": min(cov),
                }
        width = {}
        for k in ["right_edge", "dash"]:
            key = "dash_m" if k == "dash" else "edge_m"
            rows = w["independent_photo_estimates"]
            width[k] = np.mean([rows[0][key], rows[2][key]])
        receipt = {
            "features": stats,
            "crossview": cal,
            "width_m_two_outer_street_photos": width,
            "withheld_epoch": doc["withheld_epoch"],
        }
        # Centimetre claims would overstate the source. Separate survey support and in-photo width are both essential.
        # Apply a district-level minimum because patchy historic dashes can create doubled lane phase.
        eligible = (
            stats.get("dash", {}).get("min_coverage", 0) >= 0.85
            and stats.get("right_edge", {}).get("min_coverage", 0) >= 0.85
            and stats.get("symbol", {}).get("min_coverage", 1) >= 0.85
            and all(0.065 <= v <= 0.24 for v in width.values())
            and cal["nearest_dash_mean_endpoint_error_px"] <= 20
            and cal.get("mean_arrow_centroid_error_px", 0) <= 20
        )
        receipt["accepted"] = eligible
        results[name] = receipt
        if not eligible:
            continue
        # Work in local E/N metres rather than quantized OSM geometry; it selected only which carriageway to search.
        _, _, lon, lat, heading = SITES[name]
        o = np.array(WGS.transform(lon, lat))
        latScale = np.cos(np.deg2rad(lat))
        a = np.array([np.sin(np.deg2rad(heading)), np.cos(np.deg2rad(heading))])
        n = np.array([a[1], -a[0]])

        def en(x, y):
            return (np.array(WGS.transform(x, y)) - o[:, None]) * latScale

        def tilexy(x, y):
            X = np.asarray(x) / latScale + o[0]
            Y = np.asarray(y) / latScale + o[1]
            return (X + H) / (2 * H) * 65536, (H - Y) / (2 * H) * 65536

        patches = []
        dash_polys = []
        physical = []
        for feat in feats:
            poly = shapely.transform(Polygon(feat["polygon"]), en, interleaved=False)
            kind = feat["kind"]
            if kind == "symbol":
                patches.append(("features", kind, poly))
                continue
            parts = ribbons(poly, a, n, width[kind])
            patches.extend(("features", kind, q) for q in parts)
            if kind == "dash":
                dash_polys.append((feat, poly))
        strips = []
        for (f1, p1), (f2, p2) in zip(
            sorted(dash_polys, key=lambda v: v[0]["station_m"]),
            sorted(dash_polys, key=lambda v: v[0]["station_m"])[1:],
        ):
            if not 8 < f2["station_m"] - f1["station_m"] < 16:
                continue
            u = np.array(p1.centroid.coords[0])
            v = np.array(p2.centroid.coords[0])
            t = (v - u) / np.linalg.norm(v - u)
            strips.append(
                LineString(
                    [u - t * f1["length_m"] / 2, v + t * f2["length_m"] / 2]
                ).buffer(2.0, cap_style="flat")
            )
        u = unary_union(strips)
        patches.extend(
            ("replacement", "mask", v)
            for v in ([u] if u.geom_type == "Polygon" else u.geoms)
        )
        for field, k, p in patches:
            unit = shapely.transform(p, tilexy, interleaved=False)
            x0, y0, x1, y1 = unit.bounds
            for x in range(int(x0), int(x1) + 1):
                for y in range(int(y0), int(y1) + 1):
                    clipped = unit.intersection(box(x, y, x + 1, y + 1))
                    for g in (
                        [clipped]
                        if clipped.geom_type == "Polygon"
                        else getattr(clipped, "geoms", [])
                    ):
                        if g.geom_type != "Polygon" or g.area < 1e-10:
                            continue
                        if (x, y) in tile_provenance and tile_provenance[(x, y)][
                            0
                        ] != name:
                            raise ValueError(
                                "Multiple validated roads overlap one package tile; review its masks before combining them."
                            )
                        tile_provenance[(x, y)] = (name, dash_polys)
                        bytile[(x, y)][field].append(
                            [k, np.round(geomring(g) - [x, y], 8).tolist()]
                        )
    for (x, y), row in bytile.items():
        name, dash_polys = tile_provenance[(x, y)]
        row.update(
            version=1,
            tile=[x, y],
            source=SURVEYS[name]["sources"],
        )
        # Road revalidation anchors are data, sampled from the measured stripe. The runtime abstains if the map realigns.
        ordered = sorted(dash_polys, key=lambda pair: pair[0]["station_m"])

        def normalized_center(feat):
            ll = np.mean(np.asarray(feat["polygon"]), axis=0)
            X, Y = WGS.transform(*ll)
            return [
                round((X + H) / (2 * H) * 65536 - x, 7),
                round((H - Y) / (2 * H) * 65536 - y, 7),
            ]

        row["road"] = SURVEYS[name]["road"]
        stations = [normalized_center(v[0]) for v in ordered]
        stations = [p for p in stations if all(0 <= q <= 1 for q in p)]
        if len(stations) < 2:
            continue
        row["anchors"] = [stations[0], stations[len(stations) // 2], stations[-1]]
        path = output / f"observed-paint-{x}-{y}.json"
        path.write_text(json.dumps(row, separators=(",", ":")) + "\n")
    (output / "decision.json").write_text(json.dumps(results, indent=2))


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser()
    parser.add_argument("output")
    save_final(parser.parse_args().output)
