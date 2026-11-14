"""Clean training and test data for the scan engine's aerial eye.

Every input and label is legal for a free game, weights AND data (docs/research/local-ai-cpu.md):

- Aerial photo: USGS The National Map, USGSImageryOnly (NAIP-based; US public domain).
- Buildings, roads (paved / unpaved), water: OpenStreetMap via OpenFreeMap vector tiles (ODbL).
- Trees: Meta and WRI canopy height map v2 (CC BY 4.0), the same source the game already reads.
- Grass, crop, shrub, bare, built, wetland, snow: ESA WorldCover 2021 10 m v200 (CC BY 4.0).

One sample is one Web Mercator zoom-16 tile (the game's world tile, 611.5 m x cos(lat)) at 512 px,
so about 1 m a pixel at mid latitudes. Files are cached under the data folder and are never
committed; the game never reads them.

    python -I lab/scan/scan_data.py --out C:/aiwork/scan-data --places test --random 30 --ring 1
"""
import argparse, io, json, math, os, random, struct, sys, time, urllib.request, zlib

import numpy as np
from PIL import Image, ImageDraw

UA = {"User-Agent": "SimCruise scan-eye data fetch (+https://github.com/simcruisegame/simcruise)"}
Z = 16
PX = 512
ORIGIN = 20037508.342789244

# Label classes of the eye (index = network output channel). 255 = ignore.
CLASSES = ["paved_road", "unpaved_road", "roof", "water", "tree", "grass", "crop", "shrub",
           "bare", "built_other", "wetland", "snow"]
C = {n: i for i, n in enumerate(CLASSES)}
IGNORE = 255

# ESA WorldCover codes to eye classes.
WORLDCOVER = {10: C["tree"], 20: C["shrub"], 30: C["grass"], 40: C["crop"], 50: C["built_other"],
              60: C["bare"], 70: C["snow"], 80: C["water"], 90: C["wetland"], 95: C["tree"], 100: C["grass"]}

# OpenMapTiles road classes: width in metres (both directions) and whether paved by default.
ROAD_W = {"motorway": 22, "trunk": 16, "primary": 13, "secondary": 11, "tertiary": 9, "minor": 7,
          "service": 4.5, "track": 3.5, "path": 0, "raceway": 9, "busway": 7, "bus_guideway": 7}


def get(url, cache, tries=4, rng=None):
    if cache and os.path.exists(cache):
        with open(cache, "rb") as f:
            return f.read()
    hdr = dict(UA)
    if rng:
        hdr["Range"] = "bytes=%d-%d" % rng
    err = None
    for k in range(tries):
        try:
            data = urllib.request.urlopen(urllib.request.Request(url, headers=hdr), timeout=120).read()
            if cache:
                os.makedirs(os.path.dirname(cache), exist_ok=True)
                with open(cache, "wb") as f:
                    f.write(data)
            time.sleep(0.4)  # polite: under 3 requests a second to any host
            return data
        except Exception as e:  # noqa: BLE001
            err = e
            time.sleep(2 ** k)
    raise RuntimeError(f"{url}: {err}")


def tile_of(lat, lon, z=Z):
    n = 2 ** z
    x = int((lon + 180) / 360 * n)
    y = int((1 - math.asinh(math.tan(math.radians(lat))) / math.pi) / 2 * n)
    return x, y


def tile_bounds_3857(x, y, z=Z):
    size = 2 * ORIGIN / 2 ** z
    return x * size - ORIGIN, ORIGIN - (y + 1) * size, (x + 1) * size - ORIGIN, ORIGIN - y * size


def pixel_latlon(x, y, z=Z, px=PX):
    """Lat/lon of every pixel centre of the tile, arrays px x px."""
    n = 2 ** z
    u = (x + (np.arange(px) + 0.5) / px) / n
    v = (y + (np.arange(px) + 0.5) / px) / n
    lon = u * 360 - 180
    lat = np.degrees(np.arctan(np.sinh(np.pi * (1 - 2 * v))))
    return np.meshgrid(lat, lon, indexing="ij")


def aerial(x, y, root):
    b = tile_bounds_3857(x, y)
    url = ("https://basemap.nationalmap.gov/arcgis/rest/services/USGSImageryOnly/MapServer/export?bbox="
           + ",".join("%.3f" % v for v in b) + f"&bboxSR=3857&imageSR=3857&size={PX},{PX}&format=jpg&f=image")
    data = get(url, os.path.join(root, "aerial", f"{Z}-{x}-{y}.jpg"))
    return np.asarray(Image.open(io.BytesIO(data)).convert("RGB"))


_ofm = None


