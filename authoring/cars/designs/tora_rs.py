"""Tora RS -- reference: 2022 Subaru WRX (VB) sedan.

Proportions from the published dimensions (length 4669 mm, width 1826 mm,
height 1468 mm, wheelbase 2670 mm, track 1560/1580 mm, 245/40R18) and
Commons photographs (side: "Subaru WRX S4 GT-H EX (5BA-VBH) right.jpg",
CC BY-SA 4.0; front and rear views listed in docs/cars.md). Notchback with an
upright greenhouse, hood scoop, black polygonal wheel-arch cladding and sill
trim, quad exhaust.
"""
from design_tools import row, L, sedan_regions, standard_lines, photo_map, photo_patch
import importlib.util as _u, os as _os

PLATE = __import__("os").path.join(__import__("os").path.dirname(__file__), "..", "..", "..", "game", "art", "plate.png")
WB = 2.67
Y_F, Y_R = 1.335 + .91, -1.335 - 1.089


def side(y, xz, belt):
    return [(x, y, z) for (x, z) in xz] + [belt]


S = [(0, .155), (.63, .16), (.82, .20), (.885, .28), (.91, .43), (.913, .63)]
MAIN = [
    row("rear_end",
        side(-2.12, [(0, .30), (.60, .31), (.80, .33), (.87, .39), (.895, .50), (.902, .64), (.895, .86)], (.85, -2.20, 1.03)),
        [(.81, -2.21, 1.05), (.75, -2.22, 1.065), (.69, -2.23, 1.075), (.56, -2.24, 1.082), (.30, -2.25, 1.087), (0, -2.255, 1.089)]),
    row("r_arch_back",
        side(-1.82, [(0, .21), (.62, .22), (.82, .25), (.885, .32), (.908, .46), (.915, .63), (.912, .89)], (.87, -1.92, 1.05)),
        [(.83, -1.98, 1.07), (.77, -2.02, 1.082), (.71, -2.06, 1.09), (.58, -2.09, 1.095), (.31, -2.11, 1.10), (0, -2.12, 1.102)]),
    row("c_base",
        side(-1.55, [(0, .17), (.63, .18), (.82, .21), (.885, .29), (.91, .45), (.913, .63), (.91, .895)], (.85, -1.32, 1.10)),
        [(.848, -1.3195, 1.101), (.846, -1.319, 1.102), (.844, -1.3185, 1.103), (.62, -1.45, 1.11), (.33, -1.48, 1.115), (0, -1.50, 1.118)],
        crease=["glass_lo", "glass_hi", "rail", "roof_edge", "roof_mid", "crown"]),
    row("r_axle",
        side(-1.335, S + [(.91, .90)], (.87, -1.20, 1.04)),
        [(.86, -1.20, 1.05), (.81, -1.18, 1.18), (.76, -1.16, 1.27), (.68, -1.30, 1.25), (.37, -1.36, 1.22), (0, -1.38, 1.21)]),
    row("q_rear",
        side(-1.15, S + [(.91, .90)], (.872, -1.10, 1.02)),
        [(.862, -1.10, 1.03), (.81, -1.08, 1.20), (.755, -1.06, 1.30), (.685, -1.18, 1.30), (.37, -1.20, 1.285), (0, -1.21, 1.28)]),
    row("q_front",
        side(-0.98, S + [(.91, .90)], (.874, -1.0, 1.0)),
        [(.864, -1.0, 1.01), (.81, -.99, 1.21), (.75, -.98, 1.32), (.69, -1.03, 1.35), (.37, -1.05, 1.34), (0, -1.06, 1.34)]),
    row("roof_rear",
        side(-0.70, S + [(.91, .90)], (.876, -.70, .985)),
        [(.866, -.70, .995), (.80, -.70, 1.20), (.735, -.70, 1.355), (.69, -.73, 1.415), (.37, -.75, 1.43), (0, -.76, 1.432)]),
    row("b_rear",
        side(-0.30, S + [(.91, .90)], (.878, -.30, .98)),
        [(.868, -.30, .99), (.80, -.30, 1.20), (.73, -.30, 1.36), (.685, -.30, 1.44), (.37, -.30, 1.462), (0, -.30, 1.468)]),
    row("b_front",
        side(-0.16, S + [(.91, .90)], (.878, -.16, .978)),
        [(.868, -.16, .988), (.80, -.16, 1.20), (.728, -.16, 1.362), (.683, -.16, 1.442), (.37, -.16, 1.464), (0, -.16, 1.468)]),
    row("header",
        side(0.28, S + [(.91, .895)], (.878, .28, .975)),
        [(.868, .28, .985), (.80, .28, 1.19), (.72, .30, 1.35), (.66, .32, 1.42), (.36, .33, 1.445), (0, .34, 1.45)],
        crease=["roof_edge", "roof_mid", "crown"]),
    row("cowl",
        side(0.66, S[:5] + [(.912, .62), (.91, .89)], (.876, .66, .97)),
        [(.868, .66, .971), (.867, .6605, .972), (.866, .661, .973), (.72, .93, .99), (.40, 1.00, 1.0), (0, 1.03, 1.0)],
        crease=["glass_lo", "glass_hi", "rail", "roof_edge", "roof_mid", "crown"]),
    row("f_arch_back",
        side(0.94, [(0, .16), (.62, .17), (.82, .21), (.885, .29), (.91, .43), (.915, .61), (.912, .87)], (.87, .96, .955)),
        [(.84, .99, .96), (.80, 1.03, .965), (.74, 1.07, .97), (.62, 1.16, .975), (.33, 1.24, .98), (0, 1.28, .98)]),
    row("f_axle",
        side(1.335, [(0, .18), (.61, .19), (.81, .23), (.88, .31), (.905, .44), (.91, .59), (.905, .83)], (.86, 1.36, .91)),
        [(.82, 1.40, .915), (.78, 1.42, .92), (.72, 1.44, .925), (.61, 1.48, .935), (.33, 1.52, .945), (0, 1.53, .948)]),
    row("f_arch_front",
        side(1.70, [(0, .20), (.60, .21), (.79, .245), (.86, .31), (.885, .43), (.89, .57), (.88, .78)], (.84, 1.72, .865)),
        [(.80, 1.73, .87), (.76, 1.74, .875), (.71, 1.75, .88), (.59, 1.77, .89), (.31, 1.79, .898), (0, 1.80, .90)]),
    row("front_end",
        side(1.92, [(0, .21), (.57, .22), (.76, .245), (.84, .31), (.865, .43), (.87, .56), (.86, .74)], (.82, 1.92, .83)),
        [(.78, 1.93, .835), (.74, 1.94, .838), (.69, 1.95, .84), (.57, 1.96, .845), (.30, 1.97, .85), (0, 1.98, .852)]),
]

