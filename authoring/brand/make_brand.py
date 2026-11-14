#!/usr/bin/env python3
"""
make_brand.py -- OSDS (Open Source Driving Simulator) logo and brand-kit generator.

Draws every mark as clean vector SVG (paths only, no live text, no filters, no
rasters). Letters come from the Overpass font (SIL OFL 1.1, already in
data/fonts/overpass) and are converted to outlines with fontTools.

Method: the design process, test list and delivery list of the
"logo-design" Agent Skill by kaankiziltug (MIT,
https://github.com/kaankiziltug/logo-design-skill). See docs/brand/README.md.

Usage
  python make_brand.py concepts  OUTDIR   # three concepts (greyscale), for review
  python make_brand.py kit       OUTDIR   # final SVG kit for the chosen mark (A)

Needs: Python 3.10+, fontTools (pip install fonttools). Rasters (PNG, ICO) are
made by render_brand.py from these SVGs.
"""
from __future__ import annotations

import math
import os
import sys

from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen
from fontTools.ttLib import TTFont

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
FONT_DIR = os.path.join(REPO, "data", "fonts", "overpass")

# --------------------------------------------------------------------------- palette
# Names and values are documented in docs/brand/README.md.
PAL = {
    "asphalt": "#1B2128",   # ink: primary dark (old SimCruise asphalt, kept for equity)
    "lane":    "#F5B53F",   # brand amber: the centre line (old SimCruise amber)
    "paper":   "#F4EFE4",   # warm off-white: edge-line cream
    "sky":     "#3A89D8",   # support blue: sky, links (accent only)
    "verge":   "#1F7A57",   # support green: verge / OK states (accent only)
    "grey":    "#5B6672",   # neutral: secondary text on light
}


# --------------------------------------------------------------------------- helpers
def f(v: float) -> str:
    s = f"{v:.2f}".rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s


def svg_doc(w: float, h: float, body: str, title: str) -> str:
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {f(w)} {f(h)}" '
            f'width="{f(w)}" height="{f(h)}" role="img" aria-label="{title}">\n'
            f'<title>{title}</title>\n{body}\n</svg>\n')


def circle_line_hits(cx, cy, r, p, d):
    """Return the parameters t where p + t*d meets the circle (sorted)."""
    fx, fy = p[0] - cx, p[1] - cy
    a = d[0] ** 2 + d[1] ** 2
    b = 2 * (fx * d[0] + fy * d[1])
    c = fx ** 2 + fy ** 2 - r ** 2
    disc = b * b - 4 * a * c
    if disc < 0:
        return []
    s = math.sqrt(disc)
    return sorted([(-b - s) / (2 * a), (-b + s) / (2 * a)])


def at(p, d, t):
    return (p[0] + d[0] * t, p[1] + d[1] * t)


def offset_line(p, q, dist, outward_sign):
    """Offset the line p->q by dist along its normal; outward_sign picks the side."""
    dx, dy = q[0] - p[0], q[1] - p[1]
    L = math.hypot(dx, dy)
    nx, ny = -dy / L * outward_sign, dx / L * outward_sign
    return (p[0] + nx * dist, p[1] + ny * dist), (q[0] + nx * dist, q[1] + ny * dist)


def line_intersect(p1, p2, p3, p4):
    x1, y1 = p1; x2, y2 = p2; x3, y3 = p3; x4, y4 = p4
    den = (x1 - x2) * (y3 - y4) - (y1 - y2) * (x3 - x4)
    px = ((x1 * y2 - y1 * x2) * (x3 - x4) - (x1 - x2) * (x3 * y4 - y3 * x4)) / den
    py = ((x1 * y2 - y1 * x2) * (y3 - y4) - (y1 - y2) * (x3 * y4 - y3 * x4)) / den
    return px, py


