#!/usr/bin/env python3
"""
make_logo.py -- SimCruise app-icon / logo concept generator.

Python 3 + Pillow only (no other dependencies). Everything is drawn at 4x
resolution with hard edges and downsampled with LANCZOS, so every curve is
cleanly anti-aliased at any output size.

Concepts
  A  "Horizon"  road vanishing to a horizon under a rising sun, sky gradient
  B  "S-Road"   a bold S made of a winding two-lane road with a centre dash
  C  "Pin"      a map pin whose inside is a road/horizon scene

Outputs per concept (written to --out):
  X-icon-1024.png    1024x1024 macOS icon. Default follows the macOS HIG
                     template: an 824 px rounded square (22.4 % corner radius)
                     centred on a transparent 1024 canvas with a soft drop
                     shadow. --full-bleed fills the whole canvas instead.
  X-wordmark.png     512x128 lock-up (icon mark + "SimCruise"), transparent
  X-preview-128.png  128x128 preview of the icon

Usage
  python3 make_logo.py                         # A, B and C
  python3 make_logo.py --concept B             # one concept
  python3 make_logo.py --color sun=#ffaa00 --color green=#0f4a36
  python3 make_logo.py --font "/System/Library/Fonts/SFNS.ttf" --font-variation Semibold
  python3 make_logo.py --font "/System/Library/Fonts/Supplemental/Futura.ttc" --font-index 0
  python3 make_logo.py --squircle              # continuous (superellipse) corners
  python3 make_logo.py --review-sheet review.png   # contact sheet at 16..512 px
  python3 make_logo.py --list-colors
"""
from __future__ import annotations

import argparse
import bisect
import math
import os
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

SS = 4                                  # supersampling factor
LANCZOS = Image.Resampling.LANCZOS
BICUBIC = Image.Resampling.BICUBIC
CORNER_RADIUS = 0.224                   # macOS icon corner radius (fraction of the shape)
HIG_SHAPE = 824 / 1024                  # shape size on the 1024 canvas (macOS HIG template)

# --------------------------------------------------------------------------- palette
COLORS = {
    # road
    "asphalt":      "#1c2126",   # deep road asphalt
    "asphalt_far":  "#55606a",   # asphalt near the horizon (atmospheric haze)
    # sky
    "sky_top":      "#143c75",
    "sky_mid":      "#3a89d8",
    "sky_low":      "#9dcdf1",
    "haze":         "#f8d9a6",   # warm horizon haze
    # sun
    "sun":          "#ffb648",
    "sun_hi":       "#ffd872",
    "glow":         "#ffcf7a",
    "amber":        "#f6b541",   # centre-line dashes
    # British Racing Green (Miata nod)
    "green":        "#15533e",
    "green_light":  "#1f7a57",
    "green_dark":   "#0d3a2b",
    # neutrals
    "cream":        "#f3ecd9",   # road edge lines
    "pin_rim":      "#f6f1e6",
    "bg_dark_top":  "#2b333b",
    "bg_dark_bot":  "#121619",
    # wordmark text ("Sim" / "Cruise")
    "text_a":       "#1c2126",
    "text_b":       "#15533e",
}

# Fonts tried in order (path, ttc index, variable-font instance name or None).
FONT_CHAIN = [
    ("/System/Library/Fonts/Avenir Next.ttc", 2, None),           # Avenir Next Demi Bold
    ("/System/Library/Fonts/SFNS.ttf", 0, "Semibold"),            # SF Pro Semibold
    ("/System/Library/Fonts/HelveticaNeue.ttc", 10, None),        # Helvetica Neue Medium
    ("/System/Library/Fonts/Supplemental/Futura.ttc", 0, None),   # Futura Medium
    ("/Library/Fonts/Arial Unicode.ttf", 0, None),
]


# --------------------------------------------------------------------------- colour helpers
def rgb(h):
    h = h.lstrip("#")
    if len(h) == 3:
        h = "".join(c * 2 for c in h)
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


def rgba(h, a=255):
    return rgb(h) + (int(round(a)),)


