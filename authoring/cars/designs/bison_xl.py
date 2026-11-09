"""Bison XL -- reference: 2021 Ford F-150 SuperCrew 5.5 ft box, 2.7 EcoBoost 4x4.

Published dimensions: length 5890 mm, width 2029 mm without mirrors, height
1961 mm, wheelbase 3683 mm, track 1717 mm; 265/70R17 tyres as in the car
file. Commons photographs (side: "2021-2023 Ford F-150 Crew Cab Timberland",
front and rear three-quarter views listed in docs/cars.md). The cab and
front end are one skin whose tail is the flat cab back; the bed is a
separate tub (parts.py).
"""
from design_tools import row, L

PLATE = __import__("os").path.join(__import__("os").path.dirname(__file__), "..", "..", "..", "game", "art", "plate.png")
WB = 3.683
YA = WB / 2
Y_F, Y_R = YA + .94, -YA - 1.267
CAB = [(0, .48), (.70, .48), (.94, .50), (.985, .56), (1.0, .72), (1.005, .95), (1.008, 1.27)]


def side(y, xz, belt):
    return [(x, y, z) for (x, z) in xz] + [belt]


def cab_top(y, dy_roof=0.0):
    return [(.965, y, 1.38), (.92, y, 1.62), (.875, y, 1.86), (.83, y + dy_roof, 1.935), (.45, y + dy_roof, 1.958), (0, y + dy_roof, 1.961)]


