"""Cabin for build_car.py, packaged around the driver's eye point.

Typical packaging distances (SAE J1100 style, rounded): the steering wheel
hub ~0.50 m ahead of and ~0.33 m below the eye, the seat H-point ~0.15 m
behind and ~0.70 m below it, the instrument panel face ~0.72 m ahead. The
design can override any of these under DESIGN["cabin"].

Parts: dashboard (lofted profile across the car) with a cluster hood, a
digital cluster and a centre screen (painted textures), a vent band, centre
console with shifter, front sport seats and a rear bench, rear-view mirror,
and the steering wheel as its own node "steering_wheel" whose local Y axis
(Godot local -Z) is the column; the game turns it about that axis.
"""
import math
import bmesh
import bpy
import numpy as np
from mathutils import Vector, Matrix, Euler

import paint


def loft_x(name, profile, x0, x1, mat, MATS, link, round_ends=0.03, n=24):
    """Extrude a side-view profile [(y, z)] across x0..x1 with rounded ends."""
    bm = bmesh.new()
    rings = []
    xs = [x0 + (x1 - x0) * k / n for k in range(n + 1)]
    for x in xs:
        d = min(x - x0, x1 - x)
        shrink = 1.0 if d >= round_ends else math.sqrt(max(0.0, 1 - ((round_ends - d) / round_ends) ** 2))
        cy = sum(p[0] for p in profile) / len(profile); cz = sum(p[1] for p in profile) / len(profile)
        rings.append([bm.verts.new((x, cy + (y - cy) * (0.6 + 0.4 * shrink), cz + (z - cz) * (0.6 + 0.4 * shrink))) for (y, z) in profile])
    m = len(profile)
    for a, b in zip(rings, rings[1:]):
        for i in range(m - 1):
            bm.faces.new((a[i], b[i], b[i + 1], a[i + 1]))
    bm.faces.new(list(reversed(rings[0]))); bm.faces.new(rings[-1])
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free()
    o = link(bpy.data.objects.new(name, me)); o.data.materials.append(MATS[mat])
    for p in o.data.polygons:
        p.use_smooth = True
    return o


def textured(name, MATS, img_arr, emit_arr=None, rough=0.2, emission=1.0):
    m = bpy.data.materials.new(name); m.use_nodes = True
    nt = m.node_tree; b = nt.nodes["Principled BSDF"]
    def image(n, arr):
        h, w = arr.shape[:2]
        im = bpy.data.images.new(n, w, h, alpha=True)
        im.pixels.foreach_set(np.ascontiguousarray(arr[::-1].astype(np.float32)).ravel())
        im.file_format = "PNG"; im.pack()
        return im
    t = nt.nodes.new("ShaderNodeTexImage"); t.image = image(name + "_albedo", img_arr)
    nt.links.new(t.outputs["Color"], b.inputs["Base Color"])
    b.inputs["Roughness"].default_value = rough
    if emit_arr is not None:
        e = nt.nodes.new("ShaderNodeTexImage"); e.image = image(name + "_emission", emit_arr)
        nt.links.new(e.outputs["Color"], b.inputs["Emission Color"])
        b.inputs["Emission Strength"].default_value = emission
    MATS[name] = m
    return m


def plane(name, corners, mat, link, uv=True):
    me = bpy.data.meshes.new(name)
    me.from_pydata([tuple(c) for c in corners], [], [(0, 1, 2, 3)])
    if uv:
        u = me.uv_layers.new(name="UVMap")
        for li, co in zip(range(4), [(0, 0), (1, 0), (1, 1), (0, 1)]):
            u.data[li].uv = co
    me.update()
    o = link(bpy.data.objects.new(name, me)); o.data.materials.append(mat)
    return o


def cluster_texture(style):
    """Digital instrument cluster: two dials with tick marks or a bar tach."""
    c = paint.Canvas(512, 192, "#05070a"); e = paint.Canvas(512, 192, "#000000")
    accent = style.get("accent", "#e23b2b")
    for (cx, label) in ((0.27, "rpm"), (0.73, "mph")):
        for k in range(41):
            a = math.radians(225 - k * 270 / 40)
            r0, r1 = (0.30, 0.37) if k % 5 == 0 else (0.33, 0.37)
            pts = [(cx + r0 * math.cos(a) * 192 / 512, 0.5 + r0 * math.sin(a)), (cx + r1 * math.cos(a) * 192 / 512, 0.5 + r1 * math.sin(a))]
            col = accent if (label == "rpm" and k > 32) else "#cfd6dc"
            c.line(pts, 0.006 if k % 5 == 0 else 0.003, col); e.line(pts, 0.006 if k % 5 == 0 else 0.003, col)
        a = math.radians(225 - 0.18 * 270)
        nd = [(cx, 0.5), (cx + 0.31 * math.cos(a) * 192 / 512, 0.5 + 0.31 * math.sin(a))]
        c.line(nd, 0.008, accent); e.line(nd, 0.008, accent)
        c.ellipse(cx, 0.5, 0.03, 0.08, "#1b1f24")
    c.rect(0.44, 0.3, 0.56, 0.7, "#0d1218", 1.0, r=0.01)
    for k in range(4):
        c.rect(0.46, 0.36 + k * 0.08, 0.54, 0.40 + k * 0.08, "#4fa3d9", 0.8); e.rect(0.46, 0.36 + k * 0.08, 0.54, 0.40 + k * 0.08, "#4fa3d9", 0.8)
    return c.result(), e.result()