# --------------------------------------------------------------------------- type
class Type:
    """Overpass glyph outlines as SVG path data."""

    def __init__(self, weight: str):
        self.font = TTFont(os.path.join(FONT_DIR, f"Overpass-{weight}.ttf"))
        self.gs = self.font.getGlyphSet()
        self.cmap = self.font.getBestCmap()
        self.upm = self.font["head"].unitsPerEm
        self.cap = self.font["OS/2"].sCapHeight
        self.kern = self._kern_pairs()

    def _kern_pairs(self):
        pairs = {}
        try:
            from fontTools.ttLib.tables import otTables  # noqa: F401
            gpos = self.font["GPOS"].table
            for lookup in gpos.LookupList.Lookup:
                if lookup.LookupType != 2:
                    continue
                for st in lookup.SubTable:
                    if getattr(st, "Format", None) == 1:
                        cov = st.Coverage.glyphs
                        for i, ps in enumerate(st.PairSet):
                            for rec in ps.PairValueRecord:
                                v = rec.Value1
                                adv = getattr(v, "XAdvance", 0) if v else 0
                                if adv:
                                    pairs[(cov[i], rec.SecondGlyph)] = adv
        except Exception:
            pass
        return pairs

    def run(self, text: str, x: float, baseline: float, cap_px: float,
            tracking: float = 0.0) -> tuple[str, float]:
        """Outline `text` with its cap height = cap_px. Return (path d, advance width)."""
        s = cap_px / self.cap
        pen = SVGPathPen(self.gs)
        cur = 0.0
        prev = None
        for ch in text:
            g = self.cmap[ord(ch)]
            if prev is not None:
                cur += self.kern.get((prev, g), 0)
            tp = TransformPen(pen, (s, 0, 0, -s, x + cur * s, baseline))
            self.gs[g].draw(tp)
            cur += self.font["hmtx"][g][0] + tracking * self.upm
            prev = g
        width = (cur - tracking * self.upm) * s
        return pen.getCommands(), width

    def bbox_x(self, text: str, cap_px: float, tracking: float = 0.0):
        """Ink left side-bearing of the first glyph and right side-bearing of the last (px)."""
        s = cap_px / self.cap
        first = self.cmap[ord(text[0])]
        last = self.cmap[ord(text[-1])]
        glyf = self.font["glyf"]
        lsb = glyf[first].xMin * s
        adv_last = self.font["hmtx"][last][0]
        rsb = (adv_last - glyf[last].xMax) * s
        return lsb, rsb


