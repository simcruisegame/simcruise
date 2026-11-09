"""Kestrel S -- reference: 2022 Honda Civic Si sedan (11th generation).

Control net measured from Commons photographs (side: "Moscow, Honda Civic
sedan, Apr 2026 03.jpg", CC0; front, rear and three-quarter views listed in
docs/cars.md) and the published dimensions: length 4674 mm, width 1801 mm,
height 1415 mm, wheelbase 2735 mm, track 1547/1575 mm, 235/40R18 tyres.
"""
from design_tools import row, L, photo_map, photo_patch

PLATE = __import__("os").path.join(__import__("os").path.dirname(__file__), "..", "..", "..", "game", "art", "plate.png")
WB = 2.735
Y_F, Y_R = 2.3425, -2.3315


def side(y, xz, belt):
    """keel..shoulder at station y from (x, z) pairs, then the belt point (x, y, z)."""
    return [(x, y, z) for (x, z) in xz] + [belt]


MAIN = [
    row("rear_end",
        side(-2.08, [(0, .27), (.58, .28), (.78, .31), (.85, .37), (.872, .49), (.878, .64), (.868, .85)], (.83, -2.20, 1.0)),
        [(.80, -2.19, 1.012), (.74, -2.20, 1.03), (.67, -2.21, 1.045), (.55, -2.22, 1.055), (.30, -2.23, 1.062), (0, -2.235, 1.065)]),
    row("r_arch_back",
        side(-1.80, [(0, .20), (.60, .21), (.80, .24), (.865, .31), (.888, .45), (.897, .62), (.893, .88)], (.85, -1.85, 1.025)),
        [(.81, -1.93, 1.035), (.75, -1.97, 1.05), (.68, -2.0, 1.06), (.56, -2.03, 1.07), (.30, -2.06, 1.078), (0, -2.08, 1.08)]),
    row("c_base",
        side(-1.58, [(0, .17), (.61, .18), (.80, .215), (.868, .29), (.893, .44), (.90, .62), (.897, .885)], (.835, -1.556, 1.036)),
        [(.833, -1.5555, 1.037), (.831, -1.555, 1.038), (.829, -1.5545, 1.039), (.60, -1.80, 1.085), (.32, -1.84, 1.088), (0, -1.86, 1.09)],
        crease=["glass_lo", "glass_hi", "rail", "roof_edge", "roof_mid", "crown"]),
    row("r_axle",
        side(-1.3675, [(0, .15), (.61, .16), (.80, .20), (.87, .28), (.895, .44), (.9005, .62), (.898, .888)], (.85, -1.38, .99)),
        [(.836, -1.38, .998), (.78, -1.37, 1.07), (.73, -1.36, 1.14), (.64, -1.48, 1.17), (.35, -1.58, 1.16), (0, -1.62, 1.155)]),
    row("q_rear",
        side(-1.15, [(0, .145), (.61, .15), (.80, .185), (.865, .26), (.89, .41), (.898, .62), (.896, .886)], (.856, -1.207, .965)),
        [(.845, -1.20, .972), (.76, -1.14, 1.11), (.69, -1.06, 1.24), (.62, -1.22, 1.26), (.34, -1.33, 1.25), (0, -1.36, 1.245)]),
    row("q_front",
        side(-0.95, [(0, .14), (.62, .14), (.80, .175), (.862, .25), (.885, .40), (.895, .62), (.895, .885)], (.858, -.966, .958)),
        [(.846, -.966, .965), (.75, -.966, 1.12), (.668, -.966, 1.262), (.61, -1.06, 1.30), (.33, -1.17, 1.29), (0, -1.20, 1.287)]),
    row("roof_rear",
        side(-0.70, [(0, .14), (.62, .14), (.80, .175), (.86, .25), (.882, .40), (.893, .62), (.894, .884)], (.858, -.70, .955)),
        [(.846, -.70, .962), (.745, -.70, 1.14), (.655, -.70, 1.305), (.60, -.78, 1.35), (.32, -.84, 1.37), (0, -.86, 1.372)]),
    row("b_rear",
        side(-0.33, [(0, .14), (.62, .14), (.80, .175), (.86, .25), (.882, .40), (.893, .62), (.894, .883)], (.86, -.33, .955)),
        [(.846, -.33, .962), (.74, -.33, 1.15), (.648, -.33, 1.322), (.59, -.33, 1.388), (.32, -.34, 1.41), (0, -.35, 1.415)]),
    row("b_front",
        side(-0.19, [(0, .14), (.62, .14), (.80, .175), (.86, .25), (.882, .40), (.893, .62), (.894, .882)], (.86, -.19, .955)),
        [(.846, -.19, .962), (.74, -.19, 1.15), (.647, -.19, 1.323), (.589, -.19, 1.389), (.32, -.20, 1.412), (0, -.20, 1.415)]),
    row("header",
        side(0.25, [(0, .14), (.62, .14), (.80, .175), (.86, .25), (.881, .40), (.892, .62), (.893, .878)], (.858, .25, .953)),
        [(.846, .25, .96), (.74, .25, 1.15), (.645, .253, 1.32), (.585, .28, 1.375), (.32, .31, 1.392), (0, .32, 1.394)],
        crease=["roof_edge", "roof_mid", "crown"]),
    row("cowl",
        side(0.62, [(0, .14), (.62, .145), (.80, .18), (.858, .25), (.88, .40), (.89, .62), (.892, .87)], (.857, .62, .95)),
        [(.849, .62, .951), (.848, .6205, .952), (.847, .621, .953), (.70, .93, .975), (.38, 1.04, .98), (0, 1.10, .98)],
        crease=["glass_lo", "glass_hi", "rail", "roof_edge", "roof_mid", "crown"]),
    row("f_arch_back",
        side(0.98, [(0, .15), (.61, .16), (.80, .20), (.862, .28), (.888, .42), (.896, .60), (.895, .845)], (.85, 1.0, .93)),
        [(.82, 1.02, .94), (.78, 1.04, .95), (.72, 1.06, .958), (.62, 1.15, .965), (.33, 1.25, .975), (0, 1.30, .978)]),
    row("f_axle",
        side(1.3675, [(0, .17), (.60, .18), (.795, .22), (.86, .30), (.888, .43), (.895, .58), (.892, .80)], (.84, 1.40, .875)),
        [(.80, 1.42, .885), (.76, 1.44, .892), (.70, 1.45, .90), (.60, 1.50, .915), (.32, 1.54, .93), (0, 1.55, .935)]),
    row("f_arch_front",
        side(1.76, [(0, .20), (.59, .21), (.78, .24), (.85, .30), (.878, .42), (.884, .56), (.878, .75)], (.82, 1.78, .83)),
        [(.78, 1.79, .838), (.74, 1.80, .844), (.69, 1.81, .85), (.58, 1.83, .86), (.31, 1.85, .872), (0, 1.86, .875)]),
    row("front_end",
        side(1.98, [(0, .22), (.56, .23), (.75, .25), (.83, .31), (.857, .42), (.862, .55), (.852, .68)], (.79, 2.0, .775)),
        [(.75, 2.01, .784), (.71, 2.02, .788), (.66, 2.03, .791), (.55, 2.04, .795), (.29, 2.05, .80), (0, 2.06, .80)]),
]