def screen_texture():
    """Centre screen: a map-like home screen."""
    c = paint.Canvas(512, 288, "#0b1016"); e = paint.Canvas(512, 288, "#000000")
    c.rect(0.0, 0.0, 0.62, 1.0, "#1d2a24", 1.0)
    for k in range(9):
        c.line([(0.0, 0.1 + k * 0.11), (0.62, 0.05 + k * 0.12)], 0.006, "#3a4a42")
    c.line([(0.05, 0.1), (0.25, 0.45), (0.38, 0.5), (0.58, 0.95)], 0.012, "#6fb0e8")
    c.ellipse(0.38, 0.5, 0.015, 0.027, "#e8eef3")
    for k in range(3):
        c.rect(0.66, 0.7 - k * 0.28, 0.97, 0.92 - k * 0.28, "#18222c", 1.0, r=0.01)
        c.rect(0.69, 0.84 - k * 0.28, 0.9, 0.87 - k * 0.28, "#9fb3c4", 1.0)
    e.img[:] = c.img * 0.8
    return c.result(), e.result()


def vent_texture():
    c = paint.Canvas(512, 64, "#121315")
    c.pattern("honeycomb", 60, "#2a2c30", line=0.14)
    return c.result()


def build(D, MATS, material, link, parts):
    cab = D.get("cabin", {})
    eye = Vector(D.get("eye", (-0.36, 0.02, 1.14)))
    W = D["width"]
    material("dash", cab.get("dash", "#17181a"), 0.7)
    material("dash_top", cab.get("dash_top", "#141516"), 0.85)
    material("seat", cab.get("seat", "#1b1c1f"), 0.85)
    material("seat_accent", cab.get("seat_accent", "#1b1c1f"), 0.8)
    material("carpet", "#121212", 0.95)
    material("headliner", cab.get("headliner", "#6f6e6a"), 0.9)
    material("trim_in", cab.get("trim_in", "#1c1d1f"), 0.75)
    material("alu", "#9ea3a8", 0.3, 0.9)
    side = 1 if eye.x > 0 else -1           # driver side sign
    hub = eye + Vector((0, cab.get("hub_ahead", 0.50), -cab.get("hub_below", 0.33)))
    dash_y = eye.y + cab.get("dash_ahead", 0.72)
    dash_top = cab.get("dash_top_z", eye.z - 0.19)
    cowl_y, cowl_z = cab.get("cowl", (dash_y + 0.42, dash_top + 0.02))
    floor_z = cab.get("floor_z", 0.25)
    # dashboard: windscreen base -> top -> face -> knee panel -> footwell
    prof = [(cowl_y, cowl_z), (dash_y + 0.25, dash_top + 0.015), (dash_y + 0.08, dash_top), (dash_y, dash_top - 0.06),
            (dash_y - 0.02, dash_top - 0.2), (dash_y + 0.02, dash_top - 0.33), (dash_y + 0.12, dash_top - 0.42),
            (dash_y + 0.35, floor_z + 0.12), (cowl_y, floor_z + 0.12)]
    half = W / 2 - cab.get("dash_inset", 0.06)
    loft_x("dash", prof, -half, half, "dash", MATS, link, 0.04)
    # vent band across the dash face (honeycomb mesh, or a slim slot with a trim strip)
    vz = dash_top - cab.get("vent_drop", 0.1)
    if cab.get("vents", "band") == "band":
        vt = textured("vents", MATS, vent_texture(), rough=0.6)
        plane("vent_band", [(-half + 0.05, dash_y - 0.012, vz - 0.03), (half - 0.05, dash_y - 0.012, vz - 0.03),
                            (half - 0.05, dash_y - 0.003, vz + 0.03), (-half + 0.05, dash_y - 0.003, vz + 0.03)], vt, link)
    else:
        material("dash_strip", cab.get("strip", "#5b4a3c"), 0.6)
        plane("dash_strip", [(-half + 0.05, dash_y - 0.013, vz - 0.025), (half - 0.05, dash_y - 0.013, vz - 0.025),
                             (half - 0.05, dash_y - 0.004, vz + 0.025), (-half + 0.05, dash_y - 0.004, vz + 0.025)], MATS["dash_strip"], link, uv=False)
        plane("vent_slot", [(-half + 0.05, dash_y - 0.008, vz + 0.028), (half - 0.05, dash_y - 0.008, vz + 0.028),
                            (half - 0.05, dash_y + 0.002, vz + 0.04), (-half + 0.05, dash_y + 0.002, vz + 0.04)], MATS["gap"], link, uv=False)
    # cluster hood and digital cluster behind the wheel
    cx = eye.x
    cz = dash_top + cab.get("cluster_up", 0.0)
    if cab.get("cluster", True):
        parts.rounded("cluster_hood", (0.175, 0.07, 0.012), Vector((cx, dash_y + 0.035, cz + 0.05)), "dash_top", 1, round_frac=0.8)
        ct, ce = cluster_texture(cab)
        cm = textured("cluster", MATS, ct, ce, rough=0.15, emission=1.2)
        plane("cluster", [(cx - 0.15, dash_y + 0.03, cz - 0.07), (cx + 0.15, dash_y + 0.03, cz - 0.07),
                          (cx + 0.15, dash_y + 0.05, cz + 0.035), (cx - 0.15, dash_y + 0.05, cz + 0.035)], cm, link)
    # centre screen (floating tablet or set into the dash)
    st, se = screen_texture()
    sm = textured("screen", MATS, st, se, rough=0.08, emission=1.0)
    sw, sh = cab.get("screen", (0.23, 0.13))
    sy = dash_y + cab.get("screen_ahead", 0.03); sz = dash_top + cab.get("screen_up", 0.06)
    sx = cab.get("screen_x", 0.0)
    parts.rounded("screen_bezel", (sw / 2 + 0.008, 0.01, sh / 2 + 0.008), Vector((sx, sy + 0.011, sz)), "trim", 1, round_frac=0.3)
    plane("screen", [(sx - sw / 2, sy - 0.0012, sz - sh / 2), (sx + sw / 2, sy - 0.0012, sz - sh / 2),
                     (sx + sw / 2, sy - 0.0012, sz + sh / 2), (sx - sw / 2, sy - 0.0012, sz + sh / 2)], sm, link)
    # centre console with shifter and armrest
    parts.rounded("console", (0.11, 0.38, 0.11), Vector((0, dash_y - 0.33, floor_z + 0.13)), "dash", 1)
    parts.rounded("console_front", (0.1, 0.1, 0.16), Vector((0, dash_y - 0.02, floor_z + 0.2)), "dash", 1)
    parts.rounded("armrest", (0.09, 0.16, 0.035), Vector((0, eye.y - 0.32, floor_z + 0.33)), "seat", 1)
    if cab.get("shifter", "manual") == "manual":
        parts.cylinder("shift_lever", 0.008, 0.16, (0, dash_y - 0.22, floor_z + 0.31), "alu", rot=(math.radians(-15), 0, 0))
        parts.rounded("shift_knob", (0.022, 0.022, 0.022), Vector((0, dash_y - 0.24, floor_z + 0.4)), "alu", 2, round_frac=0.9)
    else:
        parts.rounded("shift_knob", (0.025, 0.04, 0.03), Vector((0, dash_y - 0.24, floor_z + 0.28)), "trim", 1)
    # floor
    plane("floor", [(-half, cowl_y, floor_z), (half, cowl_y, floor_z), (half, cab.get("rear_floor_y", -1.25), floor_z),
                    (-half, cab.get("rear_floor_y", -1.25), floor_z)], MATS["carpet"], link, uv=False)
    # seats
    hp = Vector((eye.x, eye.y + cab.get("hip_ahead", 0.12), eye.z - cab.get("hip_below", 0.70)))
    for sx in (1, -1):
        seat(parts, Vector((sx * abs(eye.x), hp.y, hp.z)), cab)
    if cab.get("rear_seat", True):
        ry = cab.get("rear_seat_y", eye.y - 0.85)
        parts.rounded("rear_cushion", (half - 0.12, 0.25, 0.07), Vector((0, ry + 0.05, hp.z - 0.02)), "seat", 1)
        rb = parts.rounded("rear_back", (half - 0.12, 0.08, 0.30), Vector((0, ry - 0.22, hp.z + 0.3)), "seat", 1)
        rb.rotation_euler = (math.radians(-22), 0, 0)
        shelf_z = cab.get("shelf_z", 0.98)
        plane("parcel_shelf", [(-half, ry - 0.3, shelf_z), (half, ry - 0.3, shelf_z), (half, ry - 0.75, shelf_z), (-half, ry - 0.75, shelf_z)],
              MATS["trim_in"], link, uv=False)
    # rear-view mirror at the top of the windscreen
    rv = cab.get("mirror", (0.0, eye.y + 0.55, eye.z + 0.13))
    parts.rounded("rv_mirror", (0.12, 0.02, 0.035), Vector(rv), "trim", 1)
    parts.rounded("rv_glass", (0.112, 0.002, 0.03), Vector((rv[0], rv[1] - 0.021, rv[2])), "chrome", 1)
    steering_wheel(parts, hub, eye, cab, link, MATS)


