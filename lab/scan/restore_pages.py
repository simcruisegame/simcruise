"""Restoration pass prototype (scan contract RSTR, v2.0.4): NAIP at about 0.6 m through a x4 restoration
network on the CPU gives 0.15 m/px pages near the road.

    python -I lab/scan/restore_pages.py --lat 41.7318 --lon -93.6001 --out C:/aiwork/restore

- Photo: USDA NAIP via imagery.geoplatform.gov NAIP_plus (public domain), RGB, 1024 px over a z16 tile.
- Model: Real-ESRGAN General x4v3 (BSD-3, data/ai/real-esrgan-general-x4v3.onnx), 128 px windows.
- Pages: 38.4 m squares (256 px at 0.15 m) whose centre is within 30 m of an OSM road (pass 1).
- Self-check: each page, box-downsampled back to the source grid, against the source photo (mean
  absolute difference, 0-255); pages over the limit are rejected.
Writes before (bicubic) | after (restored) pictures and a JSON with seconds per page and per tile.
"""
import argparse, json, math, os, sys, time

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import scan_data as sd  # noqa: E402

PAGE_M = 38.4
SRC = 1024          # NAIP pixels over the tile
WIN, OVL = 128, 8   # restoration windows (input pixels) and overlap


def naip(x, y, root):
    b = sd.tile_bounds_3857(x, y)
    url = ("https://imagery.geoplatform.gov/iipp/rest/services/NAIP/NAIP_plus/ImageServer/exportImage?bbox="
           + ",".join("%.3f" % v for v in b) + f"&bboxSR=3857&imageSR=3857&size={SRC},{SRC}&format=jpgpng&bandIds=0,1,2&f=image")
    import io
    data = sd.get(url, os.path.join(root, "naip", f"16-{x}-{y}.jpg"))
    return np.asarray(Image.open(io.BytesIO(data)).convert("RGB"))


def restore(session, img):
    """x4 restoration of an image (H x W x 3 uint8) in overlapping windows."""
    h, w, _ = img.shape
    out = np.zeros((h * 4, w * 4, 3), np.float32)
    acc = np.zeros((h * 4, w * 4, 1), np.float32)
    step = WIN - 2 * OVL
    name = session.get_inputs()[0].name
    for y0 in range(0, max(1, h - 2 * OVL), step):
        for x0 in range(0, max(1, w - 2 * OVL), step):
            ys, xs = min(y0, h - WIN), min(x0, w - WIN)
            tile = img[ys:ys + WIN, xs:xs + WIN].astype(np.float32).transpose(2, 0, 1)[None] / 255
            r = session.run(None, {name: tile})[0][0].transpose(1, 2, 0)
            out[ys * 4:(ys + WIN) * 4, xs * 4:(xs + WIN) * 4] += r
            acc[ys * 4:(ys + WIN) * 4, xs * 4:(xs + WIN) * 4] += 1
    return (np.clip(out / np.maximum(acc, 1), 0, 1) * 255 + 0.5).astype(np.uint8)


def road_mask(x, y, root, size):
    lat = math.degrees(math.atan(math.sinh(math.pi * (1 - 2 * (y + 0.5) / 2 ** 16))))
    mpp = 2 * sd.ORIGIN / 2 ** 16 / sd.PX * math.cos(math.radians(lat))
    osm = sd.rasterise_osm(x, y, root, mpp)
    m = (osm["paved"] > 0) | (osm["unpaved"] > 0)
    return np.asarray(Image.fromarray(m.astype(np.uint8) * 255).resize((size, size), Image.NEAREST)) > 0, mpp * sd.PX / size


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--lat", type=float, required=True)
    ap.add_argument("--lon", type=float, required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--threads", type=int, default=4)
    ap.add_argument("--model", default=os.path.join(os.path.dirname(__file__), "..", "..", "data", "ai", "real-esrgan-general-x4v3.onnx"))
    ap.add_argument("--max-pages", type=int, default=64)
    ap.add_argument("--reject", type=float, default=12.0, help="self-check limit, mean abs difference 0-255")
    a = ap.parse_args()
    import onnxruntime as ort
    so = ort.SessionOptions(); so.intra_op_num_threads = a.threads
    s = ort.InferenceSession(a.model, so, providers=["CPUExecutionProvider"])
    x, y = sd.tile_of(a.lat, a.lon)
    img = naip(x, y, a.out)
    roads, m_per_src_px = road_mask(x, y, a.out, SRC)
    tile_m = m_per_src_px * SRC
    n = int(tile_m // PAGE_M)
    page_src = PAGE_M / m_per_src_px                     # source pixels per page (about 64)
    near = int(round(30 / m_per_src_px))
    from PIL import ImageFilter
    roads_wide = np.asarray(Image.fromarray(roads.astype(np.uint8) * 255).filter(ImageFilter.MaxFilter(2 * min(near, 30) + 1))) > 0
    pages, t_all = [], time.perf_counter()
    for j in range(n):
        for i in range(n):
            cx, cy = int((i + 0.5) * page_src), int((j + 0.5) * page_src)
            if not roads_wide[min(cy, SRC - 1), min(cx, SRC - 1)] or len(pages) >= a.max_pages:
                continue
            x0, y0 = int(i * page_src), int(j * page_src)
            # The page plus a margin, so the windows have context at the page edge.
            m = 8
            xa, ya = max(0, x0 - m), max(0, y0 - m)
            crop = img[ya:min(SRC, y0 + int(page_src) + m), xa:min(SRC, x0 + int(page_src) + m)]
            if min(crop.shape[:2]) < WIN:
                pad = [(0, max(0, WIN - crop.shape[0])), (0, max(0, WIN - crop.shape[1])), (0, 0)]
                crop = np.pad(crop, pad, mode="reflect")
            t = time.perf_counter()
            up = restore(s, crop)
            dt = time.perf_counter() - t
            ox, oy = (x0 - xa) * 4, (y0 - ya) * 4
            page = np.asarray(Image.fromarray(up[oy:oy + int(page_src) * 4, ox:ox + int(page_src) * 4]).resize((256, 256), Image.LANCZOS))
            src = img[y0:y0 + int(page_src), x0:x0 + int(page_src)]
            back = np.asarray(Image.fromarray(page).resize(src.shape[1::-1], Image.BOX)).astype(np.float32)
            err = float(np.abs(back - src.astype(np.float32)).mean())
            pages.append({"i": i, "j": j, "seconds": round(dt, 3), "selfcheck_mad": round(err, 2), "ok": err <= a.reject})
            if len(pages) <= 6:
                before = np.asarray(Image.fromarray(src).resize((256, 256), Image.BICUBIC))
                Image.fromarray(np.concatenate([before, page], 1)).save(os.path.join(a.out, f"page-{x}-{y}-{i}-{j}.png"))
    total = time.perf_counter() - t_all
    res = {"tile": [16, x, y], "tile_m": round(tile_m, 1), "pages_near_road": len(pages), "seconds_total": round(total, 2),
           "seconds_per_page_median": float(np.median([p["seconds"] for p in pages])) if pages else 0,
           "rejected": sum(not p["ok"] for p in pages), "threads": a.threads, "pages": pages}
    json.dump(res, open(os.path.join(a.out, f"restore-{x}-{y}.json"), "w"), indent=1)
    print(json.dumps({k: v for k, v in res.items() if k != "pages"}))


if __name__ == "__main__":
    main()