# The rounded ends, authored explicitly: two corner rows and the tip on the
# centre plane (every level ends there). The bumper levels sweep furthest.
TAIL = [
    row("tail",
        [(0, -2.24, .31), (0, -2.28, .315), (0, -2.305, .34), (0, -2.32, .40), (0, Y_R, .50), (0, -2.326, .64),
         (0, -2.305, .78), (0, -2.29, .925)],
        [(0, -2.30, .975), (0, -2.315, 1.015), (0, -2.325, 1.04), (0, -2.33, 1.052), (0, -2.33, 1.058), (0, -2.33, 1.06)]),
    row("tail2",
        [(0, -2.20, .30), (.28, -2.265, .31), (.38, -2.29, .335), (.42, -2.31, .395), (.44, -2.322, .50), (.44, -2.318, .64),
         (.435, -2.298, .785), (.42, -2.284, .925)],
        [(.405, -2.293, .975), (.38, -2.306, 1.014), (.35, -2.315, 1.039), (.28, -2.32, 1.051), (.15, -2.322, 1.057), (0, -2.324, 1.059)]),
    row("tail1",
        [(0, -2.14, .29), (.50, -2.20, .30), (.68, -2.23, .325), (.755, -2.245, .385), (.78, -2.252, .495), (.785, -2.248, .64),
         (.775, -2.235, .80), (.74, -2.24, .94)],
        [(.715, -2.25, .985), (.67, -2.26, 1.016), (.61, -2.27, 1.037), (.50, -2.278, 1.05), (.27, -2.284, 1.057), (0, -2.29, 1.06)]),
]
NOSE = [
    row("nose1",
        [(0, 2.10, .23), (.48, 2.18, .245), (.65, 2.19, .265), (.715, 2.20, .31), (.735, 2.205, .41), (.735, 2.20, .53),
         (.715, 2.18, .64), (.66, 2.15, .715)],
        [(.62, 2.13, .745), (.585, 2.125, .755), (.545, 2.12, .765), (.45, 2.13, .775), (.24, 2.14, .782), (0, 2.15, .785)]),
    row("nose2",
        [(0, 2.17, .235), (.27, 2.255, .247), (.37, 2.29, .268), (.41, 2.315, .31), (.42, 2.325, .405), (.42, 2.32, .525),
         (.40, 2.295, .625), (.37, 2.255, .705)],
        [(.35, 2.235, .735), (.33, 2.225, .748), (.31, 2.215, .758), (.26, 2.205, .765), (.14, 2.20, .768), (0, 2.195, .769)]),
    row("nose",
        [(0, 2.20, .24), (0, 2.275, .248), (0, 2.31, .27), (0, 2.335, .31), (0, Y_F, .40), (0, 2.338, .52),
         (0, 2.31, .62), (0, 2.27, .70)],
        [(0, 2.25, .73), (0, 2.24, .744), (0, 2.23, .755), (0, 2.22, .762), (0, 2.21, .766), (0, 2.20, .768)]),
]
ROWS = TAIL + MAIN + NOSE

