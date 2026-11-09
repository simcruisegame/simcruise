"""Wheels for build_car.py: tyre with sidewall and tread grooves, a rim with a
spoke pattern, lug nuts and centre cap, and the brake disc and caliper.

Each wheel is a node wheel_fl/fr/rl/rr with its origin at the hub (it spins in
game); the disc and caliper hang from wheel_*_upright (steers, does not spin).
Spoke styles: split5 (five twin spokes), y5 (five Y spokes), six, aero (flat
face with slots), mesh (ten thin spokes), multi (fifteen fine spokes).
"""
import math
import bmesh
import bpy


def build(D, MATS, lathe, box, link):
    RT, TW = D["tire_r"], D["tire_w"]
    WB = D["wheelbase"]
    rim_r = D["rim_in"] * 0.0254 / 2
    for nm, y, track, tw, rr in (("wheel_fl", WB / 2, D["track_f"], TW, rim_r), ("wheel_fr", WB / 2, D["track_f"], TW, rim_r),
                                 ("wheel_rl", -WB / 2, D["track_r"], D.get("tire_w_rear", TW), D.get("rim_in_rear", D["rim_in"]) * 0.0254 / 2),
                                 ("wheel_rr", -WB / 2, D["track_r"], D.get("tire_w_rear", TW), D.get("rim_in_rear", D["rim_in"]) * 0.0254 / 2)):
        s = 1 if nm.endswith("r") else -1
        wheel(D, MATS, lathe, box, link, nm, (s * track / 2, y, RT), s, tw, rr, D.get("tire_r_rear", RT) if y < 0 else RT)


