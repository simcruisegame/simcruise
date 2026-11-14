#!/usr/bin/env python3
"""
render_brand.py -- turn the OSDS brand SVGs into PNG / ICO files and test sheets.

Rasterises with a local headless Chrome or Edge (no network), then Pillow for
down-sampling and the multi-size Windows .ico.

Usage
  python render_brand.py png  IN.svg OUT.png SIZE [--bg "#RRGGBB"]
  python render_brand.py pngw IN.svg OUT.png WIDTH          (non-square, e.g. a lockup)
  python render_brand.py html IN.html OUT.png WIDTH HEIGHT
  python render_brand.py ico  OUT.ico SRC_LARGE.svg [SRC_SMALL.svg]
      (16, 20, 24 and 32 px come from SRC_SMALL when it is given)
"""
from __future__ import annotations

import os
import shutil
import subprocess
import sys
import tempfile

from PIL import Image

BROWSERS = [
    r"C:\Program Files\Google\Chrome\Application\chrome.exe",
    r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
    "chromium", "google-chrome", "chrome",
]


def browser() -> str:
    for b in BROWSERS:
        if os.path.isfile(b) or shutil.which(b):
            return b
    raise SystemExit("no Chrome / Edge found")


def shoot(html_path: str, out_png: str, w: int, h: int) -> None:
    with tempfile.TemporaryDirectory() as prof:
        cmd = [browser(), "--headless=new", "--disable-gpu", "--hide-scrollbars",
               "--no-first-run", "--no-default-browser-check", f"--user-data-dir={prof}",
               "--force-device-scale-factor=1", "--default-background-color=00000000",
               f"--window-size={w},{h}", f"--screenshot={os.path.abspath(out_png)}",
               "file:///" + os.path.abspath(html_path).replace("\\", "/")]
        subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=60)
    im = Image.open(out_png)
    if im.size != (w, h):  # headless can add a few px; crop to the asked size
        im.crop((0, 0, w, h)).save(out_png)


def svg_to_png(svg: str, out_png: str, size: int, bg: str | None = None) -> None:
    """Render at >= 512 px, then reduce with LANCZOS for small sizes (clean edges)."""
    big = max(size, 512)
    with tempfile.TemporaryDirectory() as td:
        html = os.path.join(td, "r.html")
        uri = "file:///" + os.path.abspath(svg).replace("\\", "/")
        bgcss = bg if bg else "transparent"
        with open(html, "w", encoding="utf-8") as fh:
            fh.write(f'<html><body style="margin:0;background:{bgcss}">'
                     f'<img src="{uri}" style="display:block;width:{big}px;height:{big}px;object-fit:contain">'
                     f'</body></html>')
        tmp = os.path.join(td, "r.png")
        shoot(html, tmp, big, big)
        im = Image.open(tmp).convert("RGBA")
        if big != size:
            im = im.resize((size, size), Image.Resampling.LANCZOS)
        im.save(out_png)


def svg_to_png_w(svg: str, out_png: str, width: int) -> None:
    """Non-square SVG (a lockup) to a transparent PNG of the given width."""
    import re
    head = open(svg, encoding="utf-8").read(400)
    vb = [float(v) for v in re.search(r'viewBox="([^"]+)"', head).group(1).split()]
    height = round(width * vb[3] / vb[2])
    with tempfile.TemporaryDirectory() as td:
        html = os.path.join(td, "r.html")
        uri = "file:///" + os.path.abspath(svg).replace("\\", "/")
        with open(html, "w", encoding="utf-8") as fh:
            fh.write(f'<html><body style="margin:0;background:transparent">'
                     f'<img src="{uri}" style="display:block;width:{width}px;height:{height}px"></body></html>')
        shoot(html, out_png, width, height)


def make_ico(out_ico: str, large_svg: str, small_svg: str | None = None) -> None:
    sizes = [16, 20, 24, 32, 40, 48, 64, 128, 256]
    with tempfile.TemporaryDirectory() as td:
        imgs = []
        for s in sizes:
            src = small_svg if (small_svg and s <= 32) else large_svg
            p = os.path.join(td, f"{s}.png")
            svg_to_png(src, p, s)
            imgs.append(Image.open(p).convert("RGBA"))
        imgs[-1].save(out_ico, format="ICO", sizes=[(s, s) for s in sizes], append_images=imgs[:-1])


if __name__ == "__main__":
    a = sys.argv[1:]
    if not a:
        print(__doc__); sys.exit(2)
    if a[0] == "png":
        bg = a[a.index("--bg") + 1] if "--bg" in a else None
        svg_to_png(a[1], a[2], int(a[3]), bg)
    elif a[0] == "pngw":
        svg_to_png_w(a[1], a[2], int(a[3]))
    elif a[0] == "html":
        shoot(a[1], a[2], int(a[3]), int(a[4]))
    elif a[0] == "ico":
        make_ico(a[1], a[2], a[3] if len(a) > 3 else None)
    else:
        print(__doc__); sys.exit(2)
