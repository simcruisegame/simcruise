"""Volta -- reference: Tesla Model 3 RWD (2024 "Highland").

Control net measured from Commons photographs (side: "Tesla Model 3 (2023),
long range, Japan, left-side.jpg", CC0) and the published dimensions:
length 4720 mm, width 1849 mm without mirrors, height 1441 mm, wheelbase
2875 mm, track 1584 mm, 235/45R18 tyres. Smooth fastback with a glass roof,
no grille, short ducktail lid.
"""
from design_tools import row, L, sedan_regions, standard_lines

PLATE = __import__("os").path.join(__import__("os").path.dirname(__file__), "..", "..", "..", "game", "art", "plate.png")
WB = 2.875
Y_F, Y_R = 2.3075, -2.4125


def side(y, xz, belt):
    return [(x, y, z) for (x, z) in xz] + [belt]


MAIN = [
    row("rear_end",
        side(-2.17, [(0, .28), (.60, .29), (.79, .31), (.86, .37), (.89, .48), (.898, .62), (.89, .80)], (.80, -2.15, 1.0)),
        [(.76, -2.17, 1.03), (.70, -2.18, 1.05), (.64, -2.19, 1.065), (.52, -2.195, 1.075), (.28, -2.20, 1.08), (0, -2.205, 1.082)]),
    row("r_arch_back",
        side(-1.85, [(0, .20), (.62, .21), (.82, .24), (.88, .31), (.905, .45), (.915, .62), (.912, .83)], (.85, -1.88, 1.06)),
        [(.81, -1.95, 1.09), (.76, -1.99, 1.10), (.70, -2.03, 1.11), (.58, -2.06, 1.115), (.31, -2.08, 1.12), (0, -2.09, 1.122)]),
    row("c_base",
        side(-1.62, [(0, .17), (.63, .18), (.83, .215), (.885, .29), (.91, .44), (.9245, .62), (.92, .85)], (.83, -1.50, 1.148)),
        [(.828, -1.4995, 1.149), (.826, -1.499, 1.150), (.824, -1.4985, 1.151), (.60, -1.86, 1.15), (.32, -1.92, 1.155), (0, -1.95, 1.16)],
        crease=["glass_lo", "glass_hi", "rail", "roof_edge", "roof_mid", "crown"]),
    row("r_axle",
        side(-1.4375, [(0, .155), (.63, .16), (.83, .20), (.885, .28), (.912, .44), (.9245, .62), (.92, .855)], (.86, -1.30, 1.06)),
        [(.85, -1.30, 1.07), (.80, -1.30, 1.15), (.74, -1.30, 1.222), (.66, -1.48, 1.26), (.36, -1.62, 1.235), (0, -1.66, 1.23)]),
    row("q_rear",
        side(-1.22, [(0, .15), (.64, .15), (.83, .19), (.885, .28), (.91, .42), (.922, .62), (.918, .85)], (.868, -1.20, 1.03)),
        [(.856, -1.20, 1.04), (.80, -1.19, 1.15), (.73, -1.17, 1.245), (.665, -1.32, 1.30), (.365, -1.42, 1.29), (0, -1.45, 1.285)]),
    row("q_front",
        side(-1.10, [(0, .145), (.64, .145), (.83, .19), (.885, .28), (.91, .42), (.922, .62), (.918, .845)], (.87, -1.12, 1.02)),
        [(.858, -1.12, 1.03), (.80, -1.11, 1.16), (.725, -1.10, 1.258), (.665, -1.20, 1.33), (.365, -1.27, 1.33), (0, -1.29, 1.325)]),
    row("roof_rear",
        side(-0.85, [(0, .145), (.64, .145), (.83, .19), (.885, .28), (.91, .42), (.922, .62), (.918, .842)], (.872, -.85, 1.0)),
        [(.86, -.85, 1.01), (.79, -.85, 1.17), (.71, -.85, 1.285), (.66, -.92, 1.37), (.36, -.97, 1.40), (0, -.99, 1.405)]),
    row("b_rear",
        side(-0.45, [(0, .145), (.64, .145), (.83, .19), (.885, .28), (.91, .42), (.921, .62), (.917, .84)], (.874, -.45, .985)),
        [(.862, -.45, .995), (.785, -.45, 1.17), (.70, -.45, 1.305), (.655, -.45, 1.395), (.36, -.45, 1.432), (0, -.45, 1.438)]),
    row("b_front",
        side(-0.30, [(0, .145), (.64, .145), (.83, .19), (.885, .28), (.91, .42), (.921, .62), (.917, .84)], (.874, -.30, .983)),
        [(.862, -.30, .993), (.785, -.30, 1.17), (.70, -.30, 1.31), (.655, -.30, 1.40), (.36, -.30, 1.436), (0, -.30, 1.441)]),
    row("header",
        side(0.25, [(0, .145), (.64, .145), (.83, .19), (.885, .28), (.91, .42), (.92, .62), (.916, .835)], (.876, .25, .975)),
        [(.864, .25, .985), (.78, .25, 1.16), (.69, .28, 1.27), (.64, .30, 1.37), (.35, .28, 1.418), (0, .26, 1.425)],
        crease=["roof_edge", "roof_mid", "crown"]),
    row("cowl",
        side(0.82, [(0, .145), (.64, .15), (.83, .19), (.885, .28), (.908, .42), (.918, .61), (.914, .83)], (.875, .82, .955)),
        [(.866, .82, .956), (.865, .8205, .957), (.864, .821, .958), (.72, 1.18, .925), (.40, 1.27, .93), (0, 1.30, .93)],
        crease=["glass_lo", "glass_hi", "rail", "roof_edge", "roof_mid", "crown"]),
    row("f_arch_back",
        side(1.02, [(0, .15), (.63, .16), (.83, .205), (.885, .29), (.908, .42), (.918, .60), (.913, .81)], (.87, 1.04, .93)),
        [(.84, 1.10, .925), (.79, 1.18, .915), (.73, 1.25, .905), (.60, 1.36, .895), (.32, 1.42, .89), (0, 1.44, .888)]),
    row("f_axle",
        side(1.4375, [(0, .17), (.62, .18), (.82, .22), (.88, .30), (.905, .42), (.914, .57), (.906, .76)], (.86, 1.45, .86)),
        [(.82, 1.48, .862), (.77, 1.50, .858), (.71, 1.52, .85), (.58, 1.56, .84), (.31, 1.60, .835), (0, 1.62, .833)]),
    row("f_arch_front",
        side(1.80, [(0, .19), (.60, .20), (.80, .235), (.86, .30), (.885, .41), (.892, .54), (.882, .70)], (.835, 1.80, .79)),
        [(.80, 1.82, .79), (.75, 1.84, .787), (.69, 1.86, .782), (.56, 1.88, .775), (.30, 1.90, .77), (0, 1.91, .768)]),
    row("front_end",
        side(2.04, [(0, .20), (.56, .21), (.75, .235), (.82, .30), (.845, .41), (.85, .52), (.835, .645)], (.79, 2.03, .735)),
        [(.75, 2.04, .735), (.71, 2.05, .733), (.66, 2.06, .73), (.53, 2.07, .725), (.28, 2.075, .72), (0, 2.08, .718)]),
]