def wheel(D, MATS, lathe, box, link, name, loc, out, tw, rim_r, RT):
    hw = tw / 2
    side = rim_r + 0.012
    # tyre: bead, sidewall bulge, shoulder, tread with three grooves
    prof = [(rim_r - 0.004, -hw * 0.86), (side, -hw * 0.94), (side + (RT - side) * 0.35, -hw * 1.03),
            (side + (RT - side) * 0.7, -hw * 1.02), (RT - 0.012, -hw * 0.93), (RT - 0.002, -hw * 0.8)]
    tread = []
    for k, x in enumerate((-0.62, -0.38, -0.36, -0.12, -0.10, 0.10, 0.12, 0.36, 0.38, 0.62)):
        groove = k in (2, 3, 6, 7)
        tread.append((RT - (0.009 if groove else 0.0), hw * x))
    prof += tread + [(r, -x) for (r, x) in reversed(prof)]
    tyre = lathe(name, prof, 56, "rubber")
    tyre.location = loc
    parts = []
    parts.append(lathe(name + "_barrel", [(rim_r - 0.014, -hw * 0.88), (rim_r - 0.022, 0.0), (rim_r - 0.014, hw * 0.88)], 64, "rim"))
    parts.append(lathe(name + "_lip", [(rim_r - 0.014, out * hw * 0.86), (rim_r + 0.008, out * hw * 0.9),
                                        (rim_r + 0.010, out * hw * 0.84), (rim_r - 0.006, out * hw * 0.78)], 72, "rim"))
    face_x = out * hw * 0.62
    style = D.get("spokes", "split5")
    bm = bmesh.new()
    hub_r = 0.085
    dish = D.get("dish", 0.028)

    def prism(a0, a1, r0, r1, w0, w1, depth=0.032, twist=0.0):
        """Spoke from radius r0 (width w0) to r1 (width w1) at angle a0..a1 (centre lines)."""
        v = []
        for (r, w, a) in ((r0, w0, a0), (r1, w1, a1)):
            xf = face_x - out * dish * ((r - r0) / max(r1 - r0, 1e-6))
            for sgn in (-1, 1):
                aa = a + sgn * (w / 2) / max(r, 1e-3) + twist * sgn * 0
                for dpt in (0.0, depth):
                    v.append(bm.verts.new((xf - out * dpt, r * math.cos(aa), r * math.sin(aa))))
        # v: [r0-, r0- back, r0+, r0+ back, r1-, r1- back, r1+, r1+ back]
        quads = [(0, 4, 6, 2), (1, 3, 7, 5), (0, 1, 5, 4), (2, 6, 7, 3), (0, 2, 3, 1), (4, 5, 7, 6)]
        for q in quads:
            f = [v[i] for i in q]
            if out < 0:
                f = list(reversed(f))
            try:
                bm.faces.new(f)
            except ValueError:
                pass

    n = {"split5": 5, "y5": 5, "six": 6, "mesh": 10, "multi": 15, "aero": 5, "five": 5}.get(style, 5)
    rim_in = rim_r - 0.02
    for k in range(n):
        a = 2 * math.pi * k / n
        if style == "split5":
            for d in (-0.13, 0.13):
                prism(a + d * 0.35, a + d, hub_r, rim_in, 0.034, 0.03)
        elif style == "y5":
            prism(a, a, hub_r, rim_r * 0.62, 0.05, 0.04)
            for d in (-0.2, 0.2):
                prism(a + d * 0.45, a + d, rim_r * 0.58, rim_in, 0.032, 0.028)
        elif style == "six":
            prism(a, a, hub_r, rim_in, 0.07, 0.085, depth=0.04)
        elif style == "mesh":
            prism(a, a + 0.05, hub_r, rim_in, 0.026, 0.024)
        elif style == "multi":
            prism(a, a + 0.12, hub_r, rim_in, 0.018, 0.016, depth=0.025)
        elif style == "aero":
            prism(a + 0.3, a + 0.45, hub_r, rim_in, 0.11, 0.17, depth=0.02)
        else:
            prism(a, a, hub_r, rim_in, 0.055, 0.05)
    me = bpy.data.meshes.new(name + "_spokes"); bm.to_mesh(me); bm.free()
    sp = link(bpy.data.objects.new(name + "_spokes", me)); sp.data.materials.append(MATS["rim"])
    bev = sp.modifiers.new("bevel", "BEVEL"); bev.width = 0.0035; bev.segments = 2; bev.limit_method = "ANGLE"
    for p in sp.data.polygons:
        p.use_smooth = False
    parts.append(sp)
    if style == "aero":
        # flat aerodynamic face with the spokes as slots behind it
        parts.append(lathe(name + "_aeroface", [(hub_r, face_x + out * 0.004), (rim_in - 0.01, face_x - out * dish),
                                                 (rim_in, face_x - out * (dish + 0.01))], 64, "rim"))
    # hub, lug nuts and centre cap
    parts.append(lathe(name + "_hub", [(0.0, face_x + out * 0.006), (0.03, face_x + out * 0.006), (0.034, face_x + out * 0.002),
                                        (hub_r, face_x - out * 0.004), (hub_r, face_x - out * 0.04)], 32, "rim"))
    for k in range(D.get("lugs", 5)):
        a = 2 * math.pi * k / D.get("lugs", 5) + 0.3
        nut = lathe(name + "_lug", [(0.0, face_x + out * 0.012), (0.009, face_x + out * 0.011), (0.0105, face_x + out * 0.004), (0.0105, face_x - out * 0.01)], 6, "chrome")
        nut.location = (0, 0.058 * math.cos(a), 0.058 * math.sin(a))
        parts.append(nut)
    parts.append(lathe(name + "_cap", [(0.0, face_x + out * 0.015), (0.022, face_x + out * 0.014), (0.026, face_x + out * 0.008)], 24, "satin"))
    for p in parts:
        p.parent = tyre
    # upright: disc and caliper (do not spin)
    pivot = link(bpy.data.objects.new(name + "_upright", None)); pivot.location = loc
    disc_r = rim_r - 0.04
    disc = lathe(name + "_disc", [(0.07, -out * 0.012), (disc_r, -out * 0.012), (disc_r, out * 0.014), (0.07, out * 0.014),
                                   (0.075, out * 0.03), (0.0, out * 0.03)], 56, "disc")
    disc.parent = pivot
    cy = disc_r * 0.15 * (1 if loc[1] > 0 else -1)
    cal = box(name + "_caliper", (0.075, 0.15, 0.085), (loc[0] + out * 0.008, loc[1] - cy, loc[2] + disc_r * 0.66), "caliper", 0.022)
    cal.rotation_euler = (math.atan2(-cy, disc_r * 0.66) if loc[1] > 0 else -math.atan2(cy, disc_r * 0.66), 0, 0)
    cal.parent = pivot; cal.matrix_parent_inverse = pivot.matrix_world.inverted()
    return tyre