# level creases (across the section) and the rows they run between
LEVEL_CREASE = {"shoulder": "all", "belt": ("tail1", "nose1"), "rail": "all", "sill": ("nose1", "nose"), "waist": ("nose1", "nose")}

R = {r["name"]: i for i, r in enumerate(ROWS)}

# Material regions on the right half in (row, level) net coordinates; later
# entries win. "inset" recesses the region (metres) with a dark wall.
REGIONS = [
    {"mat": "under", "s": (0, len(ROWS) - 1), "l": (0, 1.6)},
    {"mat": "trim", "s": (R["c_base"], R["cowl"]), "l": (L["belt"], L["glass_lo"])},
    {"mat": "trim", "s": (R["c_base"], R["cowl"]), "l": (L["rail"] - .15, L["rail"] + .35)},
    {"mat": "trim", "s": (R["b_rear"], R["b_front"]), "l": (L["belt"], L["rail"] + .35)},
    {"mat": "trim", "s": (R["q_rear"], R["q_front"]), "l": (L["belt"], L["rail"] + .35)},
    {"mat": "glass", "s": (R["c_base"], R["q_rear"]), "l": (L["glass_lo"], L["rail"] - .15), "inset": .006},
    {"mat": "glass", "s": (R["q_front"], R["b_rear"]), "l": (L["glass_lo"], L["rail"] - .15), "inset": .006},
    {"mat": "glass", "s": (R["b_front"], R["cowl"]), "l": (L["glass_lo"], L["rail"] - .15), "inset": .006},
    {"mat": "glass", "s": (R["header"], R["cowl"]), "l": (L["roof_edge"], L["crown"]), "inset": .005},
    {"mat": "trim", "s": (R["header"], R["header"] + .1), "l": (L["roof_edge"], L["crown"])},
    {"mat": "trim", "s": (R["cowl"] - .1, R["cowl"]), "l": (L["roof_edge"], L["crown"])},
    {"mat": "trim", "s": (R["header"], R["cowl"]), "l": (L["roof_edge"], L["roof_edge"] + .22)},
    {"mat": "glass", "s": (R["c_base"], R["roof_rear"]), "l": (L["roof_edge"], L["crown"]), "inset": .005},
    {"mat": "trim", "s": (R["c_base"], R["c_base"] + .12), "l": (L["roof_edge"], L["crown"])},
    {"mat": "trim", "s": (R["roof_rear"] - .1, R["roof_rear"]), "l": (L["roof_edge"], L["crown"])},
    {"mat": "trim", "s": (R["c_base"], R["roof_rear"]), "l": (L["roof_edge"], L["roof_edge"] + .25)},
    {"mat": "glass", "s": (R["b_rear"] + .35, R["header"] - .45), "l": (L["roof_mid"] + .25, L["crown"]), "inset": .004},
    {"mat": "trim", "s": (R["cowl"], R["cowl"] + .3), "l": (L["roof_edge"], L["crown"])},
]