def mix(c1, c2, t):
    return tuple(int(round(a + (b - a) * t)) for a, b in zip(c1, c2))


def hexmix(h1, h2, t):
    return "#%02x%02x%02x" % mix(rgb(h1), rgb(h2), t)


def eval_stops(stops, t):
    stops = sorted(stops, key=lambda s: s[0])
    if t <= stops[0][0]:
        return rgb(stops[0][1])
    for (p0, c0), (p1, c1) in zip(stops, stops[1:]):
        if t <= p1:
            u = 0.0 if p1 == p0 else (t - p0) / (p1 - p0)
            return mix(rgb(c0), rgb(c1), u)
    return rgb(stops[-1][1])


def apply_ramp(mask, stops):
    """Map an 'L' image (0..255) through a colour ramp -> opaque RGBA image."""
    table = [eval_stops(stops, i / 255) for i in range(256)]
    bands = [mask.point([c[k] for c in table]) for k in range(3)]
    return Image.merge("RGB", bands).convert("RGBA")


# --------------------------------------------------------------------------- masks / layers
def vertical_mask(size, y0, y1):
    """'L' mask ramping 0 at row y0 to 255 at row y1 (clamped outside)."""
    w, h = size
    col = Image.new("L", (1, h))
    col.putdata([int(round(255 * min(1.0, max(0.0, (y - y0) / (y1 - y0))))) for y in range(h)])
    return col.resize((w, h))


def diagonal_mask(size, angle_deg=45.0):
    """'L' mask ramping 0 (top-left) to 255 (bottom-right) along angle_deg."""
    w, h = size
    n = 256
    ca, sa = math.cos(math.radians(angle_deg)), math.sin(math.radians(angle_deg))
    g = Image.new("L", (n, n))
    g.putdata([int(round(255 * ((x / (n - 1)) * ca + (y / (n - 1)) * sa) / (ca + sa)))
               for y in range(n) for x in range(n)])
    return g.resize((w, h), BICUBIC)


def band_layer(size, y0, y1, color, a0, a1):
    """Solid colour whose alpha ramps a0 -> a1 between rows y0 and y1 (0 elsewhere)."""
    w, h = size
    col = [0] * h
    for y in range(max(0, int(y0)), min(h, int(math.ceil(y1)))):
        t = (y - y0) / (y1 - y0)
        col[y] = int(round(a0 + (a1 - a0) * min(1.0, max(0.0, t))))
    m = Image.new("L", (1, h))
    m.putdata(col)
    layer = Image.new("RGBA", size, rgba(color))
    layer.putalpha(m.resize((w, h)))
    return layer


def radial_layer(size, center, radius, color, a_max, power=2.0):
    """Soft disc: alpha a_max at the centre falling to 0 at `radius` (pixels)."""
    d = max(2, int(round(radius * 2)))
    g = Image.radial_gradient("L").resize((d, d), BICUBIC)   # 0 centre .. ~180 at inscribed circle
    lut = [int(round(a_max * max(0.0, 1.0 - v / 180.3) ** power)) for v in range(256)]
    g = g.point(lut)
    m = Image.new("L", size, 0)
    m.paste(g, (int(round(center[0] - d / 2)), int(round(center[1] - d / 2))))
    layer = Image.new("RGBA", size, rgba(color))
    layer.putalpha(m)
    return layer


def shape_mask(size_px, squircle=False):
    """Rounded-square icon mask (macOS 22.4 % corner radius)."""
    m = Image.new("L", (size_px, size_px), 0)
    d = ImageDraw.Draw(m)
    r = CORNER_RADIUS * size_px
    if not squircle:
        d.rounded_rectangle([0, 0, size_px - 1, size_px - 1], radius=r, fill=255)
        return m
    # continuous corners: superellipse quadrants (exponent 5) of a slightly larger radius
    rs, n, pts = r * 1.28, 5.0, []
    s = size_px - 1
    corners = [((s - rs, rs), 0.0), ((rs, rs), 0.5), ((rs, s - rs), 1.0), ((s - rs, s - rs), 1.5)]
    for (cx, cy), start in corners:          # start: multiple of pi, going counter-clockwise on screen
        for k in range(65):
            th = math.pi * (start + 0.5 * k / 64)
            c, sn = math.cos(th), math.sin(th)
            x = cx + rs * math.copysign(abs(c) ** (2 / n), c)
            y = cy - rs * math.copysign(abs(sn) ** (2 / n), sn)
            pts.append((x, y))
    d.polygon(pts, fill=255)
    return m


