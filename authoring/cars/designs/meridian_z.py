"""Meridian Z -- reference: 2020 Chevrolet Corvette Stingray (C8) Z51 coupe.

Published dimensions: length 4630 mm, width 1934 mm, height 1234 mm,
wheelbase 2722 mm, track 1648/1598 mm, 245/35ZR19 front and 305/30ZR20 rear
tyres. Commons photographs (side: "2023 Chevrolet Corvette Stingray Coupe
2LT in Red Mist Metallic Tintcoat, Front Left, 06-03-2023.jpg", CC BY-SA
4.0; rear views listed in docs/cars.md). Mid-engine wedge: cab-forward
greenhouse, glass engine cover between flying buttresses, big side intakes,
low nose, rear wing.
"""
from design_tools import row, L
import importlib.util as _u, os as _os

PLATE = __import__("os").path.join(__import__("os").path.dirname(__file__), "..", "..", "..", "game", "art", "plate.png")
WB = 2.722
Y_F, Y_R = 1.361 + .925, -1.361 - .983
W = .967


def side(y, xz, belt):
    return [(x, y, z) for (x, z) in xz] + [belt]


MAIN = [
    row("rear_end",
        side(-2.06, [(0, .30), (.62, .31), (.82, .33), (.89, .39), (.92, .50), (.93, .64), (.925, .84)], (.88, -2.12, .99)),
        [(.84, -2.14, 1.01), (.78, -2.16, 1.025), (.72, -2.18, 1.035), (.58, -2.19, 1.04), (.31, -2.20, 1.04), (0, -2.205, 1.04)]),
    row("r_arch_back",
        side(-1.74, [(0, .20), (.64, .21), (.86, .24), (.925, .31), (.955, .45), (.967, .62), (.96, .86)], (.90, -1.80, 1.02)),
        [(.86, -1.84, 1.04), (.80, -1.88, 1.06), (.74, -1.92, 1.07), (.60, -1.95, 1.06), (.32, -1.98, 1.055), (0, -1.99, 1.055)]),
    row("r_axle",
        side(-1.361, [(0, .15), (.64, .155), (.86, .19), (.93, .27), (.958, .44), (.967, .62), (.96, .87)], (.90, -1.40, 1.03)),
        [(.86, -1.44, 1.06), (.80, -1.48, 1.08), (.74, -1.52, 1.09), (.60, -1.58, 1.075), (.32, -1.63, 1.07), (0, -1.65, 1.07)],
        crease=["roof_edge", "roof_mid", "crown"]),
    row("c_base",
        side(-1.05, [(0, .14), (.64, .145), (.86, .18), (.92, .26), (.945, .42), (.95, .62), (.93, .86)], (.86, -.78, .985)),
        [(.858, -.7795, .986), (.856, -.779, .987), (.854, -.7785, .988), (.62, -1.30, 1.10), (.33, -1.36, 1.11), (0, -1.38, 1.11)],
        crease=["glass_lo", "glass_hi", "rail"]),
    row("q_rear",
        side(-0.86, [(0, .14), (.64, .145), (.85, .18), (.91, .26), (.93, .42), (.935, .62), (.915, .85)], (.86, -.70, .965)),
        [(.85, -.70, .972), (.79, -.69, 1.06), (.73, -.66, 1.13), (.63, -1.0, 1.15), (.34, -1.06, 1.15), (0, -1.08, 1.15)]),
    row("q_front",
        side(-0.70, [(0, .14), (.64, .145), (.84, .18), (.90, .26), (.92, .42), (.925, .62), (.905, .84)], (.862, -.62, .955)),
        [(.852, -.62, .962), (.79, -.61, 1.07), (.72, -.60, 1.15), (.64, -.72, 1.19), (.35, -.76, 1.195), (0, -.78, 1.195)]),
    row("roof_rear",
        side(-0.48, [(0, .14), (.64, .145), (.84, .18), (.90, .26), (.92, .42), (.925, .62), (.905, .835)], (.864, -.45, .945)),
        [(.854, -.45, .952), (.78, -.45, 1.08), (.70, -.45, 1.18), (.64, -.42, 1.215), (.35, -.40, 1.228), (0, -.39, 1.23)],
        crease=["roof_edge", "roof_mid", "crown"]),
    row("b_rear",
        side(-0.20, [(0, .14), (.64, .145), (.84, .18), (.90, .26), (.92, .42), (.925, .62), (.905, .83)], (.866, -.20, .94)),
        [(.856, -.20, .947), (.78, -.20, 1.08), (.69, -.20, 1.19), (.635, -.20, 1.225), (.35, -.20, 1.234), (0, -.20, 1.234)]),
    row("b_front",
        side(-0.05, [(0, .14), (.64, .145), (.84, .18), (.90, .26), (.92, .42), (.925, .62), (.905, .825)], (.866, -.05, .935)),
        [(.856, -.05, .942), (.78, -.05, 1.075), (.69, -.05, 1.19), (.635, -.05, 1.224), (.35, -.05, 1.233), (0, -.05, 1.234)]),
    row("header",
        side(0.22, [(0, .14), (.64, .145), (.84, .18), (.90, .26), (.92, .42), (.925, .61), (.905, .815)], (.866, .22, .925)),
        [(.856, .22, .932), (.78, .22, 1.06), (.68, .25, 1.18), (.62, .27, 1.215), (.34, .28, 1.225), (0, .29, 1.226)],
        crease=["roof_edge", "roof_mid", "crown"]),
    row("cowl",
        side(0.60, [(0, .14), (.64, .145), (.84, .18), (.90, .26), (.92, .42), (.927, .60), (.91, .80)], (.866, .60, .90)),
        [(.858, .60, .901), (.857, .6005, .902), (.856, .601, .903), (.74, .95, .885), (.42, 1.02, .88), (0, 1.05, .875)],
        crease=["glass_lo", "glass_hi", "rail", "roof_edge", "roof_mid", "crown"]),
    row("f_arch_back",
        side(0.94, [(0, .15), (.64, .155), (.86, .19), (.925, .27), (.95, .42), (.958, .58), (.95, .77)], (.90, .98, .86)),
        [(.86, 1.04, .855), (.80, 1.10, .845), (.74, 1.15, .835), (.62, 1.22, .82), (.33, 1.28, .80), (0, 1.30, .795)]),
    row("f_axle",
        side(1.361, [(0, .17), (.63, .175), (.85, .21), (.92, .29), (.945, .42), (.952, .55), (.94, .72)], (.89, 1.38, .80)),
        [(.85, 1.42, .79), (.79, 1.45, .775), (.72, 1.48, .755), (.60, 1.52, .735), (.32, 1.56, .72), (0, 1.58, .715)]),
    row("f_arch_front",
        side(1.70, [(0, .18), (.62, .19), (.83, .22), (.90, .29), (.925, .40), (.93, .51), (.915, .65)], (.86, 1.72, .73)),
        [(.82, 1.74, .715), (.76, 1.76, .70), (.70, 1.78, .685), (.58, 1.80, .665), (.31, 1.82, .65), (0, 1.83, .645)]),
    row("front_end",
        side(1.94, [(0, .19), (.58, .20), (.78, .225), (.86, .29), (.885, .39), (.89, .48), (.875, .60)], (.82, 1.95, .665)),
        [(.78, 1.96, .65), (.73, 1.97, .638), (.67, 1.98, .626), (.55, 1.99, .61), (.29, 2.0, .60), (0, 2.01, .597)]),
]