# --------------------------------------------------------------------------- concept A: Open Road
def open_road(cx=128.0, cy=128.0, R=104.0, r=66.0, vp_y=58.0, slope=0.28,
              horizon_y=114.0, gap=9.0,
              dashes=((122.0, 132.0), (146.0, 164.0), (184.0, 214.0)), dash_k=0.11):
    """
    An O (the "Open" in OSDS) broken at the bottom by a straight road that runs
    up to a crest inside the ring. The road top is cut flat at the horizon (no
    point, so it never reads as a letter A). The centre line is cut out of the
    road. Silhouette = one circle. Returns (ring d, road d).

    vp_y      virtual vanishing point (above the horizon, never drawn)
    slope     road half-width per unit of y below the vanishing point
    horizon_y where the road goes over the crest (flat top)
    gap       white space between the road and the ring
    """
    vp = (cx, vp_y)
    far = 400.0                                    # a point far below, on each edge line
    eL = (cx - slope * (far - vp_y), far)
    eR = (cx + slope * (far - vp_y), far)
    dL = (eL[0] - vp[0], eL[1] - vp[1])
    dR = (eR[0] - vp[0], eR[1] - vp[1])

    def x_at(d, y):                                # x of an edge line at height y
        return vp[0] + d[0] * (y - vp[1]) / d[1]

    # road: flat top at the horizon, edges down to the outer circle, bottom on the outer arc
    pL = at(vp, dL, circle_line_hits(cx, cy, R, vp, dL)[1])
    pR = at(vp, dR, circle_line_hits(cx, cy, R, vp, dR)[1])
    tl = (x_at(dL, horizon_y), horizon_y)
    tr = (x_at(dR, horizon_y), horizon_y)
    road = (f"M{f(tl[0])} {f(tl[1])}H{f(tr[0])}L{f(pR[0])} {f(pR[1])}"
            f"A{f(R)} {f(R)} 0 0 1 {f(pL[0])} {f(pL[1])}Z")

    # centre line: perspective trapezoids (evenodd cut-outs)
    def hw(y):
        return slope * (y - vp_y) * dash_k
    for (y0, y1) in dashes:
        road += (f"M{f(cx - hw(y0))} {f(y0)}H{f(cx + hw(y0))}"
                 f"L{f(cx + hw(y1))} {f(y1)}H{f(cx - hw(y1))}Z")

    # ring with a gap: each road edge moved outward by `gap`
    oL = offset_line(vp, eL, gap, +1)
    oR = offset_line(vp, eR, gap, -1)
    dOL = (oL[1][0] - oL[0][0], oL[1][1] - oL[0][1])
    dOR = (oR[1][0] - oR[0][0], oR[1][1] - oR[0][1])
    oLo = at(oL[0], dOL, circle_line_hits(cx, cy, R, oL[0], dOL)[1])
    oLi = at(oL[0], dOL, circle_line_hits(cx, cy, r, oL[0], dOL)[1])
    oRo = at(oR[0], dOR, circle_line_hits(cx, cy, R, oR[0], dOR)[1])
    oRi = at(oR[0], dOR, circle_line_hits(cx, cy, r, oR[0], dOR)[1])
    ring = (f"M{f(oRo[0])} {f(oRo[1])}"
            f"A{f(R)} {f(R)} 0 1 0 {f(oLo[0])} {f(oLo[1])}"
            f"L{f(oLi[0])} {f(oLi[1])}"
            f"A{f(r)} {f(r)} 0 1 1 {f(oRi[0])} {f(oRi[1])}Z")
    return ring, road


# --------------------------------------------------------------------------- concept B: S-Road
def s_road():
    """A bold S drawn as a two-lane road; the centre line is a dashed cut."""
    c1, c2, rr = (128, 84), (128, 172), 44
    a0, a1 = math.radians(-24), math.radians(156)
    p0 = (c1[0] + rr * math.cos(a0), c1[1] + rr * math.sin(a0))
    p2 = (c2[0] + rr * math.cos(a1), c2[1] + rr * math.sin(a1))
    d = (f"M{f(p0[0])} {f(p0[1])}A{rr} {rr} 0 1 0 128 128"
         f"A{rr} {rr} 0 1 1 {f(p2[0])} {f(p2[1])}")
    return d


# --------------------------------------------------------------------------- concept builders
def concepts(out: str):
    os.makedirs(out, exist_ok=True)
    ink = "#111111"
    eb = Type("ExtraBold")

    # A symbol
    ring, road = open_road()
    a_sym = svg_doc(256, 256, f'<path fill="{ink}" d="{ring}"/>\n'
                    f'<path fill="{ink}" fill-rule="evenodd" d="{road}"/>', "OSDS concept A, Open Road")
    # B symbol (exploration: strokes)
    sd = s_road()
    b_sym = svg_doc(256, 256,
                    f'<path d="{sd}" fill="none" stroke="{ink}" stroke-width="46" stroke-linecap="butt"/>\n'
                    f'<path d="{sd}" fill="none" stroke="#fff" stroke-width="7" stroke-dasharray="14 13" '
                    f'stroke-dashoffset="4"/>', "OSDS concept B, S-Road")
    # C symbol: guide-sign plate with the wordmark
    plate_d, wd = c_plate(eb, ink)
    c_sym = svg_doc(256, 256, plate_d, "OSDS concept C, Guide Sign")

    # lockups (symbol + OSDS word)
    def lockup(sym_body: str, name: str):
        cap = 120.0
        word, w = eb.run("OSDS", 0, 0, cap)
        x0 = 256 + 40
        body = (f'<g>{sym_body}</g>\n'
                f'<path fill="{ink}" transform="translate({f(x0)} {f(128 + cap / 2)})" d="{word}"/>')
        return svg_doc(x0 + w + 8, 256, body, name)

    a_lock = lockup(f'<path fill="{ink}" d="{ring}"/><path fill="{ink}" fill-rule="evenodd" d="{road}"/>',
                    "OSDS concept A lockup")
    b_lock = lockup(f'<path d="{sd}" fill="none" stroke="{ink}" stroke-width="46"/>'
                    f'<path d="{sd}" fill="none" stroke="#fff" stroke-width="7" stroke-dasharray="14 13" stroke-dashoffset="4"/>',
                    "OSDS concept B lockup")
    c_lock = c_sym
    for name, doc in (("a-symbol", a_sym), ("b-symbol", b_sym), ("c-symbol", c_sym),
                      ("a-lockup", a_lock), ("b-lockup", b_lock), ("c-lockup", c_lock)):
        with open(os.path.join(out, name + ".svg"), "w", encoding="utf-8") as fh:
            fh.write(doc)
    print("concepts ->", out)