# --------------------------------------------------------------------------- path helpers
def offset_path(points, offset):
    """Offset a dense polyline along its left normal (positive = left of travel)."""
    out, n = [], len(points)
    for i, (x, y) in enumerate(points):
        x0, y0 = points[max(i - 1, 0)]
        x1, y1 = points[min(i + 1, n - 1)]
        tx, ty = x1 - x0, y1 - y0
        L = math.hypot(tx, ty) or 1.0
        out.append((x - ty / L * offset, y + tx / L * offset))
    return out


def draw_stroke(draw, points, width, fill, cap="round"):
    """Stroke a dense polyline as a filled polygon (+ round end caps)."""
    left, right = offset_path(points, width / 2), offset_path(points, -width / 2)
    draw.polygon(left + right[::-1], fill=fill)
    if cap == "round":
        r = width / 2
        for (x, y) in (points[0], points[-1]):
            draw.ellipse([x - r, y - r, x + r, y + r], fill=fill)


def cumulative_lengths(points):
    acc = [0.0]
    for (x0, y0), (x1, y1) in zip(points, points[1:]):
        acc.append(acc[-1] + math.hypot(x1 - x0, y1 - y0))
    return acc


def point_at(points, acc, s):
    s = min(max(s, 0.0), acc[-1])
    i = min(bisect.bisect_right(acc, s) - 1, len(points) - 2)
    seg = acc[i + 1] - acc[i]
    u = 0.0 if seg == 0 else (s - acc[i]) / seg
    (x0, y0), (x1, y1) = points[i], points[i + 1]
    return (x0 + (x1 - x0) * u, y0 + (y1 - y0) * u)


def sub_path(points, acc, s0, s1, step):
    n = max(2, int((s1 - s0) / step) + 1)
    return [point_at(points, acc, s0 + (s1 - s0) * k / n) for k in range(n + 1)]


def s_road_path(S, r=0.125, a=0.07, b=0.125, terminal_deg=30.0, n=180):
    """Centre-line of the letter S, in pixel coords for an S x S shape.

    Two equal circles (radius r) -- top bowl centred at (+a, +b), bottom bowl at
    (-a, -b) in a y-up frame centred on the icon -- joined by their internal
    tangent, which becomes the diagonal spine. `terminal_deg` is the angle of
    the top terminal on the top bowl (the bottom one is point-symmetric).
    """
    R = math.hypot(a, b)
    if R < r:
        raise ValueError("S bowls overlap: need hypot(a, b) >= r")
    phi = -(math.atan2(a, b) + math.acos(r / R))         # spine direction (down-right)
    c1, c2 = (a, b), (-a, -b)
    t1 = (c1[0] + r * math.sin(phi), c1[1] - r * math.cos(phi))   # tangent point on top bowl
    t2 = (-t1[0], -t1[1])
    th1 = math.atan2(t1[1] - c1[1], t1[0] - c1[0])
    th_t = math.radians(terminal_deg)
    while th1 <= th_t:
        th1 += 2 * math.pi
    pts = [(c1[0] + r * math.cos(th_t + (th1 - th_t) * k / n),
            c1[1] + r * math.sin(th_t + (th1 - th_t) * k / n)) for k in range(n + 1)]
    m = max(2, int(n * math.hypot(t2[0] - t1[0], t2[1] - t1[1]) / (r * (th1 - th_t))))
    pts += [(t1[0] + (t2[0] - t1[0]) * k / m, t1[1] + (t2[1] - t1[1]) * k / m) for k in range(1, m)]
    th2, th_b = th1 + math.pi, th_t + math.pi
    while th_b >= th2:
        th_b -= 2 * math.pi
    pts += [(c2[0] + r * math.cos(th2 + (th_b - th2) * k / n),
             c2[1] + r * math.sin(th2 + (th_b - th2) * k / n)) for k in range(n + 1)]
    return [(S * (0.5 + x), S * (0.5 - y)) for (x, y) in pts]