# Skin features. "xyz" outlines are rough points on the right side that snap
# to the skin; front/rear outlines are (x, z) in metres, side ones (y, z).
# Front end from the photograph (the Miata method): outlines traced on a
# head-on photo, the same mapping places the photo on them. Source: "2023
# Cleveland Auto Show (52789253962).jpg", Wikimedia Commons, CC BY 2.0;
# badge painted out (photo_prep.py).
FRONT = photo_map(__import__("os").path.join(__import__("os").path.dirname(__file__), "..", "photos", "kestrel_s_front.jpg"),
                  cx=665, sx=738, z0=.755, py0=28, sz=1010)
_o = lambda pts: [(x - 280, y - 640) for (x, y) in pts]
PHOTO_FRONT = [
    photo_patch("headlamp", FRONT, _o([(315, 668), (390, 718), (490, 752), (590, 782), (640, 815), (615, 842), (590, 842),
                                       (490, 822), (390, 802), (325, 786), (310, 760)]), "photo_front", rough=.15, coat=1.0),
    photo_patch("grille", FRONT, _o([(945, 858), (800, 858), (700, 838), (648, 826), (670, 905), (720, 935), (800, 942), (945, 946)]),
                "photo_front", lift=.004),
    photo_patch("intake", FRONT, _o([(945, 994), (640, 998), (612, 1012), (560, 1100), (525, 1158), (700, 1165), (945, 1168)]), "photo_front"),
    photo_patch("corner_intake", FRONT, _o([(318, 948), (480, 972), (430, 1085), (520, 1150), (500, 1165), (330, 1100), (312, 1000)]), "photo_front"),
]

# Tail from a head-on rear photograph: "2021 Honda Civic sedan RS in Taman
# Adipura Indah - Jambi City, JA.jpg" by Firzafp, Wikimedia Commons, CC BY-SA
# 4.0 (badges and plate painted out, white paint swapped for this car's red).
REAR = photo_map(__import__("os").path.join(__import__("os").path.dirname(__file__), "..", "photos", "kestrel_s_rear.jpg"),
                 cx=590, sx=653, z0=.93, py0=100, sz=895, view="rear")
_r = lambda pts: [(x - 30, y - 280) for (x, y) in pts]
PHOTO_REAR = [
    photo_patch("tail", REAR, _r([(620, 376), (1050, 374), (1118, 396), (1138, 450), (1140, 560), (1146, 700), (1146, 820),
                                 (1118, 893), (1060, 928), (620, 933)]), "lamp_rear", lift=.004, rough=.25, coat=1.0,
                emit_mask=__import__("os").path.join(__import__("os").path.dirname(__file__), "..", "photos", "kestrel_s_rear_emit.jpg")),
]