_spec = _u.spec_from_file_location("kestrel", _os.path.join(_os.path.dirname(__file__), "kestrel_s.py"))
_k = _u.module_from_spec(_spec); _spec.loader.exec_module(_k)


def _remap(r, axle_from, axle_to, oh_from, oh_to, zmap, wscale, name):
    return {"name": name, "crease": set(r["crease"]),
            "pts": [(x * wscale, axle_to + (y - axle_from) * (oh_to / oh_from), zmap(z)) for (x, y, z) in r["pts"]]}


def _zf(z):   # low wedge nose: hood tip 0.787 -> 0.56, bumper lower
    return .18 + (z - .24) * (.56 - .18) / (.787 - .24) if z > .24 else .18 + (z - .24) * 0.5


def _zr(z):   # rear: deck 1.06 -> 1.04, bumper kept
    return z if z < .5 else .5 + (z - .5) * (1.04 - .5) / (1.06 - .5)


WS = W / .9005
NOSE = [_remap(_k.NOSE[i], 1.3675, 1.361, .975, .925, _zf, WS * .98, n) for i, n in enumerate(("nose1", "nose2", "nose"))]
TAIL = [_remap(_k.TAIL[i], -1.3675, -1.361, .964, .983, _zr, WS, n) for i, n in enumerate(("tail", "tail2", "tail1"))]
for k, (x, y, z) in enumerate(MAIN[-1]["pts"]):
    MAIN[-1]["pts"][k] = (x, min(y, NOSE[0]["pts"][k][1] - .05), z)
for k, (x, y, z) in enumerate(MAIN[0]["pts"]):
    MAIN[0]["pts"][k] = (x, max(y, TAIL[2]["pts"][k][1] + .05), z)