def ofm_template(root):
    global _ofm
    if _ofm is None:
        meta = json.loads(get("https://tiles.openfreemap.org/planet", os.path.join(root, "osm", "planet.json")))
        _ofm = meta["tiles"][0]
    return _ofm


def osm_layers(x, y, root):
    import mapbox_vector_tile
    tx, ty = x >> 2, y >> 2
    url = ofm_template(root).replace("{z}", "14").replace("{x}", str(tx)).replace("{y}", str(ty))
    data = get(url, os.path.join(root, "osm", f"14-{tx}-{ty}.pbf"))
    if data[:2] == b"\x1f\x8b":
        data = zlib.decompress(data, 16 + zlib.MAX_WBITS)
    return mapbox_vector_tile.decode(data, default_options={"y_coord_down": True}), (x - 4 * tx, y - 4 * ty)


def _rings(geom):
    t, c = geom["type"], geom["coordinates"]
    if t == "Polygon":
        return [c]
    if t == "MultiPolygon":
        return c
    return []


def _lines(geom):
    t, c = geom["type"], geom["coordinates"]
    if t == "LineString":
        return [c]
    if t == "MultiLineString":
        return c
    return []


def rasterise_osm(x, y, root, metres_per_px):
    """Water, paved and unpaved road, building masks (uint8 0/1), px x px; border band masks for ignore."""
    layers, (sx, sy) = osm_layers(x, y, root)
    out = {}
    for name in ("water", "paved", "unpaved", "roof"):
        out[name] = Image.new("L", (PX, PX), 0)

    def tf(p, extent):
        s = PX * 4 / extent
        return (p[0] * s - sx * PX, p[1] * s - sy * PX)

    def fill(img, geom, extent):
        d = ImageDraw.Draw(img)
        for poly in _rings(geom):
            for k, ring in enumerate(poly):
                pts = [tf(p, extent) for p in ring]
                if len(pts) >= 3:
                    d.polygon(pts, fill=1 if k == 0 else 0)

    for f in layers.get("water", {}).get("features", []):
        fill(out["water"], f["geometry"], layers["water"]["extent"])
    for f in layers.get("building", {}).get("features", []):
        fill(out["roof"], f["geometry"], layers["building"]["extent"])
    tr = layers.get("transportation", {})
    for f in tr.get("features", []):
        p = f["properties"]
        cls = p.get("class", "")
        if p.get("brunnel") == "tunnel":
            continue
        w = ROAD_W.get(cls, 0)
        if w <= 0:
            continue
        surface = p.get("surface")
        paved = surface != "unpaved" and cls != "track" if surface is None else surface == "paved"
        img = out["paved" if paved else "unpaved"]
        d = ImageDraw.Draw(img)
        wpx = max(1, int(round(w / metres_per_px)))
        for line in _lines(f["geometry"]):
            pts = [tf(q, tr["extent"]) for q in line]
            if len(pts) >= 2:
                d.line(pts, fill=1, width=wpx, joint="curve")
    return {k: np.asarray(v, dtype=np.uint8) for k, v in out.items()}


def chm(x, y, root):
    """Meta/WRI canopy height v2, metres, 512 x 512 for this z16 tile (zeros where there is none)."""
    key = "".join(str(((x >> 6 >> b) & 1) + 2 * ((y >> 6 >> b) & 1)) for b in range(9, -1, -1))
    url = f"https://dataforgood-fb-data.s3.amazonaws.com/forests/v2/global/dinov3_global_chm_v2_ml3/chm/{key}.tif"
    try:
        hdr = get(url, os.path.join(root, "chm", f"h-{key}.bin"), rng=(0, 65535), tries=2)
    except RuntimeError as e:
        if "404" in str(e):  # no canopy file for this area (sea, or outside the map)
            return np.zeros((PX, PX), np.float32)
        raise
    ifd = struct.unpack_from("<I", hdr, 4)[0]
    n = struct.unpack_from("<H", hdr, ifd)[0]
    tags = {}
    for i in range(n):
        tag, typ, cnt, val = struct.unpack_from("<HHII", hdr, ifd + 2 + 12 * i)
        tags[tag] = (typ, cnt, val)

    def val(tag, i):
        typ, cnt, data = tags[tag]
        if cnt == 1:
            return data
        return struct.unpack_from("<H" if typ == 3 else "<I", hdr, data + (2 if typ == 3 else 4) * i)[0]

    idx = (y % 64) * 64 + x % 64
    off, size = val(324, idx), val(325, idx)
    if not size:
        return np.zeros((PX, PX), np.float32)
    comp = get(url, os.path.join(root, "chm", f"c-{Z}-{x}-{y}.bin"), rng=(off, off + size - 1))
    img = np.frombuffer(zlib.decompress(comp), np.uint8).reshape(512, 512)
    return img.astype(np.float32)