FEATURES = PHOTO_FRONT + PHOTO_REAR + [
    {"kind": "patch", "name": "front_lip", "view": "front", "lift": 0.002,
     "outline": [(0, .248), (.55, .262), (.66, .29), (.62, .30), (0, .268)], "mat": "satin"},
    {"kind": "patch", "name": "plate_front", "view": "front", "lift": 0.006,
     "outline": [(0, .325), (.155, .325), (.155, .435), (0, .435)], "uv": "view", "mat": {"tex": "image", "name": "plate_f", "path": PLATE}},
    {"kind": "patch", "name": "taillamp", "view": "xyz", "radius": 0.05, "lift": 0.0025,
     "outline": [(.862, -1.90, .878), (.872, -2.05, .80), (.835, -2.18, .765), (.72, -2.255, .76), (.55, -2.285, .79),
                 (.40, -2.298, .82), (.27, -2.305, .846), (.27, -2.302, .876), (.45, -2.296, .882), (.65, -2.28, .905),
                 (.78, -2.24, .925), (.85, -2.14, .93), (.868, -2.0, .915)],
     "mat": {"tex": "taillamp", "name": "lamp_rear", "size": (512, 160), "reverse_u": (0.06, 0.2), "guides": [(0.16, 0.05), (0.36, 0.03)]}},
    {"kind": "patch", "name": "plate_rear", "view": "rear", "lift": 0.008,
     "outline": [(0, .615), (.155, .615), (.155, .767), (0, .767)], "uv": "view", "mat": {"tex": "image", "name": "plate_r", "path": PLATE}},
    # shut lines
    {"kind": "line", "name": "door_front", "view": "side", "path": [(.80, .935), (.855, .76), (.875, .5), (.866, .272)]},
    {"kind": "line", "name": "door_mid", "view": "side", "path": [(-.215, .952), (-.226, .6), (-.22, .275)]},
    {"kind": "line", "name": "door_rear", "view": "side", "path": [(-1.225, .965), (-1.17, .742), (-1.07, .554), (-.915, .40), (-.73, .315), (-.64, .29)]},
    {"kind": "line", "name": "door_sill", "view": "side", "path": [(.866, .272), (.2, .275), (-.64, .29)]},
    {"kind": "line", "name": "fender_bumper", "view": "side", "path": [(1.70, .715), (1.73, .64), (1.745, .58)]},
    {"kind": "line", "name": "hood_edge", "view": "sl", "path": [(R["cowl"] + .45, L["rail"]), (R["f_axle"], L["rail"]), (R["front_end"], L["rail"]), (R["nose1"], L["rail"]), (R["nose2"], L["rail"]), (R["nose"], L["rail"])], "width": 0.004},
    {"kind": "line", "name": "lid_edge", "view": "sl", "path": [(R["c_base"] + .25, L["rail"] + .1), (R["r_axle"] - .3, L["rail"] + .1), (R["r_arch_back"], L["rail"] + .1), (R["rear_end"], L["rail"] + .1), (R["tail1"] + .2, L["rail"] + .1)], "width": 0.004},
    {"kind": "line", "name": "lid_rear", "view": "xyz", "path": [(.60, -2.27, 1.03), (.62, -2.29, .93), (.585, -2.30, .80), (.50, -2.31, .735), (.0, -2.325, .725)], "width": 0.004},
    {"kind": "line", "name": "bumper_rear", "view": "xyz", "path": [(.88, -1.80, .70), (.86, -2.05, .73), (.80, -2.19, .745)], "width": 0.0035},
]

DESIGN = {
    "id": "kestrel_s",
    "length": Y_F - Y_R, "width": 1.801, "height": 1.415, "wheelbase": WB,
    "track_f": 1.547, "track_r": 1.575, "tire_w": .235, "tire_r": .3226, "rim_in": 18,
    "rows": ROWS, "ds": .036, "dl": .026, "level_crease": LEVEL_CREASE, "regions": REGIONS,
    "arch_r": (.372, .372), "arch_z": .345,
    "paint": "#b5150f", "rim": "#2c2f33", "caliper": "#b01c1c", "spokes": "split5",
    "features": FEATURES,
    "eye": (-.36, .021, 1.14),
    "cabin": {"seat_accent": "#6e1114", "accent": "#e23b2b", "shifter": "manual", "screen": (.22, .125), "screen_up": .07,
              "dash_ahead": .70, "dash_top_z": .955, "cowl": (1.08, .975), "floor_z": .24, "shelf_z": 1.0, "rear_seat_y": -.86,
              "mirror": (0, .47, 1.31), "cluster_up": .02, "screen_ahead": -.02},
    "cabin_rows": ("c_base", "f_arch_back"),
    "mirror": {"at": (.85, .58, .96), "size": (.2, .10, .125), "mat": "trim", "out": 0.035, "dz": 0.075, "dy": -0.03},
    "handles": [{"at": (.9, -.07, .812), "mat": "paint"}, {"at": (.9, -.99, .825), "mat": "paint"}],
    "wing": {"span": 1.14, "chord": .12, "y": -2.16, "z": 1.078, "thick": .018, "sweep": .025, "rise": .004, "stand_h": .012, "plate_h": .022},
    "exhaust": {"x": [.42], "y": -2.27, "z": .31, "r": .05},
    "fin": {"y": -.86},
    "wipers": {"blades": [(-.62, .02, 1.0, 1.0), (.04, .55, 1.02, .99)], "angle": 3},
}