MAIN = [
    row("rear_end", side(-1.28, CAB, (.97, -1.28, 1.37)),
        [(.96, -1.28, 1.38), (.915, -1.28, 1.62), (.87, -1.28, 1.855), (.825, -1.28, 1.93), (.45, -1.28, 1.955), (0, -1.28, 1.958)],
        crease=["rail"]),
    row("c_base", side(-1.12, CAB, (.972, -1.12, 1.37)), cab_top(-1.12), crease=[]),
    row("q_rear", side(-0.98, CAB, (.973, -.98, 1.37)), cab_top(-.98)),
    row("q_front", side(-0.86, CAB, (.974, -.86, 1.37)), cab_top(-.86)),
    row("roof_rear", side(-0.60, CAB, (.975, -.60, 1.37)), cab_top(-.60)),
    row("b_rear", side(-0.12, CAB, (.975, -.12, 1.37)), cab_top(-.12)),
    row("b_front", side(0.0, CAB, (.975, 0.0, 1.37)), cab_top(0.0)),
    row("header", side(0.50, CAB, (.975, .50, 1.37)),
        [(.965, .50, 1.38), (.92, .52, 1.62), (.86, .55, 1.85), (.82, .62, 1.92), (.44, .66, 1.945), (0, .67, 1.95)],
        crease=["roof_edge", "roof_mid", "crown"]),
    row("cowl", side(0.95, CAB[:6] + [(1.008, 1.265)], (.975, .95, 1.37)),
        [(.966, .95, 1.371), (.965, .9505, 1.372), (.964, .951, 1.373), (.86, 1.30, 1.32), (.46, 1.36, 1.325), (0, 1.38, 1.325)],
        crease=["glass_lo", "glass_hi", "rail", "roof_edge", "roof_mid", "crown"]),
    row("f_arch_back", side(1.35, [(0, .50), (.70, .50), (.94, .53), (.99, .60), (1.01, .75), (1.015, .95), (1.012, 1.24)], (.985, 1.40, 1.31)),
        [(.95, 1.45, 1.315), (.90, 1.50, 1.318), (.86, 1.55, 1.32), (.72, 1.60, 1.322), (.40, 1.62, 1.325), (0, 1.63, 1.326)]),
    row("f_axle", side(YA, [(0, .52), (.70, .52), (.94, .55), (.99, .62), (1.012, .76), (1.015, .95), (1.012, 1.22)], (.985, 1.86, 1.30)),
        [(.95, 1.88, 1.305), (.90, 1.90, 1.308), (.86, 1.92, 1.31), (.72, 1.94, 1.312), (.40, 1.96, 1.314), (0, 1.97, 1.315)]),
    row("f_arch_front", side(2.30, [(0, .50), (.70, .50), (.93, .53), (.98, .60), (1.0, .75), (1.005, .95), (1.0, 1.20)], (.975, 2.30, 1.29)),
        [(.94, 2.31, 1.295), (.89, 2.32, 1.298), (.85, 2.33, 1.30), (.71, 2.34, 1.302), (.39, 2.35, 1.304), (0, 2.36, 1.305)]),
    row("front_end", side(2.55, [(0, .46), (.68, .46), (.90, .49), (.95, .56), (.97, .72), (.975, .95), (.97, 1.19)], (.945, 2.56, 1.28)),
        [(.91, 2.57, 1.285), (.86, 2.58, 1.288), (.82, 2.59, 1.29), (.69, 2.60, 1.292), (.38, 2.61, 1.294), (0, 2.62, 1.295)]),
]
TAIL = [
    row("tail", [(0, -1.33, .48), (0, -1.335, .48), (0, -1.34, .50), (0, -1.342, .56), (0, -1.345, .72), (0, -1.345, .95), (0, -1.345, 1.27), (0, -1.345, 1.37)],
        [(0, -1.345, 1.38), (0, -1.345, 1.62), (0, -1.345, 1.855), (0, -1.34, 1.93), (0, -1.335, 1.955), (0, -1.335, 1.958)]),
    row("tail2", [(0, -1.32, .48), (.32, -1.33, .48), (.43, -1.335, .50), (.45, -1.338, .56), (.46, -1.34, .72), (.46, -1.34, .95), (.46, -1.34, 1.27), (.445, -1.34, 1.37)],
        [(.44, -1.34, 1.38), (.42, -1.34, 1.62), (.40, -1.34, 1.855), (.38, -1.335, 1.93), (.21, -1.33, 1.955), (0, -1.33, 1.958)]),
    row("tail1", [(0, -1.30, .48), (.64, -1.31, .48), (.86, -1.315, .50), (.90, -1.32, .56), (.915, -1.322, .72), (.92, -1.322, .95), (.92, -1.322, 1.27), (.89, -1.322, 1.37)],
        [(.88, -1.322, 1.38), (.84, -1.322, 1.62), (.80, -1.322, 1.855), (.76, -1.31, 1.93), (.41, -1.30, 1.955), (0, -1.30, 1.958)]),
]
NOSE = [
    row("nose1", [(0, 2.66, .44), (.62, 2.67, .45), (.82, 2.70, .48), (.865, 2.72, .55), (.88, 2.73, .72), (.882, 2.73, .95), (.875, 2.725, 1.19), (.85, 2.715, 1.275)],
        [(.82, 2.70, 1.282), (.78, 2.69, 1.285), (.74, 2.685, 1.288), (.62, 2.68, 1.29), (.34, 2.675, 1.292), (0, 2.67, 1.293)]),
    row("nose2", [(0, 2.68, .44), (.32, 2.73, .45), (.43, 2.755, .48), (.46, 2.77, .55), (.47, 2.775, .72), (.47, 2.775, .95), (.465, 2.77, 1.19), (.45, 2.76, 1.275)],
        [(.43, 2.745, 1.283), (.41, 2.73, 1.286), (.39, 2.72, 1.289), (.33, 2.71, 1.291), (.18, 2.70, 1.292), (0, 2.695, 1.293)]),
    row("nose", [(0, 2.70, .44), (0, 2.76, .45), (0, 2.775, .48), (0, 2.78, .55), (0, 2.78, .72), (0, 2.78, .95), (0, 2.775, 1.19), (0, 2.765, 1.275)],
        [(0, 2.75, 1.284), (0, 2.735, 1.287), (0, 2.725, 1.29), (0, 2.715, 1.292), (0, 2.705, 1.293), (0, 2.70, 1.294)]),
]
ROWS = TAIL + MAIN + NOSE
LEVEL_CREASE = {"shoulder": "all", "belt": "all", "rail": "all", "sill": ("nose1", "nose"), "waist": ("nose1", "nose")}
R = {r["name"]: i for i, r in enumerate(ROWS)}
Lg = L
REGIONS = [
    {"mat": "under", "s": (0, len(ROWS) - 1), "l": (0, 1.6)},
    {"mat": "trim", "s": (R["c_base"], R["cowl"]), "l": (Lg["belt"], Lg["glass_lo"])},
    {"mat": "trim", "s": (R["c_base"], R["cowl"]), "l": (Lg["rail"] - .15, Lg["rail"] + .35)},
    {"mat": "trim", "s": (R["b_rear"], R["b_front"]), "l": (Lg["belt"], Lg["rail"] + .35)},
    {"mat": "trim", "s": (R["rear_end"], R["c_base"]), "l": (Lg["belt"], Lg["rail"] + .35)},
    {"mat": "glass", "s": (R["c_base"], R["b_rear"]), "l": (Lg["glass_lo"], Lg["rail"] - .15), "inset": .006},
    {"mat": "glass", "s": (R["b_front"], R["cowl"]), "l": (Lg["glass_lo"], Lg["rail"] - .15), "inset": .006},
    {"mat": "glass", "s": (R["header"], R["cowl"]), "l": (Lg["roof_edge"], Lg["crown"]), "inset": .005},
    {"mat": "trim", "s": (R["header"], R["header"] + .1), "l": (Lg["roof_edge"], Lg["crown"])},
    {"mat": "trim", "s": (R["cowl"] - .1, R["cowl"]), "l": (Lg["roof_edge"], Lg["crown"])},
    {"mat": "trim", "s": (R["header"], R["cowl"]), "l": (Lg["roof_edge"], Lg["roof_edge"] + .2)},
    {"mat": "trim", "s": (R["cowl"], R["cowl"] + .3), "l": (Lg["roof_edge"], Lg["crown"])},
    # rear window in the cab back
    {"mat": "trim", "s": (R["tail"], R["tail2"] + .75), "l": (Lg["glass_lo"] + .1, Lg["rail"] - .05)},
    {"mat": "glass", "s": (R["tail"], R["tail2"] + .62), "l": (Lg["glass_lo"] + .3, Lg["rail"] - .2), "inset": .006},
]