# Nose and tail from the Kestrel (Civic) ends, rescaled to this car's
# overhangs, width, taller hood and higher deck.
_spec = _u.spec_from_file_location("kestrel", _os.path.join(_os.path.dirname(__file__), "kestrel_s.py"))
_k = _u.module_from_spec(_spec); _spec.loader.exec_module(_k)


def _remap(r, axle_from, axle_to, oh_from, oh_to, zmap, wscale, name):
    return {"name": name, "crease": set(r["crease"]),
            "pts": [(x * wscale, axle_to + (y - axle_from) * (oh_to / oh_from), zmap(z)) for (x, y, z) in r["pts"]]}


def _zf(z):   # front: hood 0.787 -> 0.852 (taller, blunter nose)
    return z if z < .4 else .4 + (z - .4) * (.852 - .4) / (.787 - .4)


def _zr(z):   # rear: deck 1.06 -> 1.089
    return z if z < .5 else .5 + (z - .5) * (1.089 - .5) / (1.06 - .5)


WS = .913 / .9005
NOSE = [_remap(_k.NOSE[i], 1.3675, 1.335, .975, .91, _zf, WS, n) for i, n in enumerate(("nose1", "nose2", "nose"))]
TAIL = [_remap(_k.TAIL[i], -1.3675, -1.335, .964, 1.089, _zr, WS, n) for i, n in enumerate(("tail", "tail2", "tail1"))]
for k, (x, y, z) in enumerate(MAIN[-1]["pts"]):
    MAIN[-1]["pts"][k] = (x, min(y, NOSE[0]["pts"][k][1] - .05), z)