def c_plate(eb: Type, ink: str):
    x, y, w, h, rx = 8, 70, 240, 116, 18
    inset = 9
    cap = 50.0
    word, ww = eb.run("OSDS", 0, 0, cap)
    tx = 128 - ww / 2
    ty = 128 + cap / 2
    body = (f'<path fill="{ink}" fill-rule="evenodd" d="'
            f'M{x + rx} {y}H{x + w - rx}A{rx} {rx} 0 0 1 {x + w} {y + rx}V{y + h - rx}'
            f'A{rx} {rx} 0 0 1 {x + w - rx} {y + h}H{x + rx}A{rx} {rx} 0 0 1 {x} {y + h - rx}V{y + rx}'
            f'A{rx} {rx} 0 0 1 {x + rx} {y}Z'
            f'M{x + inset + rx - inset} {y + inset}H{x + w - rx}'
            f'A{rx - inset} {rx - inset} 0 0 1 {x + w - inset} {y + rx}V{y + h - rx}'
            f'A{rx - inset} {rx - inset} 0 0 1 {x + w - rx} {y + h - inset}H{x + rx}'
            f'A{rx - inset} {rx - inset} 0 0 1 {x + inset} {y + h - rx}V{y + rx}'
            f'A{rx - inset} {rx - inset} 0 0 1 {x + rx} {y + inset}Z'
            f'M{x + inset + 5 + rx - inset - 5} {y + inset + 5}H{x + w - rx}'
            f'A{rx - inset - 5} {rx - inset - 5} 0 0 1 {x + w - inset - 5} {y + rx}V{y + h - rx}'
            f'A{rx - inset - 5} {rx - inset - 5} 0 0 1 {x + w - rx} {y + h - inset - 5}H{x + rx}'
            f'A{rx - inset - 5} {rx - inset - 5} 0 0 1 {x + inset + 5} {y + h - rx}V{y + rx}'
            f'A{rx - inset - 5} {rx - inset - 5} 0 0 1 {x + rx} {y + inset + 5}Z"/>\n'
            f'<path fill="#ffffff" transform="translate({f(tx)} {f(ty)})" d="{word}"/>')
    return body, ww


# --------------------------------------------------------------------------- the kit (chosen: A, Open Road)
SMALL = dict(R=110.0, r=58.0, gap=15.0, slope=0.31, horizon_y=112.0, dashes=())   # 16-32 px cut