FEATURES = [
    {"kind": "patch", "name": "grille", "view": "front", "lift": 0.004, "radius": 0.03,
     "outline": [(0, .84), (.60, .84), (.66, .88), (.67, 1.17), (.62, 1.25), (0, 1.262)],
     "mat": {"tex": "grille", "name": "grille_bars", "pattern": "slats", "scale": 9, "line": 0.22, "base": "#0c0d0e", "rib": "#8d9196", "rough": 0.3, "metal": 0.6, "coat": 0.4}},
    {"kind": "patch", "name": "headlamp", "view": "front", "lift": 0.004, "radius": 0.03,
     "outline": [(.69, .98), (.90, .975), (.94, 1.03), (.94, 1.235), (.71, 1.255), (.675, 1.21)],
     "mat": {"tex": "headlamp", "name": "lamp_front", "size": (256, 192), "cells": [0.35, 0.62], "cell_r": 0.09,
             "amber_u": (0.86, 1.0), "pipe_in": 0.08, "pipe_w": 0.05, "emission": 2.0}},
    {"kind": "patch", "name": "fog", "view": "front", "lift": 0.003, "radius": 0.02,
     "outline": [(.70, .55), (.84, .55), (.84, .62), (.70, .62)], "mat": "satin"},
    {"kind": "patch", "name": "lower_valance", "view": "front", "lift": 0.003, "radius": 0.02,
     "outline": [(0, .45), (.60, .45), (.66, .52), (.62, .60), (0, .60)],
     "mat": {"tex": "grille", "name": "grille_mesh", "pattern": "mesh", "scale": 30, "line": 0.18}},
    {"kind": "patch", "name": "plate_front", "view": "front", "lift": 0.012,
     "outline": [(0, .62), (.155, .62), (.155, .77), (0, .77)], "uv": "view", "mat": {"tex": "image", "name": "plate_f", "path": PLATE}},
    {"kind": "patch", "name": "fender_vent", "view": "side", "lift": 0.002, "radius": 0.01,
     "outline": [(1.18, 1.18), (1.24, 1.18), (1.22, .98), (1.17, 1.02)], "mat": "satin"},
    {"kind": "line", "name": "door_front", "view": "side", "path": [(.95, 1.37), (1.0, 1.0), (1.02, .60)]},
    {"kind": "line", "name": "door_mid", "view": "side", "path": [(-.06, 1.37), (-.07, .60)]},
    {"kind": "line", "name": "door_rear", "view": "side", "path": [(-1.20, 1.37), (-1.22, .60)]},
    {"kind": "line", "name": "door_sill", "view": "side", "path": [(1.02, .60), (0, .60), (-1.22, .60)]},
    {"kind": "line", "name": "hood_edge", "view": "sl", "width": 0.005,
     "path": [(R["cowl"] + .45, L["rail"]), (R["f_axle"], L["rail"]), (R["front_end"], L["rail"]), (R["nose1"], L["rail"]), (R["nose2"], L["rail"]), (R["nose"], L["rail"])]},
]