def pin_polygon(cx, cy, r, d, n=160):
    """Map-pin outline: circle (cx, cy, r) with a tip d below the centre."""
    beta = math.acos(r / d)
    a0, a1 = math.pi / 2 + beta, math.pi / 2 - beta + 2 * math.pi   # y-down angles, the long way round
    pts = [(cx, cy + d)]
    pts += [(cx + r * math.cos(a0 + (a1 - a0) * k / n), cy + r * math.sin(a0 + (a1 - a0) * k / n))
            for k in range(n + 1)]
    return pts


# --------------------------------------------------------------------------- road scene pieces
def perspective_dashes(draw, S, vp, y_near, half_w_near, color, period=0.5, length=0.27, offset=0.0):
    """Centre-line dashes receding to the vanishing point vp (unit coords).

    Depth d maps to screen y(d) = vp_y + (y_near - vp_y) / (1 + d) and the dash
    half-width scales the same way, so spacing and width foreshorten together.
    """
    x0, hy = vp
    Y = lambda d: hy + (y_near - hy) / (1 + d)
    W = lambda d: half_w_near / (1 + d)
    for k in range(400):
        d0 = offset + k * period
        d1 = d0 + length
        y0, y1 = Y(d0), Y(d1)
        if (y0 - y1) * S < 1.0:
            break
        w0, w1 = W(d0), W(d1)
        draw.polygon([(S * (x0 - w0), S * y0), (S * (x0 + w0), S * y0),
                      (S * (x0 + w1), S * y1), (S * (x0 - w1), S * y1)], fill=color)


def road_wedge(draw, S, vp, y_near, x_near, width_near, color):
    """Thin wedge from the vanishing point to (x_near +/- width/2, y_near)."""
    draw.polygon([(S * vp[0], S * vp[1]),
                  (S * (x_near - width_near / 2), S * y_near),
                  (S * (x_near + width_near / 2), S * y_near)], fill=color)


def horizon_scene(S, C, hz, sun_c, sun_r, glow_r, vp_x, road_half_w, y_near, top=0.0, bottom=1.0,
                  dash_half_w=0.024, dash_period=0.5, dash_len=0.27, dash_offset=-0.06,
                  edge_w=0.018, edge_inset=0.045):
    """Sky / sun / green ground / asphalt road scene on an S x S canvas (unit coords)."""
    size = (S, S)
    img = apply_ramp(vertical_mask(size, top * S, hz * S),
                     [(0.0, C["sky_top"]), (0.45, C["sky_mid"]), (0.8, C["sky_low"]), (1.0, C["haze"])])
    img = Image.alpha_composite(img, radial_layer(size, (sun_c[0] * S, sun_c[1] * S), glow_r * S,
                                                  C["glow"], 170, power=2.2))
    # sun disc (gradient lit from the top)
    m = Image.new("L", size, 0)
    ImageDraw.Draw(m).ellipse([S * (sun_c[0] - sun_r), S * (sun_c[1] - sun_r),
                               S * (sun_c[0] + sun_r), S * (sun_c[1] + sun_r)], fill=255)
    img.paste(apply_ramp(vertical_mask(size, (sun_c[1] - sun_r) * S, (sun_c[1] + sun_r) * S),
                         [(0.0, C["sun_hi"]), (1.0, C["sun"])]), (0, 0), m)
    # ground
    m = Image.new("L", size, 0)
    ImageDraw.Draw(m).rectangle([0, S * hz, S, S * bottom + 2], fill=255)
    img.paste(apply_ramp(vertical_mask(size, hz * S, bottom * S),
                         [(0.0, C["green_light"]), (1.0, C["green"])]), (0, 0), m)
    # road: trapezoid converging on the vanishing point
    m = Image.new("L", size, 0)
    ImageDraw.Draw(m).polygon([(S * (vp_x - 0.004), S * hz), (S * (vp_x + 0.004), S * hz),
                               (S * (vp_x + road_half_w), S * (y_near + 0.02)),
                               (S * (vp_x - road_half_w), S * (y_near + 0.02))], fill=255)
    img.paste(apply_ramp(vertical_mask(size, hz * S, y_near * S),
                         [(0.0, C["asphalt_far"]), (0.45, hexmix(C["asphalt_far"], C["asphalt"], 0.75)),
                          (1.0, C["asphalt"])]), (0, 0), m)
    # warm haze over the horizon line
    img = Image.alpha_composite(img, band_layer(size, hz * S, (hz + 0.08) * S, C["haze"], 120, 0))
    draw = ImageDraw.Draw(img)
    for sgn in (-1, 1):   # cream edge lines
        road_wedge(draw, S, (vp_x, hz), y_near + 0.02, vp_x + sgn * (road_half_w - edge_inset), edge_w, rgba(C["cream"]))
    perspective_dashes(draw, S, (vp_x, hz), y_near, dash_half_w, rgba(C["amber"]),
                       period=dash_period, length=dash_len, offset=dash_offset)
    return img