# Nose and tail: the Kestrel's (Civic) end rows, rescaled to this car's
# overhangs, width and lower hood and deck; they read better in 3D than a
# first-principles cut.
import importlib.util as _u, os as _os
_spec = _u.spec_from_file_location("kestrel", _os.path.join(_os.path.dirname(__file__), "kestrel_s.py"))
_k = _u.module_from_spec(_spec); _spec.loader.exec_module(_k)


def _remap(r, axle_from, axle_to, oh_from, oh_to, zmap, wscale, name):
    pts = []
    for (x, y, z) in r["pts"]:
        yy = axle_to + (y - axle_from) * (oh_to / oh_from)
        pts.append((x * wscale, yy, zmap(z)))
    return {"name": name, "pts": pts, "crease": set(r["crease"])}


def _zf(z):   # front: hood 0.787 -> 0.709, bumper heights kept
    return z if z < .45 else .45 + (z - .45) * (.709 - .45) / (.787 - .45)


def _zr(z):   # rear: deck 1.06 -> 1.082
    return z if z < .5 else .5 + (z - .5) * (1.082 - .5) / (1.06 - .5)


WS = 0.9245 / 0.9005
NOSE = [_remap(_k.NOSE[i], 1.3675, 1.4375, .975, .87, _zf, WS, n) for i, n in enumerate(("nose1", "nose2", "nose"))]
TAIL = [_remap(_k.TAIL[i], -1.3675, -1.4375, .964, .975, _zr, WS, n) for i, n in enumerate(("tail", "tail2", "tail1"))]
# the front_end / rear_end rows must sit behind those corner rows
for k, (x, y, z) in enumerate(MAIN[-1]["pts"]):
    MAIN[-1]["pts"][k] = (x, min(y, NOSE[0]["pts"][k][1] - .05), z)
for k, (x, y, z) in enumerate(MAIN[0]["pts"]):
    MAIN[0]["pts"][k] = (x, max(y, TAIL[2]["pts"][k][1] + .05), z)
ROWS = TAIL + MAIN + NOSE
LEVEL_CREASE = {"belt": ("tail1", "nose1"), "rail": "all", "sill": ("nose1", "nose"), "waist": ("nose1", "nose")}
R = {r["name"]: i for i, r in enumerate(ROWS)}

REGIONS = sedan_regions(R, quarter=True, glass_roof=0.18)