ROWS = TAIL + MAIN + NOSE
LEVEL_CREASE = {"shoulder": "all", "belt": ("tail1", "nose1"), "rail": "all", "sill": ("nose1", "nose"), "waist": ("nose1", "nose")}
R = {r["name"]: i for i, r in enumerate(ROWS)}
Lg = L
REGIONS = [
    {"mat": "under", "s": (0, len(ROWS) - 1), "l": (0, 1.6)},
    {"mat": "trim", "s": (R["c_base"], R["cowl"]), "l": (Lg["belt"], Lg["glass_lo"])},
    {"mat": "trim", "s": (R["c_base"], R["cowl"]), "l": (Lg["rail"] - .15, Lg["rail"] + .35)},
    {"mat": "trim", "s": (R["q_rear"], R["q_front"]), "l": (Lg["belt"], Lg["rail"] + .35)},
    {"mat": "glass", "s": (R["c_base"], R["q_rear"]), "l": (Lg["glass_lo"], Lg["rail"] - .15), "inset": .006},
    {"mat": "glass", "s": (R["q_front"], R["cowl"]), "l": (Lg["glass_lo"], Lg["rail"] - .15), "inset": .006},
    {"mat": "glass", "s": (R["header"], R["cowl"]), "l": (Lg["roof_edge"], Lg["crown"]), "inset": .005},
    {"mat": "trim", "s": (R["header"], R["header"] + .1), "l": (Lg["roof_edge"], Lg["crown"])},
    {"mat": "trim", "s": (R["cowl"] - .1, R["cowl"]), "l": (Lg["roof_edge"], Lg["crown"])},
    {"mat": "trim", "s": (R["header"], R["cowl"]), "l": (Lg["roof_edge"], Lg["roof_edge"] + .22)},
    # roof panel (black, removable targa top) and the glass engine cover between the buttresses
    {"mat": "trim", "s": (R["roof_rear"], R["header"]), "l": (Lg["roof_edge"] + .2, Lg["crown"])},
    {"mat": "glass", "s": (R["r_axle"] + .3, R["roof_rear"] - .25), "l": (Lg["roof_mid"] - .3, Lg["crown"]), "inset": .008},
    {"mat": "trim", "s": (R["r_axle"] + .3, R["roof_rear"] - .25), "l": (Lg["roof_mid"] - .3, Lg["roof_mid"] - .1)},
    {"mat": "trim", "s": (R["roof_rear"] - .25, R["roof_rear"]), "l": (Lg["roof_edge"], Lg["crown"])},
    {"mat": "trim", "s": (R["cowl"], R["cowl"] + .3), "l": (Lg["roof_edge"], Lg["crown"])},
]