def worldcover(x, y, root):
    """ESA WorldCover 2021 class codes on the tile's 512 x 512 pixel grid (nearest), 0 = no data."""
    import rasterio
    from rasterio.windows import from_bounds
    lat, lon = pixel_latlon(x, y)
    out = np.zeros((PX, PX), np.uint8)
    cache = os.path.join(root, "worldcover", f"{Z}-{x}-{y}.npy")
    if os.path.exists(cache):
        return np.load(cache)
    la0, lo0 = int(math.floor(lat.min() / 3) * 3), int(math.floor(lon.min() / 3) * 3)
    for la in range(la0, int(math.floor(lat.max() / 3) * 3) + 1, 3):
        for lo in range(lo0, int(math.floor(lon.max() / 3) * 3) + 1, 3):
            name = f"{'N' if la >= 0 else 'S'}{abs(la):02d}{'E' if lo >= 0 else 'W'}{abs(lo):03d}"
            url = f"/vsicurl/https://esa-worldcover.s3.eu-central-1.amazonaws.com/v200/2021/map/ESA_WorldCover_10m_2021_v200_{name}_Map.tif"
            m = (lat >= la) & (lat < la + 3) & (lon >= lo) & (lon < lo + 3)
            if not m.any():
                continue
            try:
                with rasterio.Env(GDAL_HTTP_USERAGENT=UA["User-Agent"], GDAL_DISABLE_READDIR_ON_OPEN="EMPTY_DIR"):
                    with rasterio.open(url) as ds:
                        w = from_bounds(lon[m].min() - 0.001, lat[m].min() - 0.001, lon[m].max() + 0.001, lat[m].max() + 0.001, ds.transform)
                        w = w.round_offsets().round_lengths()
                        a = ds.read(1, window=w)
                        t = ds.window_transform(w)
                        col = ((lon - t.c) / t.a).astype(int)
                        row = ((lat - t.f) / t.e).astype(int)
                        ok = m & (col >= 0) & (row >= 0) & (col < a.shape[1]) & (row < a.shape[0])
                        out[ok] = a[row[ok], col[ok]]
            except Exception as e:  # noqa: BLE001  (ocean tiles have no file)
                print("worldcover miss", name, e, file=sys.stderr)
    os.makedirs(os.path.dirname(cache), exist_ok=True)
    np.save(cache, out)
    return out


def _dilate(m, r):
    from PIL import ImageFilter
    return np.asarray(Image.fromarray(m * 255).filter(ImageFilter.MaxFilter(2 * r + 1))) > 0


def photo_refine(lab, img):
    """WorldCover's 10 m "built-up" covers whole town blocks, lawns and trees included, and its 10 m
    grass and crop cells spill over pavement. The photo's own colour decides inside those cells:
    green (excess green) is grass; grey with no green is built ground; the rest is unsure (ignored)."""
    f = img.astype(np.float32) + 1.0
    s = f.sum(-1)
    r, g, b = f[..., 0] / s, f[..., 1] / s, f[..., 2] / s
    exg = 2 * g - r - b
    grey = (np.max(f, -1) - np.min(f, -1)) / np.max(f, -1) < 0.18
    warm = (r - b) > 0.03
    out = lab.copy()
    built = lab == C["built_other"]
    out[built & (exg > 0.04)] = C["grass"]
    out[built & ~(exg > 0.04) & ~(grey & (exg < 0.02))] = IGNORE
    soft = (lab == C["grass"]) | (lab == C["crop"]) | (lab == C["shrub"])
    out[soft & grey & (exg < 0.0) & ~warm] = IGNORE
    return out


def labels(x, y, root, img=None):
    """The label image (uint8, class index or 255) and its parts."""
    lat = math.degrees(math.atan(math.sinh(math.pi * (1 - 2 * (y + 0.5) / 2 ** Z))))
    mpp = 2 * ORIGIN / 2 ** Z / PX * math.cos(math.radians(lat))
    wc = worldcover(x, y, root)
    h = chm(x, y, root)
    osm = rasterise_osm(x, y, root, mpp)
    lab = np.full((PX, PX), IGNORE, np.uint8)
    for code, c in WORLDCOVER.items():
        lab[wc == code] = c
    if img is None:
        img = aerial(x, y, root)
    lab = photo_refine(lab, img)
    # WorldCover says tree but the 1 m canopy height says no tree: unsure, ignore.
    lab[(lab == C["tree"]) & (h < 1.5)] = IGNORE
    lab[osm["water"] > 0] = C["water"]
    roads = (osm["paved"] > 0) | (osm["unpaved"] > 0)
    lab[osm["unpaved"] > 0] = C["unpaved_road"]
    lab[osm["paved"] > 0] = C["paved_road"]
    lab[(h >= 3) & (osm["roof"] == 0)] = C["tree"]
    lab[osm["roof"] > 0] = C["roof"]
    # Edges of mapped things are where OSM, WorldCover and the photo disagree most: ignore a 2 px band.
    edge = np.zeros((PX, PX), bool)
    for m in (osm["roof"], roads.astype(np.uint8)):
        edge |= _dilate(m, 2) & ~(m > 0) | (m > 0) & ~(~_dilate(1 - m, 1).astype(bool))
    lab[edge] = IGNORE
    return lab, {"worldcover": wc, "chm": h, **osm}