for k, (x, y, z) in enumerate(MAIN[0]["pts"]):
    MAIN[0]["pts"][k] = (x, max(y, TAIL[2]["pts"][k][1] + .05), z)
ROWS = TAIL + MAIN + NOSE
LEVEL_CREASE = {"shoulder": "all", "belt": ("tail1", "nose1"), "rail": "all", "sill": ("nose1", "nose"), "waist": ("nose1", "nose")}
R = {r["name"]: i for i, r in enumerate(ROWS)}

REGIONS = sedan_regions(R, quarter=True)


def arch_band(name, yc, r_in, outer):
    """Black cladding around an arch: polygonal outer edge (side view), inner
    edge on a circle just outside the arch cut."""
    import math
    inner = []
    a0, a1 = math.radians(-8), math.radians(188)
    for k in range(25):
        a = a0 + (a1 - a0) * k / 24
        inner.append((yc + r_in * math.cos(a), .335 + r_in * math.sin(a)))
    return {"kind": "band", "name": name, "view": "side", "outer": outer, "inner": inner, "lift": .002, "thick": .018,
            "mat": {"tex": "grille", "name": "cladding", "pattern": "mesh", "scale": 90, "line": 0.08, "base": "#151618", "rib": "#1d1f22", "rough": 0.75, "coat": 0}}


# Front and tail from head-on photographs (the Miata method): "The frontview of
# Subaru WRX STI Sport# (5BA-VBH tuned).jpg" and "The rearview of Subaru WRX STI
# Sport# (5BA-VBH tuned).jpg" by Tokumeigakarinoaoshima, Wikimedia Commons, CC0;
# badges and plates painted out (photo_prep.py).
_P = lambda f: _os.path.join(_os.path.dirname(__file__), "..", "photos", f)
FRONT = photo_map(_P("tora_rs_front.jpg"), cx=682, sx=742, z0=.80, py0=60, sz=964)
REAR = photo_map(_P("tora_rs_rear.jpg"), cx=720, sx=772, z0=.96, py0=125, sz=894, view="rear")
_f = lambda pts: [(x - 290, y - 640) for (x, y) in pts]
_r = lambda pts: [(x - 250, y - 480) for (x, y) in pts]
PHOTOS = [
    photo_patch("headlamp", FRONT, _f([(332, 700), (420, 725), (500, 762), (565, 800), (575, 835), (500, 830), (420, 812), (350, 795), (335, 760)]),
                "photo_front", rough=.15, coat=1.0),
    photo_patch("grille", FRONT, _f([(972, 812), (640, 815), (578, 858), (640, 950), (700, 1010), (972, 1018)]), "photo_front", lift=.004),
    photo_patch("corner_intake", FRONT, _f([(325, 935), (450, 960), (560, 1060), (555, 1130), (440, 1135), (330, 1090), (318, 1000)]), "photo_front"),
    photo_patch("intake", FRONT, _f([(972, 1085), (600, 1095), (570, 1150), (620, 1190), (972, 1195)]), "photo_front"),
    photo_patch("tail", REAR, _r([(970, 545), (1440, 548), (1560, 590), (1630, 640), (1645, 780), (1648, 900), (1640, 1000),
                                 (1600, 1080), (1500, 1150), (1300, 1185), (970, 1195)]), "lamp_rear", lift=.004, rough=.25, coat=1.0,
                emit_mask=_P("tora_rs_rear_emit.jpg")),
]