# --------------------------------------------------------------------------- concepts
def draw_concept_a(S, C):
    """A -- Horizon: road to a rising sun."""
    return horizon_scene(S, C, hz=0.56, sun_c=(0.5, 0.495), sun_r=0.16, glow_r=0.58,
                         vp_x=0.5, road_half_w=0.37, y_near=1.0)


def draw_concept_b(S, C):
    """B -- S-Road: a bold S made of a two-lane road."""
    img = apply_ramp(diagonal_mask((S, S)), [(0.0, C["green_light"]), (0.55, C["green"]), (1.0, C["green_dark"])])
    draw = ImageDraw.Draw(img)
    path = s_road_path(S, r=0.145, a=0.065, b=0.135, terminal_deg=25.0)
    w = 0.185 * S
    draw_stroke(draw, path, w, rgba(C["asphalt"]), cap="flat")
    for sgn in (1, -1):                                   # cream edge lines
        draw_stroke(draw, offset_path(path, sgn * (w / 2 - 0.028 * S)), 0.011 * S, rgba(C["cream"]), cap="flat")
    acc = cumulative_lengths(path)                        # amber centre dashes, centred on the path
    dash, gap, margin = 0.07 * S, 0.055 * S, 0.03 * S
    usable = acc[-1] - 2 * margin
    n = int((usable + gap) // (dash + gap))
    s = margin + (usable - (n * dash + (n - 1) * gap)) / 2
    for _ in range(n):
        draw_stroke(draw, sub_path(path, acc, s, s + dash, S * 0.003), 0.026 * S, rgba(C["amber"]), cap="flat")
        s += dash + gap
    return img


def draw_concept_c(S, C):
    """C -- Pin: a map pin filled with a road/horizon scene."""
    size = (S, S)
    img = apply_ramp(vertical_mask(size, 0, S), [(0.0, C["bg_dark_top"]), (1.0, C["bg_dark_bot"])])
    img = Image.alpha_composite(img, radial_layer(size, (0.5 * S, 0.42 * S), 0.62 * S, "#4a545e", 80, power=1.6))
    cx, cy, r, d, rim = 0.5, 0.40, 0.25, 0.48, 0.024
    outer = pin_polygon(cx * S, cy * S, r * S, d * S)
    k = 1 - rim / r                                       # inner pin = outer scaled about the circle centre
    inner = pin_polygon(cx * S, cy * S, r * k * S, d * k * S)
    # drop shadow
    sh = Image.new("L", size, 0)
    ImageDraw.Draw(sh).polygon(outer, fill=255)
    sh = ImageChops.offset(sh, 0, int(0.025 * S)).filter(ImageFilter.GaussianBlur(0.03 * S)).point(lambda v: v * 0.55)
    shadow = Image.new("RGBA", size, (0, 0, 0, 255))
    shadow.putalpha(sh)
    img = Image.alpha_composite(img, shadow)
    ImageDraw.Draw(img).polygon(outer, fill=rgba(C["pin_rim"]))
    # scene inside the pin
    hz = cy + 0.03
    scene = horizon_scene(S, C, hz=hz, sun_c=(cx, hz - 0.05), sun_r=0.085, glow_r=0.30,
                          vp_x=cx, road_half_w=0.31, y_near=cy + d, top=cy - r, bottom=cy + d,
                          dash_half_w=0.02, dash_period=0.55, dash_len=0.3, dash_offset=0.0,
                          edge_w=0.014, edge_inset=0.04)
    m = Image.new("L", size, 0)
    ImageDraw.Draw(m).polygon(inner, fill=255)
    img.paste(scene, (0, 0), m)
    return img


CONCEPTS = {
    "A": ("Horizon", draw_concept_a),
    "B": ("S-Road", draw_concept_b),
    "C": ("Pin", draw_concept_c),
}


# --------------------------------------------------------------------------- renderers
def draw_shape(concept, C, S, squircle=False):
    """Supersampled (S x S) rounded-square icon shape, transparent outside."""
    img = CONCEPTS[concept][1](S, C)
    img.putalpha(ImageChops.multiply(img.getchannel("A"), shape_mask(S, squircle)))
    return img


def render_icon(concept, C, size=1024, full_bleed=False, squircle=False):
    S = size * SS
    if full_bleed:
        return draw_shape(concept, C, S, squircle).resize((size, size), LANCZOS)
    shape_px = int(round(S * HIG_SHAPE))
    shape = draw_shape(concept, C, shape_px, squircle)
    off = (S - shape_px) // 2
    canvas = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    sh = Image.new("L", (S, S), 0)                        # HIG-style soft shadow
    sh.paste(shape.getchannel("A"), (off, off + int(round(0.010 * S))))
    sh = sh.filter(ImageFilter.GaussianBlur(0.011 * S)).point(lambda v: v * 0.30)
    shadow = Image.new("RGBA", (S, S), (0, 0, 0, 255))
    shadow.putalpha(sh)
    canvas = Image.alpha_composite(canvas, shadow)
    canvas.alpha_composite(shape, (off, off))
    return canvas.resize((size, size), LANCZOS)


def load_font(size_px, path=None, index=0, variation=None):
    chain = [(path, index, variation)] if path else FONT_CHAIN
    for p, idx, var in chain:
        if not p or not os.path.exists(p):
            continue
        try:
            f = ImageFont.truetype(p, size_px, index=idx)
            if var:
                try:
                    f.set_variation_by_name(var)
                except Exception:
                    pass
            return f
        except Exception:
            continue
    print("warning: no TrueType font loaded, using Pillow's default", file=sys.stderr)
    return ImageFont.load_default(size_px)


def render_wordmark(concept, C, font_spec, W=512, H=128, mark_px=96, squircle=False):
    SW, SH = W * SS, H * SS
    canvas = Image.new("RGBA", (SW, SH), (0, 0, 0, 0))
    mark = draw_shape(concept, C, mark_px * SS, squircle)
    mx, my = 12 * SS, (SH - mark_px * SS) // 2
    canvas.alpha_composite(mark, (mx, my))
    text_x = mx + mark_px * SS + 22 * SS
    avail = SW - text_x - 10 * SS
    a, b = "Sim", "Cruise"
    px = int(mark_px * 0.78)
    while px > 10:                                        # fit the word in the remaining width
        font = load_font(px * SS, *font_spec)
        if font.getlength(a + b) <= avail:
            break
        px -= 1
    bbox = font.getbbox(a + b)
    text_y = my + (mark_px * SS - (bbox[3] - bbox[1])) // 2 - bbox[1]
    draw = ImageDraw.Draw(canvas)
    draw.text((text_x, text_y), a, font=font, fill=rgba(C["text_a"]))
    draw.text((text_x + font.getlength(a), text_y), b, font=font, fill=rgba(C["text_b"]))
    return canvas.resize((W, H), LANCZOS)


def review_sheet(icons, wordmarks, path):
    """Contact sheet: each icon at 512/256/128/64/32/16 px on light + dark, plus the wordmark."""
    sizes = [512, 256, 128, 64, 32, 16]
    pad = 24
    row_h = 512 + 2 * pad
    W = pad + sum(s + pad for s in sizes) * 2 + 560
    sheet = Image.new("RGBA", (W, row_h * len(icons)), (236, 237, 241, 255))
    dark = Image.new("RGBA", (W, row_h * len(icons)), (38, 40, 46, 255))
    half = pad + sum(s + pad for s in sizes)
    sheet.paste(dark.crop((half, 0, 2 * half, dark.size[1])), (half, 0))
    for row, key in enumerate(icons):
        y0 = row * row_h + pad
        for col_off in (0, half):
            x = pad + col_off
            for s in sizes:
                sheet.alpha_composite(icons[key].resize((s, s), LANCZOS), (x, y0 + 512 - s))
                x += s + pad
        wm = wordmarks[key]
        sheet.alpha_composite(wm, (2 * half + 24, y0 + 40))
        light_wm = Image.new("RGBA", (wm.width + 24, wm.height + 24), (255, 255, 255, 255))
        light_wm.alpha_composite(wm, (12, 12))
        sheet.alpha_composite(light_wm, (2 * half + 12, y0 + 220))
    sheet.save(path)


# --------------------------------------------------------------------------- CLI
def main(argv=None):
    ap = argparse.ArgumentParser(description="Generate SimCruise icon / logo concepts.")
    ap.add_argument("--concept", default="all", help="A, B, C or all (default)")
    ap.add_argument("--out", default=os.path.dirname(os.path.abspath(__file__)), help="output directory")
    ap.add_argument("--color", action="append", default=[], metavar="KEY=#HEX", help="override a palette colour")
    ap.add_argument("--list-colors", action="store_true", help="print the palette keys and exit")
    ap.add_argument("--font", help="TrueType/OpenType font path for the wordmark")
    ap.add_argument("--font-index", type=int, default=0, help="face index inside a .ttc")
    ap.add_argument("--font-variation", help="named instance for variable fonts, e.g. Semibold")
    ap.add_argument("--full-bleed", action="store_true", help="fill the whole 1024 canvas (no HIG padding/shadow)")
    ap.add_argument("--squircle", action="store_true", help="continuous-curvature corners instead of circular")
    ap.add_argument("--review-sheet", metavar="PATH", help="also write a contact sheet (16..512 px, light/dark)")
    args = ap.parse_args(argv)

    if args.list_colors:
        for k, v in COLORS.items():
            print(f"{k:12s} {v}")
        return 0
    C = dict(COLORS)
    for item in args.color:
        k, _, v = item.partition("=")
        if k not in C:
            ap.error(f"unknown colour key {k!r} (see --list-colors)")
        C[k] = v if v.startswith("#") else "#" + v

    keys = list(CONCEPTS) if args.concept.lower() == "all" else [c.upper() for c in args.concept if c.strip(",")]
    os.makedirs(args.out, exist_ok=True)
    font_spec = (args.font, args.font_index, args.font_variation)
    icons, wordmarks = {}, {}
    for key in keys:
        if key not in CONCEPTS:
            ap.error(f"unknown concept {key!r}")
        icon = render_icon(key, C, 1024, full_bleed=args.full_bleed, squircle=args.squircle)
        wm = render_wordmark(key, C, font_spec, squircle=args.squircle)
        icon.save(os.path.join(args.out, f"{key}-icon-1024.png"))
        wm.save(os.path.join(args.out, f"{key}-wordmark.png"))
        icon.resize((128, 128), LANCZOS).save(os.path.join(args.out, f"{key}-preview-128.png"))
        icons[key], wordmarks[key] = icon, wm
        print(f"{key} ({CONCEPTS[key][0]}): wrote {key}-icon-1024.png, {key}-wordmark.png, {key}-preview-128.png")
    if args.review_sheet:
        review_sheet(icons, wordmarks, args.review_sheet)
        print(f"review sheet: {args.review_sheet}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