FEATURES = [
    {"kind": "patch", "name": "headlamp", "view": "xyz", "radius": 0.05, "lift": 0.0025,
     "outline": [(.84, 1.78, .735), (.85, 1.92, .71), (.80, 2.05, .668), (.70, 2.15, .653), (.58, 2.21, .652),
                 (.50, 2.225, .657), (.50, 2.22, .676), (.62, 2.19, .69), (.74, 2.12, .705), (.82, 2.0, .72), (.845, 1.86, .735)],
     "mat": {"tex": "headlamp", "name": "lamp_front", "size": (512, 128), "cells": [0.82], "cell_r": 0.05, "amber": False,
             "pipe_in": 0.22, "pipe_w": 0.03, "emission": 2.0}},
    {"kind": "patch", "name": "intake", "view": "front", "lift": 0.002, "radius": 0.03,
     "outline": [(0, .245), (.40, .255), (.44, .29), (.38, .345), (0, .35)],
     "mat": {"tex": "grille", "name": "grille_mesh", "pattern": "mesh", "scale": 30, "line": 0.18, "rough": 0.6, "coat": 0.2}},
    {"kind": "patch", "name": "fog_slot", "view": "front", "lift": 0.002, "radius": 0.01,
     "outline": [(.60, .30), (.76, .32), (.765, .335), (.60, .318)], "mat": "satin"},
    {"kind": "patch", "name": "taillamp", "view": "xyz", "radius": 0.04, "lift": 0.0025,
     "outline": [(.88, -1.95, .93), (.89, -2.07, .855), (.83, -2.25, .838), (.66, -2.32, .848), (.45, -2.345, .882),
                 (.38, -2.345, .905), (.40, -2.33, .95), (.60, -2.30, .972), (.78, -2.24, .982), (.86, -2.12, .985), (.885, -2.0, .965)],
     "mat": {"tex": "taillamp", "name": "lamp_rear", "size": (512, 128), "reverse_u": (0.04, 0.16), "guides": [(0.12, 0.06), (0.4, 0.03)]}},
    {"kind": "patch", "name": "plate_rear", "view": "rear", "lift": 0.004,
     "outline": [(0, .56), (.155, .56), (.155, .712), (0, .712)], "uv": "view", "mat": {"tex": "image", "name": "plate_r", "path": PLATE}},
    {"kind": "patch", "name": "diffuser", "view": "rear", "lift": 0.002, "radius": 0.03,
     "outline": [(0, .29), (.62, .30), (.72, .355), (.66, .365), (0, .352)], "mat": "satin"},
    {"kind": "patch", "name": "reflector", "view": "rear", "lift": 0.002, "radius": 0.008,
     "outline": [(.64, .40), (.80, .41), (.80, .425), (.64, .415)], "mat": {"tex": "reflector", "name": "reflector", "size": (128, 32)}},
    {"kind": "patch", "name": "repeater", "view": "side", "lift": 0.002, "radius": 0.008,
     "outline": [(1.12, .735), (1.0, .722), (1.0, .708), (1.12, .72)], "mat": "trim"},
    {"kind": "patch", "name": "handle_f", "view": "side", "lift": 0.0015, "radius": 0.01,
     "outline": [(-.21, .848), (0.0, .855), (0.0, .87), (-.21, .862)], "mat": "gap"},
    {"kind": "patch", "name": "handle_r", "view": "side", "lift": 0.0015, "radius": 0.01,
     "outline": [(-1.2, .86), (-.99, .866), (-.99, .88), (-1.2, .874)], "mat": "gap"},
] + standard_lines(R,
                   door_front=[(.84, .95), (.86, .6), (.845, .28)],
                   door_mid=[(-.285, .985), (-.29, .6), (-.285, .28)],
                   door_rear=[(-1.11, 1.02), (-1.15, .80), (-1.07, .56), (-.88, .37), (-.72, .30)],
                   sill=[(.845, .28), (0, .28), (-.72, .30)],
                   lid_rear=[(.62, -2.24, 1.05), (.64, -2.30, .94), (.58, -2.33, .80), (.40, -2.355, .74), (0, -2.37, .73)])

DESIGN = {
    "id": "volta",
    "length": Y_F - Y_R, "width": 1.849, "height": 1.441, "wheelbase": WB,
    "track_f": 1.584, "track_r": 1.584, "tire_w": .235, "tire_r": .334, "rim_in": 18,
    "rows": ROWS, "ds": .036, "dl": .026, "level_crease": LEVEL_CREASE, "regions": REGIONS,
    "arch_r": (.41, .40), "arch_z": .345,
    "paint": "#2c4a72", "paint_metal": 0.55, "rim": "#5d6166", "caliper": "#2b2d30", "spokes": "aero", "dish": 0.02,
    "features": FEATURES,
    "eye": (-.37, .07, 1.14),
    "cabin": {"seat": "#1f1f22", "seat_accent": "#1f1f22", "dash": "#1d1d1f", "headliner": "#2a2a2c", "vents": "slot", "strip": "#4a3d33",
              "cluster": False, "screen": (.34, .21), "screen_up": .095, "screen_ahead": -.05, "shifter": "none",
              "dash_ahead": .72, "dash_top_z": .93, "cowl": (1.25, .93), "floor_z": .24, "shelf_z": 1.06, "rear_seat_y": -.80,
              "mirror": (0, .55, 1.34), "spoke_angles": (0, 180)},
    "cabin_rows": ("c_base", "f_arch_back"),
    "mirror": {"at": (.875, .80, .965), "size": (.19, .10, .115), "mat": "paint", "out": 0.03, "dz": 0.07, "dy": -0.03, "foot_mat": "paint"},
    "fin": None,
}