FEATURES = PHOTOS + [
    {"kind": "patch", "name": "front_lip", "view": "front", "lift": 0.002,
     "outline": [(0, .238), (.62, .25), (.70, .29), (.64, .30), (0, .27)], "mat": "satin"},
    arch_band("clad_front", 1.335, .43, [(1.84, .30), (1.86, .52), (1.74, .70), (1.50, .80), (1.17, .80), (.96, .68), (.88, .48), (.88, .30)]),
    arch_band("clad_rear", -1.335, .43, [(-.86, .30), (-.88, .50), (-.98, .68), (-1.20, .80), (-1.52, .80), (-1.76, .70), (-1.86, .52), (-1.86, .32)]),
    {"kind": "patch", "name": "sill_trim", "view": "side", "lift": 0.002, "radius": 0.02,
     "outline": [(.86, .21), (-.86, .21), (-.86, .30), (.86, .30)], "mat": "satin"},
    {"kind": "patch", "name": "plate_rear", "view": "rear", "lift": 0.008,
     "outline": [(0, .60), (.155, .60), (.155, .752), (0, .752)], "uv": "view", "mat": {"tex": "image", "name": "plate_r", "path": PLATE}},
    {"kind": "patch", "name": "plate_front", "view": "front", "lift": 0.006,
     "outline": [(0, .295), (.155, .295), (.155, .40), (0, .40)], "uv": "view", "mat": {"tex": "image", "name": "plate_f", "path": PLATE}},
] + standard_lines(R,
                   door_front=[(.86, .96), (.90, .7), (.92, .5), (.90, .30)],
                   door_mid=[(-.24, .98), (-.25, .6), (-.24, .30)],
                   door_rear=[(-1.06, 1.0), (-1.10, .78), (-1.0, .58), (-.88, .44), (-.84, .30)],
                   sill=[(.90, .30), (0, .30), (-.84, .30)],
                   lid_rear=[(.64, -2.26, 1.07), (.66, -2.33, .97), (.60, -2.375, .80), (.44, -2.40, .76), (0, -2.41, .755)])

DESIGN = {
    "id": "tora_rs",
    "length": Y_F - Y_R, "width": 1.826, "height": 1.468, "wheelbase": WB,
    "track_f": 1.565, "track_r": 1.58, "tire_w": .245, "tire_r": .3266, "rim_in": 18,
    "rows": ROWS, "ds": .036, "dl": .026, "level_crease": LEVEL_CREASE, "regions": REGIONS,
    "arch_r": (.415, .415), "arch_z": .335,
    "paint": "#1a4aa6", "paint_metal": 0.5, "rim": "#2a2c30", "caliper": "#26282b", "spokes": "multi",
    "features": FEATURES,
    "eye": (-.366, .05, 1.16),
    "cabin": {"seat_accent": "#2a2c30", "accent": "#e84a2a", "shifter": "manual", "screen": (.18, .26), "screen_up": -.04,
              "screen_ahead": -.035, "dash_ahead": .70, "dash_top_z": .985, "cowl": (1.0, 1.0), "floor_z": .25, "shelf_z": 1.08,
              "rear_seat_y": -.84, "mirror": (0, .48, 1.34), "vent_drop": .14},
    "cabin_rows": ("c_base", "f_arch_back"),
    "mirror": {"at": (.876, .64, .975), "size": (.2, .11, .125), "mat": "paint", "out": 0.035, "dz": 0.075, "dy": -0.03},
    "handles": [{"at": (.92, -.05, .86), "mat": "paint"}, {"at": (.92, -.98, .87), "mat": "paint"}],
    "wing": {"span": 1.10, "chord": .08, "y": -2.19, "z": 1.09, "thick": .014, "sweep": .02, "rise": .003, "stand_h": 0, "plates": False, "mat": "paint"},
    
    "fin": {"y": -.72},
    "scoop": {"y": 1.42, "len": .44, "w": .56, "h": .06},
    "wipers": {"blades": [(-.60, .02, 1.02, 1.02), (.04, .55, 1.04, 1.01)], "angle": 3},
}