PALETTE = np.array([[60, 60, 60], [150, 120, 80], [200, 60, 60], [40, 90, 200], [20, 110, 30], [120, 200, 90],
                    [220, 210, 90], [140, 150, 60], [210, 180, 140], [170, 170, 190], [80, 160, 160], [250, 250, 250]], np.uint8)


def colour(lab):
    out = np.zeros(lab.shape + (3,), np.uint8)
    ok = lab != IGNORE
    out[ok] = PALETTE[lab[ok]]
    return out


def places(kind, nrandom, seed):
    here = os.path.dirname(os.path.abspath(__file__))
    out = []
    if kind in ("test", "all"):
        with open(os.path.join(here, "..", "..", "data", "test-places.json")) as f:
            out += [(p["key"], p["lat"], p["lon"]) for p in json.load(f)]
    rnd = random.Random(seed)
    for i in range(nrandom):  # random points in the lower 48 (land is checked by WorldCover later)
        out.append((f"rand-{seed}-{i:02d}", round(rnd.uniform(25.5, 48.8), 4), round(rnd.uniform(-124.0, -67.5), 4)))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--places", default="test")
    ap.add_argument("--random", type=int, default=0)
    ap.add_argument("--seed", type=int, default=7)
    ap.add_argument("--ring", type=int, default=1)
    ap.add_argument("--relabel", action="store_true", help="rebuild the labels of every cached sample (no network)")
    ap.add_argument("--aerial-only", action="store_true", help="fetch only aerial photos (for distillation)")
    a = ap.parse_args()
    if a.aerial_only:
        # Unlabelled photos for distillation: the student learns the teacher's answers on many more places.
        n = 0
        for key, lat, lon in places("none", a.random, a.seed):
            cx, cy = tile_of(lat, lon)
            for dy in range(-a.ring, a.ring + 1):
                for dx in range(-a.ring, a.ring + 1):
                    try:
                        img = aerial(cx + dx, cy + dy, a.out)
                        if img.std() < 4:  # blank or no-data
                            continue
                        n += 1
                    except Exception as e:  # noqa: BLE001
                        print("fail", key, e, flush=True)
            print("place", key, n, flush=True)
        return
    if a.relabel:
        import glob
        for npz in sorted(glob.glob(os.path.join(a.out, "samples", "*.npz"))):
            _, x, y = os.path.basename(npz)[:-4].split("-")
            img = np.load(npz)["image"]
            lab, _ = labels(int(x), int(y), a.out, img)
            np.savez_compressed(npz, image=img, label=lab)
            Image.fromarray(np.concatenate([img, colour(lab)], 1)).save(npz[:-4] + ".jpg", quality=80)
        print("relabelled")
        return
    index = []
    for key, lat, lon in places(a.places, a.random, a.seed):
        cx, cy = tile_of(lat, lon)
        for dy in range(-a.ring, a.ring + 1):
            for dx in range(-a.ring, a.ring + 1):
                x, y = cx + dx, cy + dy
                npz = os.path.join(a.out, "samples", f"{Z}-{x}-{y}.npz")
                try:
                    if not os.path.exists(npz):
                        img = aerial(x, y, a.out)
                        lab, parts = labels(x, y, a.out, img)
                        if (parts["worldcover"] == 0).mean() > 0.5 or (parts["worldcover"] == 80).mean() > 0.9:
                            print("skip (sea or no data)", key, x, y)
                            continue
                        os.makedirs(os.path.dirname(npz), exist_ok=True)
                        np.savez_compressed(npz, image=img, label=lab)
                        Image.fromarray(np.concatenate([img, colour(lab)], 1)).save(npz[:-4] + ".jpg", quality=80)
                    index.append({"place": key, "lat": lat, "lon": lon, "x": x, "y": y, "file": os.path.basename(npz)})
                    print("ok", key, x, y, flush=True)
                except Exception as e:  # noqa: BLE001
                    print("fail", key, x, y, e, flush=True)
    with open(os.path.join(a.out, f"index-{a.places}-{a.random}-{a.seed}.json"), "w") as f:
        json.dump(index, f, indent=0)


if __name__ == "__main__":
    main()