def seat(parts, hp, cab):
    """Sport seat around an H-point: cushion, bolsters, reclined back, headrest."""
    acc = "seat_accent"
    parts.rounded("seat_cushion", (0.24, 0.26, 0.05), hp + Vector((0, 0.12, -0.06)), "seat", 1)
    for b in (-1, 1):
        parts.rounded("seat_bolster", (0.035, 0.24, 0.05), hp + Vector((b * 0.22, 0.12, -0.03)), acc, 1)
    back = parts.rounded("seat_back", (0.23, 0.06, 0.32), hp + Vector((0, -0.1, 0.3)), "seat", 1)
    back.rotation_euler = (math.radians(-17), 0, 0)
    for b in (-1, 1):
        bb = parts.rounded("seat_side", (0.04, 0.07, 0.24), hp + Vector((b * 0.22, -0.07, 0.24)), acc, 1)
        bb.rotation_euler = (math.radians(-17), 0, 0)
    hr = parts.rounded("headrest", (0.13, 0.05, 0.09), hp + Vector((0, -0.25, 0.71)), "seat", 1)
    hr.rotation_euler = (math.radians(-10), 0, 0)


def steering_wheel(parts, hub, eye, cab, link, MATS):
    """Three-spoke wheel built in a local frame (rim in XZ, column along +Y),
    then tilted so the rim faces the driver; origin at the hub."""
    R = cab.get("wheel_r", 0.185)
    bm = bmesh.new()
    # rim: torus with a slightly flattened bottom
    segs, tube = 64, 12
    rings = []
    for i in range(segs):
        a = 2 * math.pi * i / segs
        rr = R * (0.94 if math.sin(a) < -0.85 and cab.get("flat_bottom", True) else 1.0)
        c = Vector((rr * math.cos(a), 0.0, rr * math.sin(a)))
        radial = Vector((math.cos(a), 0, math.sin(a)))
        ring = []
        for j in range(tube):
            b = 2 * math.pi * j / tube
            off = radial * (0.016 * math.cos(b)) + Vector((0, 1, 0)) * (0.019 * math.sin(b))
            ring.append(bm.verts.new(c + off))
        rings.append(ring)
    for i in range(segs):
        a, b = rings[i], rings[(i + 1) % segs]
        for j in range(tube):
            bm.faces.new((a[j], b[j], b[(j + 1) % tube], a[(j + 1) % tube]))
    me = bpy.data.meshes.new("steering_wheel"); bm.to_mesh(me); bm.free()
    sw = link(bpy.data.objects.new("steering_wheel", me))
    sw.data.materials.append(MATS["trim_in"])
    for p in sw.data.polygons:
        p.use_smooth = True
    # spokes and hub pad as children (same local frame)
    pieces = []
    for ang in cab.get("spoke_angles", (0, 180, 270)):
        a = math.radians(ang)
        d = Vector((math.cos(a), 0, math.sin(a)))
        sp = parts.rounded("spoke", (R * 0.42, 0.012, 0.022), d * R * 0.5 + Vector((0, 0.02, 0)), "trim_in", 1)
        sp.rotation_euler = (0, -a, 0)
        pieces.append(sp)
    pad = parts.rounded("wheel_pad", (0.075, 0.03, 0.06), Vector((0, 0.025, 0)), "dash", 1)
    pieces.append(pad)
    for p in pieces:
        p.parent = sw
    tilt = math.radians(cab.get("column_deg", 24))   # column leans back from vertical
    sw.rotation_euler = (-tilt, 0, 0)
    sw.location = hub
    # column shroud (does not turn)
    col = parts.rounded("column", (0.04, 0.12, 0.04), hub + Vector((0, 0.16, -0.05)), "dash", 1)
    col.rotation_euler = (-tilt, 0, 0)
    return sw
