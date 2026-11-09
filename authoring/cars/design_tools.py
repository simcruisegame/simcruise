"""Helpers for car design files (authoring/cars/designs/*.py).

A design lists *main rows* from the rear of the car to the front. Each row
gives every level a point (x, y, z) in metres: x to the right, y forward
from the midpoint between the axles, z up from the ground. The rounded ends
are added here: from each level's last main point the plan curve turns in
along a superellipse quadrant to that level's tip on the centre plane.
"""
import math

LEVELS = ["keel", "floor", "rocker", "sill", "lower", "waist", "shoulder", "belt",
          "glass_lo", "glass_hi", "rail", "roof_edge", "roof_mid", "crown"]
SIDE = LEVELS[:8]
TOP = LEVELS[8:]
L = {name: i for i, name in enumerate(LEVELS)}


def row(name, side, top, crease=()):
    """side: 8 (x, y, z) for keel..belt; top: 6 (x, y, z) for glass_lo..crown."""
    assert len(side) == 8 and len(top) == 6, name
    return {"name": name, "pts": [tuple(map(float, p)) for p in list(side) + list(top)], "crease": set(crease)}


def section(y, xz, dy=None):
    """Points at one station: xz is a list of (x, z); dy optional per-point y offsets."""
    dy = dy or [0.0] * len(xz)
    return [(x, y + d, z) for (x, z), d in zip(xz, dy)]


def ends(main_rows, rear_tips, front_tips, squareness=2.6, rear_squareness=None, front_squareness=None,
         rear_angles=(32, 66), front_angles=(32, 66)):
    """Add the rounded ends. *_tips: per level (y, z) on the centre plane.
    The corner rows sit at the given angles (degrees) along the quadrant."""
    def corner_rows(edge, tips, n, angles, label):
        rows = []
        for a in angles:
            th = math.radians(a)
            c = math.cos(th) ** (2.0 / n)       # fraction of full width
            s = 1.0 - math.sin(th) ** (2.0 / n)  # fraction of the run-out
            pts = []
            for (x, y, z), (ty, tz) in zip(edge["pts"], tips):
                pts.append((x * c, ty + (y - ty) * s, tz + (z - tz) * (c ** 1.3)))
            rows.append({"name": f"{label}{a}", "pts": pts, "crease": set()})
        return rows
    rs = rear_squareness or squareness
    fs = front_squareness or squareness
    rear = corner_rows(main_rows[0], rear_tips, rs, rear_angles, "tail")
    front = corner_rows(main_rows[-1], front_tips, fs, front_angles, "nose")
    tail = {"name": "tail", "pts": [(0.0, y, z) for (y, z) in rear_tips], "crease": set()}
    nose = {"name": "nose", "pts": [(0.0, y, z) for (y, z) in front_tips], "crease": set()}
    return [tail] + list(reversed(rear)) + list(main_rows) + front + [nose]


def row_index(rows, name):
    for i, r in enumerate(rows):
        if r["name"] == name:
            return i
    raise KeyError(name)


def lerp(a, b, t):
    return a + (b - a) * t