def rounded_square(size: float, pad: float, rx_frac: float = 0.225) -> str:
    x0, x1 = pad, size - pad
    rx = (x1 - x0) * rx_frac
    return (f"M{f(x0 + rx)} {f(x0)}H{f(x1 - rx)}A{f(rx)} {f(rx)} 0 0 1 {f(x1)} {f(x0 + rx)}"
            f"V{f(x1 - rx)}A{f(rx)} {f(rx)} 0 0 1 {f(x1 - rx)} {f(x1)}H{f(x0 + rx)}"
            f"A{f(rx)} {f(rx)} 0 0 1 {f(x0)} {f(x1 - rx)}V{f(x0 + rx)}A{f(rx)} {f(rx)} 0 0 1 {f(x0 + rx)} {f(x0)}Z")


def mark_body(ring_fill: str, road_fill: str, small: bool = False, scale: float = 1.0,
              dx: float = 0.0, dy: float = 0.0) -> str:
    ring, road = open_road(**SMALL) if small else open_road()
    tf = f' transform="translate({f(dx)} {f(dy)}) scale({f(scale)})"' if (scale != 1 or dx or dy) else ""
    return (f'<g{tf}><path fill="{ring_fill}" d="{ring}"/>'
            f'<path fill="{road_fill}" fill-rule="evenodd" d="{road}"/></g>')


def icon_body(small: bool = False, variant: str = "amber-ring") -> str:
    """App icon: asphalt rounded tile, mark at 66 % (optical size inside the tile)."""
    ring_c, road_c = (PAL["lane"], PAL["paper"]) if variant == "amber-ring" else (PAL["paper"], PAL["lane"])
    pad = 0 if small else 0          # full-bleed tile; the OS adds its own margin
    k = 0.70 if small else 0.66
    off = 128 * (1 - k)
    return (f'<path fill="{PAL["asphalt"]}" d="{rounded_square(256, pad, 0.18 if small else 0.225)}"/>'
            + mark_body(ring_c, road_c, small, k, off, off))


