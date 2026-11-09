#!/usr/bin/env python3
"""Draws the original SimCruise car textures. Python 3 + Pillow only.

Outputs (run from the source root):
  game/art/plate.png                 neutral rear number plate, 305 x 152 mm
  game/art/tyre_sidewall_normal.png  tangent-space normal map for the raised
                                     tyre sidewall lettering (size, construction)

Everything here is drawn from scratch for SimCruise (MIT, see
licenses/Car finish.txt). The lettering uses the game's own UI face, Barlow
Semi Condensed (SIL Open Font License, game/art/ui-license.txt). No state,
slogan, maker or tyre-brand marks appear on either texture.

    python3 authoring/draw_car_textures.py
"""
from __future__ import annotations

import math
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = Path(__file__).resolve().parents[1]
ART = ROOT / "game" / "art"
FONT = str(ART / "ui.ttf")


def fit_font(text: str, max_w: int, max_h: int, stroke: int = 0) -> ImageFont.FreeTypeFont:
    size = max_h * 2
    while size > 8:
        font = ImageFont.truetype(FONT, size)
        x0, y0, x1, y1 = font.getbbox(text, stroke_width=stroke)
        if x1 - x0 <= max_w and y1 - y0 <= max_h:
            return font
        size -= 1
    return ImageFont.truetype(FONT, 8)


def centred(draw: ImageDraw.ImageDraw, text: str, font, cx: float, cy: float, **kw) -> None:
    x0, y0, x1, y1 = draw.textbbox((0, 0), text, font=font, stroke_width=kw.get("stroke_width", 0))
    draw.text((cx - (x0 + x1) / 2, cy - (y0 + y1) / 2), text, font=font, **kw)


def plate() -> None:
    # 305 x 152 mm at 2.1 px/mm, drawn at 4x and reduced for clean edges.
    W, H, S = 640, 320, 4
    big = Image.new("RGB", (W * S, H * S), (236, 238, 233))
    d = ImageDraw.Draw(big)
    # Retroreflective sheeting: a faint vertical sheen.
    for y in range(H * S):
        t = y / (H * S)
        shade = int(6 * math.sin(t * math.pi))
        d.line([(0, y), (W * S, y)], fill=(236 + shade // 2, 238 + shade // 2, 233 + shade // 2))
    navy = (30, 44, 82)
    # Pressed rim: dark outer band, light inner bevel.
    d.rounded_rectangle([5 * S, 5 * S, (W - 5) * S, (H - 5) * S], radius=18 * S, outline=navy, width=9 * S)
    d.rounded_rectangle([16 * S, 16 * S, (W - 16) * S, (H - 16) * S], radius=12 * S, outline=(250, 251, 248), width=3 * S)
    # Four mounting slots (178 mm apart, as on a standard 6 x 12 inch plate).
    for sx in (-1, 1):
        for sy in (-1, 1):
            cx = W / 2 + sx * 89 * 2.1
            cy = H / 2 + sy * 61 * 2.1
            d.rounded_rectangle([(cx - 11) * S, (cy - 6) * S, (cx + 11) * S, (cy + 6) * S], radius=6 * S,
                                fill=(118, 122, 124), outline=(84, 88, 90), width=2 * S)
    # Embossed characters: a soft drop shadow, a highlight, then the ink.
    text = "SIMCRUISE"
    font = fit_font(text, int((W - 92) * S), int(122 * S), stroke=2 * S)
    cx, cy = W / 2 * S, H / 2 * S + 4 * S
    centred(d, text, font, cx + 3 * S, cy + 4 * S, fill=(176, 179, 176), stroke_width=2 * S, stroke_fill=(176, 179, 176))
    centred(d, text, font, cx - 2 * S, cy - 2 * S, fill=(255, 255, 255), stroke_width=2 * S, stroke_fill=(255, 255, 255))
    centred(d, text, font, cx, cy, fill=navy, stroke_width=2 * S, stroke_fill=navy)
    out = big.resize((W, H), Image.LANCZOS)
    out.save(ART / "plate.png", optimize=True)
    print("wrote", ART / "plate.png", out.size)


def sidewall_normal() -> None:
    # U runs along the circumference (two repeats per revolution), V across the
    # lettering band: row 0 lies towards the tread, so letters stand upright.
    W, H, S = 2048, 128, 2
    height = Image.new("L", (W * S, H * S), 0)
    d = ImageDraw.Draw(height)
    half = W * S // 2
    size_font = fit_font("195/50 R15 82V", int(470 * S), int(64 * S))
    small_font = fit_font("TUBELESS", int(220 * S), int(34 * S))
    for k in range(2):
        x0 = k * half
        centred(d, "195/50 R15 82V", size_font, x0 + 300 * S, H * S * .52, fill=255)
        centred(d, "RADIAL", small_font, x0 + 655 * S, H * S * .52, fill=255)
        centred(d, "TUBELESS", small_font, x0 + 865 * S, H * S * .52, fill=255)
        # Thin moulded rings either side of the lettering band.
        d.line([(x0, 10 * S), (x0 + half, 10 * S)], fill=150, width=2 * S)
        d.line([(x0, (H - 10) * S), (x0 + half, (H - 10) * S)], fill=150, width=2 * S)
    height = height.resize((W, H), Image.LANCZOS).filter(ImageFilter.GaussianBlur(1.2))
    h = height.load()
    out = Image.new("RGB", (W, H))
    o = out.load()
    strength = 2.6 / 255.0
    for y in range(H):
        for x in range(W):
            dx = (h[(x + 1) % W, y] - h[(x - 1) % W, y]) * 0.5 * strength
            dy = (h[x, min(y + 1, H - 1)] - h[x, max(y - 1, 0)]) * 0.5 * strength
            nx, ny, nz = -dx, dy, 1.0
            inv = 1.0 / math.sqrt(nx * nx + ny * ny + nz * nz)
            o[x, y] = (int(round((nx * inv * .5 + .5) * 255)), int(round((ny * inv * .5 + .5) * 255)),
                       int(round((nz * inv * .5 + .5) * 255)))
    out.save(ART / "tyre_sidewall_normal.png", optimize=True)
    print("wrote", ART / "tyre_sidewall_normal.png", out.size)


if __name__ == "__main__":
    plate()
    sidewall_normal()