def sedan_regions(R, quarter=True, sunroof=None, glass_roof=None, frame="trim", a_pillar=None, c_pillar=None,
                  windscreen_frit=0.1, belt_seal=True):
    """Standard material regions for a four-door greenhouse whose rows are
    named like the sedan designs (c_base, q_rear, q_front, roof_rear, b_rear,
    b_front, header, cowl). Returns a list for DESIGN["regions"]."""
    Lg = L
    last = max(R.values())
    regs = [{"mat": "under", "s": (0, last), "l": (0, 1.6)}]
    if belt_seal:
        regs.append({"mat": frame, "s": (R["c_base"], R["cowl"]), "l": (Lg["belt"], Lg["glass_lo"])})
    regs.append({"mat": frame, "s": (R["c_base"], R["cowl"]), "l": (Lg["rail"] - .15, Lg["rail"] + .35)})
    regs.append({"mat": "trim", "s": (R["b_rear"], R["b_front"]), "l": (Lg["belt"], Lg["rail"] + .35)})
    if quarter:
        regs.append({"mat": "trim", "s": (R["q_rear"], R["q_front"]), "l": (Lg["belt"], Lg["rail"] + .35)})
        regs.append({"mat": "glass", "s": (R["c_base"], R["q_rear"]), "l": (Lg["glass_lo"], Lg["rail"] - .15), "inset": .006})
        regs.append({"mat": "glass", "s": (R["q_front"], R["b_rear"]), "l": (Lg["glass_lo"], Lg["rail"] - .15), "inset": .006})
    else:
        regs.append({"mat": "glass", "s": (R["c_base"], R["b_rear"]), "l": (Lg["glass_lo"], Lg["rail"] - .15), "inset": .006})
    regs.append({"mat": "glass", "s": (R["b_front"], R["cowl"]), "l": (Lg["glass_lo"], Lg["rail"] - .15), "inset": .006})
    if a_pillar:
        regs.append({"mat": a_pillar, "s": (R["header"], R["cowl"]), "l": (Lg["rail"] + .35, Lg["roof_edge"])})
    if c_pillar:
        regs.append({"mat": c_pillar, "s": (R["c_base"], R["roof_rear"]), "l": (Lg["rail"] + .35, Lg["roof_edge"])})
    # windscreen with a dark frit band
    regs += [{"mat": "glass", "s": (R["header"], R["cowl"]), "l": (Lg["roof_edge"], Lg["crown"]), "inset": .005},
             {"mat": "trim", "s": (R["header"], R["header"] + windscreen_frit), "l": (Lg["roof_edge"], Lg["crown"])},
             {"mat": "trim", "s": (R["cowl"] - .1, R["cowl"]), "l": (Lg["roof_edge"], Lg["crown"])},
             {"mat": "trim", "s": (R["header"], R["cowl"]), "l": (Lg["roof_edge"], Lg["roof_edge"] + .22)}]
    if glass_roof:
        # backlight and roof as one dark glass panel up to the header bar
        regs += [{"mat": "glass", "s": (R["c_base"], R["header"] - glass_roof), "l": (Lg["roof_edge"], Lg["crown"]), "inset": .005},
                 {"mat": "trim", "s": (R["c_base"], R["c_base"] + .12), "l": (Lg["roof_edge"], Lg["crown"])},
                 {"mat": "trim", "s": (R["c_base"], R["header"] - glass_roof), "l": (Lg["roof_edge"], Lg["roof_edge"] + .25)},
                 {"mat": "trim", "s": (R["header"] - glass_roof - .08, R["header"] - glass_roof), "l": (Lg["roof_edge"], Lg["crown"])}]
    else:
        regs += [{"mat": "glass", "s": (R["c_base"], R["roof_rear"]), "l": (Lg["roof_edge"], Lg["crown"]), "inset": .005},
                 {"mat": "trim", "s": (R["c_base"], R["c_base"] + .12), "l": (Lg["roof_edge"], Lg["crown"])},
                 {"mat": "trim", "s": (R["roof_rear"] - .1, R["roof_rear"]), "l": (Lg["roof_edge"], Lg["crown"])},
                 {"mat": "trim", "s": (R["c_base"], R["roof_rear"]), "l": (Lg["roof_edge"], Lg["roof_edge"] + .25)}]
    if sunroof:
        regs.append({"mat": "glass", "s": (R["b_rear"] + sunroof[0], R["header"] - sunroof[1]), "l": (Lg["roof_mid"] + .25, Lg["crown"]), "inset": .004})
    regs.append({"mat": "trim", "s": (R["cowl"], R["cowl"] + .3), "l": (Lg["roof_edge"], Lg["crown"])})
    return regs


def standard_lines(R, door_front, door_mid, door_rear, sill, lid=True, lid_rear=None, extra=()):
    """Shut lines: doors (side-view paths), hood edge and trunk lid along the
    rail level, plus any extra line entries."""
    out = [{"kind": "line", "name": "door_front", "view": "side", "path": door_front},
           {"kind": "line", "name": "door_mid", "view": "side", "path": door_mid},
           {"kind": "line", "name": "door_rear", "view": "side", "path": door_rear},
           {"kind": "line", "name": "door_sill", "view": "side", "path": sill},
           {"kind": "line", "name": "hood_edge", "view": "sl", "width": 0.004,
            "path": [(R["cowl"] + .45, L["rail"]), (R["f_axle"], L["rail"]), (R["front_end"], L["rail"]), (R["nose1"], L["rail"]), (R["nose2"], L["rail"]), (R["nose"], L["rail"])]}]
    if lid:
        out.append({"kind": "line", "name": "lid_edge", "view": "sl", "width": 0.004,
                    "path": [(R["c_base"] + .25, L["rail"] + .1), (R["r_axle"] - .3, L["rail"] + .1), (R["r_arch_back"], L["rail"] + .1), (R["rear_end"], L["rail"] + .1), (R["tail1"] + .2, L["rail"] + .1)]})
    if lid_rear:
        out.append({"kind": "line", "name": "lid_rear", "view": "xyz", "path": lid_rear, "width": 0.004})
    return out + list(extra)