DESIGN = {
    "id": "bison_xl",
    "length": Y_F - Y_R, "width": 2.029, "height": 1.961, "wheelbase": WB,
    "track_f": 1.717, "track_r": 1.717, "tire_w": .265, "tire_r": .4014, "rim_in": 17,
    "rows": ROWS, "ds": .04, "dl": .03, "level_crease": LEVEL_CREASE, "regions": REGIONS,
    "arch_r": (.47, .47), "arch_z": .43, "well_inner": .40,
    "paint": "#7a1b1d", "paint_metal": 0.5, "rim": "#a3a8ad", "caliper": "#2b2d30", "spokes": "six", "lugs": 6, "dish": 0.035,
    "features": FEATURES,
    "eye": (-.406, .167, 1.66),
    "cabin": {"seat": "#2a2b2e", "seat_accent": "#2a2b2e", "dash": "#232426", "shifter": "none", "screen": (.30, .19), "screen_up": -.03,
              "screen_ahead": -.05, "dash_ahead": .72, "dash_top_z": 1.36, "cowl": (1.30, 1.32), "floor_z": .62, "shelf_z": 1.40,
              "rear_seat_y": -.80, "mirror": (0, .58, 1.82), "hip_below": .62, "vent_drop": .12},
    "cabin_rows": ("rear_end", "f_arch_back"),
    "mirror": {"at": (.975, .93, 1.38), "size": (.16, .12, .30), "mat": "trim", "out": 0.06, "dz": 0.12, "dy": -0.02},
    "handles": [{"at": (1.02, .22, 1.20), "mat": "paint"}, {"at": (1.02, -.88, 1.20), "mat": "paint"}],
    "bed": {"y_front": -1.38, "y_rear": -3.10, "half_w": 1.0, "inner_half_w": .835, "tub_x": .635, "z_bottom": .58, "z_top": 1.31, "z_floor": .80,
            "axle_y": -YA, "arch_r": .47, "arch_z": .43, "lamp_z": (.86, 1.24), "bumper_z": (.50, .72), "frame_front": 1.9, "gate": .05},
    "boxes": [
        {"name": "bumper_front", "lo": (-.98, 2.74, .46), "hi": (.98, 2.86, .84), "mat": "chrome", "bevel": .04},
        {"name": "skid_plate", "lo": (-.55, 2.55, .40), "hi": (.55, 2.78, .47), "mat": "satin", "bevel": .02},
        {"name": "step_r", "lo": (.98, -1.15, .50), "hi": (1.10, 1.15, .56), "mat": "satin", "bevel": .015},
        {"name": "step_l", "lo": (-1.10, -1.15, .50), "hi": (-.98, 1.15, .56), "mat": "satin", "bevel": .015},
        {"name": "frame_front_r", "lo": (.42, -1.38, .36), "hi": (.55, 2.5, .56), "mat": "under"},
        {"name": "frame_front_l", "lo": (-.55, -1.38, .36), "hi": (-.42, 2.5, .56), "mat": "under"},
        {"name": "cab_gap", "lo": (-.88, -1.385, .58), "hi": (.88, -1.34, 1.28), "mat": "under"},
    ],
    "wipers": {"blades": [(-.70, .02, 1.36, 1.35), (.04, .66, 1.38, 1.34)], "angle": 3},
}