FEATURES = [
    {"kind": "patch", "name": "headlamp", "view": "xyz", "radius": 0.03, "lift": 0.0025,
     "outline": [(.90, 1.62, .675), (.89, 1.80, .62), (.82, 1.98, .565), (.66, 2.12, .53), (.55, 2.17, .52),
                 (.56, 2.155, .55), (.70, 2.08, .58), (.82, 1.96, .625), (.885, 1.80, .665), (.905, 1.68, .68)],
     "mat": {"tex": "headlamp", "name": "lamp_front", "size": (512, 128), "cells": [0.7, 0.84], "cell_r": 0.045,
             "amber_u": (0.0, 0.12), "pipe_in": 0.18, "pipe_w": 0.03, "emission": 2.0}},
    {"kind": "patch", "name": "intake", "view": "front", "lift": 0.002, "radius": 0.02,
     "outline": [(0, .21), (.30, .22), (.36, .26), (.32, .33), (0, .335)],
     "mat": {"tex": "grille", "name": "grille_mesh", "pattern": "mesh", "scale": 34, "line": 0.18, "rough": 0.6, "coat": 0.2}},
    {"kind": "patch", "name": "corner_intake", "view": "front", "lift": 0.002, "radius": 0.02,
     "outline": [(.46, .22), (.80, .25), (.86, .36), (.72, .44), (.50, .40), (.42, .30)],
     "mat": {"tex": "grille", "name": "grille_slats", "pattern": "slats", "scale": 18, "line": 0.35, "rough": 0.55, "coat": 0.2}},
    {"kind": "patch", "name": "splitter", "view": "front", "lift": 0.002,
     "outline": [(0, .175), (.80, .19), (.88, .23), (.84, .245), (0, .205)], "mat": "satin"},
    {"kind": "patch", "name": "side_intake", "view": "side", "lift": 0.002, "radius": 0.03,
     "outline": [(-.52, .88), (-.84, .86), (-1.04, .70), (-1.07, .52), (-.80, .60), (-.60, .74)],
     "mat": {"tex": "grille", "name": "grille_side", "pattern": "slats", "scale": 12, "line": 0.3, "base": "#08090a", "rib": "#1a1b1e", "rough": 0.6, "coat": 0}},
    {"kind": "patch", "name": "side_blade", "view": "side", "lift": 0.003, "radius": 0.02,
     "outline": [(-.40, .93), (-.52, .92), (-.70, .85), (-.66, .84), (-.50, .895), (-.40, .905)], "mat": "trim"},
    {"kind": "patch", "name": "sill_trim", "view": "side", "lift": 0.002, "radius": 0.02,
     "outline": [(.92, .19), (-.95, .19), (-.95, .26), (.92, .26)], "mat": "satin"},
    {"kind": "patch", "name": "taillamp", "view": "xyz", "radius": 0.025, "lift": 0.0025,
     "outline": [(.93, -1.95, .92), (.94, -2.08, .84), (.88, -2.25, .80), (.70, -2.31, .79), (.55, -2.32, .80),
                 (.55, -2.315, .86), (.70, -2.305, .89), (.86, -2.26, .94), (.92, -2.12, .965), (.935, -2.0, .955)],
     "mat": {"tex": "taillamp", "name": "lamp_rear", "size": (512, 160), "reverse_u": (0.02, 0.12), "guides": [(0.12, 0.06), (0.45, 0.05)]}},
    {"kind": "patch", "name": "rear_vent", "view": "rear", "lift": 0.002, "radius": 0.02,
     "outline": [(.62, .44), (.88, .46), (.90, .62), (.70, .60)],
     "mat": {"tex": "grille", "name": "grille_mesh", "pattern": "mesh", "scale": 34, "line": 0.18}},
    {"kind": "patch", "name": "plate_rear", "view": "rear", "lift": 0.004,
     "outline": [(0, .52), (.155, .52), (.155, .672), (0, .672)], "uv": "view", "mat": {"tex": "image", "name": "plate_r", "path": PLATE}},
    {"kind": "patch", "name": "diffuser", "view": "rear", "lift": 0.002, "radius": 0.02,
     "outline": [(0, .27), (.70, .28), (.80, .36), (.66, .40), (0, .395)], "mat": "satin"},
    {"kind": "line", "name": "door_front", "view": "side", "path": [(.62, .90), (.66, .70), (.70, .45), (.68, .27)]},
    {"kind": "line", "name": "door_rear", "view": "side", "path": [(-.66, .965), (-.62, .80), (-.55, .55), (-.50, .27)]},
    {"kind": "line", "name": "door_sill", "view": "side", "path": [(.68, .27), (0, .27), (-.50, .27)]},
    {"kind": "line", "name": "hood_edge", "view": "sl", "width": 0.004,
     "path": [(R["cowl"] + .45, L["rail"]), (R["f_axle"], L["rail"]), (R["front_end"], L["rail"]), (R["nose1"], L["rail"]), (R["nose2"], L["rail"]), (R["nose"], L["rail"])]},
    {"kind": "line", "name": "lid_edge", "view": "sl", "width": 0.004,
     "path": [(R["r_axle"], L["rail"] + .1), (R["r_arch_back"], L["rail"] + .1), (R["rear_end"], L["rail"] + .1), (R["tail1"] + .2, L["rail"] + .1)]},
]

DESIGN = {
    "id": "meridian_z",
    "length": Y_F - Y_R, "width": 1.934, "height": 1.234, "wheelbase": WB,
    "track_f": 1.648, "track_r": 1.598, "tire_w": .245, "tire_r": .3455, "rim_in": 19,
    "tire_w_rear": .305, "rim_in_rear": 20,
    "rows": ROWS, "ds": .036, "dl": .026, "level_crease": LEVEL_CREASE, "regions": REGIONS,
    "arch_r": (.395, .405), "arch_z": .35, "well_inner": .30,
    "paint": "#c9541d", "paint_metal": 0.45, "rim": "#26282b", "caliper": "#c21f1f", "spokes": "y5",
    "features": FEATURES,
    "eye": (-.386, -.017, 1.04),
    "cabin": {"seat_accent": "#8a1416", "accent": "#e23b2b", "shifter": "none", "screen": (.20, .12), "screen_up": .02, "screen_x": .12,
              "screen_ahead": -.06, "dash_ahead": .68, "dash_top_z": .86, "cowl": (.95, .885), "floor_z": .19, "rear_seat": False,
              "mirror": (0, .40, 1.15), "hub_below": .30, "flat_bottom": True, "column_deg": 20},
    "cabin_rows": ("q_rear", "f_arch_back"),
    "lining_from": "waist",
    "mirror": {"at": (.866, .58, .91), "size": (.20, .10, .10), "mat": "paint", "out": 0.03, "dz": 0.06, "dy": -0.04},
    "wing": {"span": 1.30, "chord": .13, "y": -2.10, "z": 1.075, "thick": .018, "sweep": .035, "rise": .015, "stand_h": .025, "plate_h": .03, "mat": "trim"},
    "exhaust": {"x": [.50, .61, -.50, -.61], "y": -2.30, "z": .34, "r": .045},
    "wipers": {"blades": [(-.58, .04, .99, .90), (.06, .56, 1.0, .89)], "angle": 3},
}