def kit(out: str):
    os.makedirs(out, exist_ok=True)
    A, W, AM, PA = PAL["asphalt"], "#FFFFFF", PAL["lane"], PAL["paper"]
    eb, bd = Type("ExtraBold"), Type("Bold")
    files = {}

    # symbol
    files["osds-symbol.svg"] = svg_doc(256, 256, mark_body(A, A), "OSDS symbol")
    files["osds-symbol-white.svg"] = svg_doc(256, 256, mark_body(W, W), "OSDS symbol, white")
    files["osds-symbol-color.svg"] = svg_doc(256, 256, mark_body(AM, PA), "OSDS symbol, colour (use on asphalt)")
    files["osds-symbol-small.svg"] = svg_doc(256, 256, mark_body(A, A, True), "OSDS symbol, small-size cut")
    files["osds-symbol-small-white.svg"] = svg_doc(256, 256, mark_body(W, W, True), "OSDS symbol, small-size cut, white")
    # app icon
    files["osds-icon.svg"] = svg_doc(256, 256, icon_body(False), "OSDS app icon")
    files["osds-icon-small.svg"] = svg_doc(256, 256, icon_body(True), "OSDS app icon, small-size cut")
    files["osds-icon-alt.svg"] = svg_doc(256, 256, icon_body(False, "amber-road"), "OSDS app icon, alternative")

    # horizontal lockup: symbol + OSDS, cap height = 47 % of the symbol, space = ring stroke
    def horizontal(ink: str, sym: str, title: str):
        cap = 120.0
        word, w = eb.run("OSDS", 0, 0, cap)
        lsb, rsb = eb.bbox_x("OSDS", cap)
        x0 = 256 + 38 - lsb
        width = x0 + w - rsb + 4
        return svg_doc(width, 256, sym + f'<path fill="{ink}" transform="translate({f(x0)} {f(128 + cap / 2)})" d="{word}"/>', title)

    files["osds-lockup-horizontal.svg"] = horizontal(A, mark_body(A, A), "OSDS logo")
    files["osds-lockup-horizontal-white.svg"] = horizontal(W, mark_body(W, W), "OSDS logo, white")
    files["osds-lockup-horizontal-color.svg"] = horizontal(W, mark_body(AM, PA), "OSDS logo, colour (use on asphalt)")

    # full-name lockup: symbol + OSDS + OPEN SOURCE DRIVING SIMULATOR (two lines right of the mark)
    def full(ink: str, sub_ink: str, sym: str, title: str):
        cap = 104.0
        word, w = eb.run("OSDS", 0, 0, cap)
        lsb, _ = eb.bbox_x("OSDS", cap)
        cap2 = 27.0
        tr = 0.06
        sub, w2 = bd.run("OPEN SOURCE DRIVING SIMULATOR", 0, 0, cap2, tracking=tr)
        lsb2, _ = bd.bbox_x("OPEN SOURCE DRIVING SIMULATOR", cap2)
        x0 = 256 + 38
        top = 128 - (cap + 30 + cap2) / 2
        body = (sym + f'<path fill="{ink}" transform="translate({f(x0 - lsb)} {f(top + cap)})" d="{word}"/>'
                f'<path fill="{sub_ink}" transform="translate({f(x0 - lsb2)} {f(top + cap + 30 + cap2)})" d="{sub}"/>')
        return svg_doc(x0 + max(w, w2) + 6, 256, body, title)

    files["osds-lockup-full.svg"] = full(A, A, mark_body(A, A), "OSDS, Open Source Driving Simulator")
    files["osds-lockup-full-white.svg"] = full(W, W, mark_body(W, W), "OSDS, Open Source Driving Simulator, white")
    files["osds-lockup-full-color.svg"] = full(W, AM, mark_body(AM, PA), "OSDS, Open Source Driving Simulator, colour")

    # stacked lockup
    def stacked(ink: str, sub_ink: str, sym: str, title: str):
        cap = 96.0
        word, w = eb.run("OSDS", 0, 0, cap)
        lsb, rsb = eb.bbox_x("OSDS", cap)
        cap2 = 22.0
        sub, w2 = bd.run("OPEN SOURCE DRIVING SIMULATOR", 0, 0, cap2, tracking=0.06)
        lsb2, rsb2 = bd.bbox_x("OPEN SOURCE DRIVING SIMULATOR", cap2)
        inkw, inkw2 = w - lsb - rsb, w2 - lsb2 - rsb2
        W_ = max(256, inkw2) + 16
        sx = (W_ - 256) / 2
        y1 = 256 + 44 + cap
        y2 = y1 + 34 + cap2
        body = (f'<g transform="translate({f(sx)} 0)">{sym}</g>'
                f'<path fill="{ink}" transform="translate({f(W_ / 2 - inkw / 2 - lsb)} {f(y1)})" d="{word}"/>'
                f'<path fill="{sub_ink}" transform="translate({f(W_ / 2 - inkw2 / 2 - lsb2)} {f(y2)})" d="{sub}"/>')
        return svg_doc(W_, y2 + 6, body, title)

    files["osds-lockup-stacked.svg"] = stacked(A, A, mark_body(A, A), "OSDS logo, stacked")
    files["osds-lockup-stacked-white.svg"] = stacked(W, W, mark_body(W, W), "OSDS logo, stacked, white")

    # wordmark only
    cap = 120.0
    word, w = eb.run("OSDS", 0, 0, cap)
    lsb, rsb = eb.bbox_x("OSDS", cap)
    files["osds-wordmark.svg"] = svg_doc(w - lsb - rsb + 8, cap + 8,
                                         f'<path fill="{A}" transform="translate({f(4 - lsb)} {f(cap + 4)})" d="{word}"/>',
                                         "OSDS wordmark")

    for name, doc in files.items():
        with open(os.path.join(out, name), "w", encoding="utf-8") as fh:
            fh.write(doc)
    print(f"kit: {len(files)} SVG files ->", out)


if __name__ == "__main__":
    if len(sys.argv) < 3 or sys.argv[1] not in ("concepts", "kit"):
        print(__doc__)
        sys.exit(2)
    if sys.argv[1] == "concepts":
        concepts(sys.argv[2])
    else:
        kit(sys.argv[2])
