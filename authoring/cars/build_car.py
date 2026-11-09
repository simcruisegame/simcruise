"""Build a SimCruise car model in Blender from a compact shape description.

    blender -b -P authoring/cars/build_car.py -- authoring/cars/kestrel_s.json game/art/cars/kestrel_s.glb [--render out.png]

The body is one lofted skin: cross-sections along the car (rear to front)
take their height from a side silhouette, their half-width from a plan
curve, and narrow above the beltline toward the roof (tumblehome). A
subdivision pass smooths it. Materials are then assigned by position on
that single skin -- glass between the pillars, lamps, grille, lower trim,
and thin dark shut lines for doors, hood and lid -- so detail costs almost
no geometry. Wheels (tyre with sidewall bulge, spoked rim, disc, caliper),
mirrors, handles, plates and a simple cabin are separate meshes.

Coordinates: metres, Blender +Y forward, +Z up, origin on the ground midway
between the axles, which is the physics body origin in Powertrain.gd. The
glTF exporter turns +Y forward into Godot's -Z forward.

Nodes in the .glb: body, cabin, mirror_l/r, wheel_fl/fr/rl/rr (origin at the
hub, spin about local X), plate_f/r. Materials named paint, glass, trim,
chrome, lamp_front, lamp_rear (brake lamps; the game drives its emission),
grille, rubber, rim, disc, caliper, seat, dash.

Everything here is original MIT work for SimCruise: no manufacturer shapes,
logos or badges are traced; proportions come from published dimensions.
"""
import bpy, bmesh, json, math, sys
from mathutils import Vector

argv = sys.argv[sys.argv.index("--") + 1:]
spec = json.load(open(argv[0]))
out_path = argv[1]
render_path = argv[argv.index("--render") + 1] if "--render" in argv else None


def lerp(a, b, t):
    return a + (b - a) * t


def curve(points, x):
    """Piecewise smooth (cosine) interpolation through [(x, value)] sorted by x."""
    if x <= points[0][0]:
        return points[0][1]
    for (x0, v0), (x1, v1) in zip(points, points[1:]):
        if x <= x1:
            t = (x - x0) / max(x1 - x0, 1e-9)
            t = (1 - math.cos(t * math.pi)) / 2
            return lerp(v0, v1, t)
    return points[-1][1]


# ---------------------------------------------------------------- scene
bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene

L = spec["length_m"]; W = spec["width_m"]; H = spec["height_m"]
wb = spec["wheelbase_m"]; rear_axle = spec["rear_overhang_m"]  # rear bumper to rear axle
Rw = spec["wheel_radius_m"]; track_f = spec["track_front_m"]; track_r = spec["track_rear_m"]
tire_w = spec["tire_width_m"]
y_rear = -wb / 2 - rear_axle          # rear bumper in model y
y_front = y_rear + L

def Y(frac):  # fraction of length from the rear bumper to model y
    return y_rear + frac * L

# Profiles are given as fractions of length; heights in metres.
top = [(p[0], p[1]) for p in spec["top"]]          # upper silhouette z
low = [(p[0], p[1]) for p in spec["bottom"]]       # lower edge z
half = [(p[0], p[1] * W / 2) for p in spec["plan"]]  # half-width at the beltline
belt = [(p[0], p[1]) for p in spec["belt"]]        # beltline z
cabin = spec["cabin"]   # [c_base, c_top, a_top, a_base] as fractions of length
tumble = spec.get("tumblehome", 0.14)              # roof half-width loss as a fraction
roof_round = spec.get("roof_round", 0.06)

# ---------------------------------------------------------------- materials
def material(name, color, rough=0.5, metal=0.0, clearcoat=0.0, emission=None, alpha=1.0, transmission=0.0):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    b = m.node_tree.nodes["Principled BSDF"]
    b.inputs["Base Color"].default_value = (*color, 1)
    b.inputs["Roughness"].default_value = rough
    b.inputs["Metallic"].default_value = metal
    if clearcoat:
        b.inputs["Coat Weight"].default_value = clearcoat
        b.inputs["Coat Roughness"].default_value = 0.04
    if emission:
        b.inputs["Emission Color"].default_value = (*emission, 1)
        b.inputs["Emission Strength"].default_value = 0.0
    if alpha < 1.0:
        b.inputs["Alpha"].default_value = alpha
        m.surface_render_method = "BLENDED"
    return m

def srgb(h):
    h = h.lstrip("#")
    c = [int(h[i:i + 2], 16) / 255 for i in (0, 2, 4)]
    return tuple(((v + 0.055) / 1.055) ** 2.4 if v > 0.04045 else v / 12.92 for v in c)

M = {
    "paint": material("paint", srgb(spec.get("paint", "#1f4e79")), 0.32, 0.35, clearcoat=1.0),
    "glass": material("glass", srgb("#0d1317"), 0.03, 0.0, alpha=0.82),
    "trim": material("trim", srgb("#151618"), 0.55),
    "chrome": material("chrome", srgb("#c8ccd0"), 0.12, 1.0),
    "lamp_front": material("lamp_front", srgb("#d9dde0"), 0.08, 0.2, emission=srgb("#fff6e0")),
    "lamp_rear": material("lamp_rear", srgb("#7a0c10"), 0.15, 0.0, emission=srgb("#ff2010")),
    "grille": material("grille", srgb("#0b0c0d"), 0.6),
    "rubber": material("rubber", srgb("#161616"), 0.85),
    "rim": material("rim", srgb(spec.get("rim", "#9aa0a6")), 0.25, 0.9),
    "disc": material("disc", srgb("#6d6f72"), 0.45, 0.8),
    "caliper": material("caliper", srgb(spec.get("caliper", "#2b2d30")), 0.4, 0.2),
    "seat": material("seat", srgb(spec.get("seat", "#1d1e20")), 0.8),
    "dash": material("dash", srgb("#202224"), 0.7),
    "plate": material("plate", srgb("#e8e6df"), 0.5),
    "lower": material("lower", srgb(spec.get("lower", "#151618")), 0.6),
}

# ---------------------------------------------------------------- body skin
N_STATIONS = 140
N_SIDE = 26          # points from the bottom centre to the roof centre on one side

def section(frac):
    """Right-hand half section, bottom centre to roof centre: list of (x, z)."""
    z0 = curve(low, frac); z1 = curve(belt, frac); z2 = curve(top, frac)
    w = curve(half, frac)
    in_cabin = cabin[0] < frac < cabin[3]
    pts = []
    # bottom: flat to a rounded lower corner
    r = min(0.09, (z1 - z0) * 0.35)
    pts.append((0.0, z0))
    pts.append((w * 0.55, z0))
    for k in range(1, 5):
        a = -math.pi / 2 + k * math.pi / 8
        pts.append((w - r + r * math.cos(a), z0 + r + r * math.sin(a)))
    # body side: tucked in at the sill, a character line 9 cm under the
    # beltline, widest just above it, then a tight shoulder
    crease = z1 - 0.09
    for k in range(1, 8):
        t = k / 7
        z = lerp(z0 + r, z1, t)
        tuck = 0.965 + 0.035 * min(1.0, t * 1.6)
        step = 0.006 if z > crease else 0.0
        pts.append((w * tuck + step, z))
    if z2 <= z1 + 0.02 or not in_cabin:
        # hood / deck: shoulder rounds into a nearly flat top at z2
        zt = max(z2, z1)
        s = min(0.06, max(zt - z1, 0.01) + 0.02)
        for k in range(1, 5):
            a = k * (math.pi / 2) / 4
            pts.append((w - s + s * math.cos(a), z1 + (zt - z1 + 0.0) * math.sin(a)))
        # gently crowned hood or deck
        for k in range(1, 5):
            t = k / 4
            pts.append((lerp(w - s, 0.0, t), zt + 0.025 * math.sin(t * math.pi / 2)))
    else:
        # greenhouse: taper from the beltline to the roof with tumblehome
        roof_w = w * (1 - tumble)
        for k in range(1, 7):
            t = k / 6
            pts.append((lerp(w * 0.985, roof_w, t), lerp(z1, z2 - roof_round, t)))
        for k in range(1, 4):
            a = k * (math.pi / 2) / 3
            pts.append((roof_w - roof_w * 0.35 * (1 - math.cos(a)), z2 - roof_round + roof_round * math.sin(a)))
        pts.append((0.0, z2))
    # resample to N_SIDE points by arc length so every section has the same count
    seg = [0.0]
    for a, b in zip(pts, pts[1:]):
        seg.append(seg[-1] + math.dist(a, b))
    out = []
    for i in range(N_SIDE):
        s = seg[-1] * i / (N_SIDE - 1)
        j = max(k for k in range(len(seg)) if seg[k] <= s + 1e-9)
        j = min(j, len(pts) - 2)
        t = (s - seg[j]) / max(seg[j + 1] - seg[j], 1e-9)
        out.append((lerp(pts[j][0], pts[j + 1][0], t), lerp(pts[j][1], pts[j + 1][1], t)))
    return out

bm = bmesh.new()
rows = []
for i in range(N_STATIONS):
    frac = i / (N_STATIONS - 1)
    # end caps pinch the plan width so the nose and tail close smoothly
    sec = section(frac)
    y = Y(frac)
    ring = []
    for side in (1, -1):
        seq = sec if side == 1 else list(reversed(sec))[1:-1]
        for (x, z) in seq:
            ring.append(bm.verts.new((x * side, y, z)))
    rows.append(ring)
for a, b in zip(rows, rows[1:]):
    n = len(a)
    for k in range(n):
        bm.faces.new((a[k], a[(k + 1) % n], b[(k + 1) % n], b[k]))
# close the ends
for ring, flip in ((rows[0], True), (rows[-1], False)):
    f = bm.faces.new(list(reversed(ring)) if flip else ring)
bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
mesh = bpy.data.meshes.new("body")
bm.to_mesh(mesh)
body = bpy.data.objects.new("body", mesh)
scene.collection.objects.link(body)
for m in M.values():
    body.data.materials.append(m)
mat_index = {name: i for i, name in enumerate(M)}

sub = body.modifiers.new("smooth", "SUBSURF")
sub.levels = 1; sub.render_levels = 1
sub.quality = 2
bpy.context.view_layer.objects.active = body
bpy.ops.object.modifier_apply(modifier="smooth")

# wheel arches: subtract a slightly flared cylinder at each wheel
def arch(y, x_side, r):
    bpy.ops.mesh.primitive_cylinder_add(vertices=48, radius=r, depth=0.9, location=(x_side, y, Rw - 0.01), rotation=(0, math.pi / 2, 0))
    c = bpy.context.active_object
    mod = body.modifiers.new("arch", "BOOLEAN"); mod.operation = "DIFFERENCE"; mod.object = c; mod.solver = "EXACT"
    bpy.context.view_layer.objects.active = body
    bpy.ops.object.modifier_apply(modifier="arch")
    bpy.data.objects.remove(c)
for (y, tr) in ((wb / 2, track_f), (-wb / 2, track_r)):
    for s in (1, -1):
        arch(y, s * (tr / 2 + 0.25), Rw + spec.get("arch_gap_m", 0.045))

# ---------------------------------------------------------------- material zones
def zone(face):
    c = face.calc_center_median()
    frac = (c.y - y_rear) / L
    x = abs(c.x); z = c.z
    f = min(max(frac, 0), 1)
    zb = curve(belt, f); zl = curve(low, f)
    n = face.normal
    # the greenhouse skin under the glass decals and the pillars: satin black
    if cabin[0] < frac < cabin[3] and z > zb + 0.01:
        # windscreen and rear-window slopes, and the side glass band under the roof edge
        if frac > cabin[2] - 0.003 or frac < cabin[1] + 0.003:
            return spec.get("pillar_material", "trim")
        if abs(n.x) > 0.3 and z < curve(top, f) - spec.get("roof_band_m", 0.05):
            return spec.get("pillar_material", "trim")
    # sills and bumper bottoms between and beyond the wheel arches
    near_wheel = any(abs(c.y - yw) < Rw + 0.12 for yw in (wb / 2, -wb / 2))
    if "lower_band_m" in spec and z < zl + spec["lower_band_m"] and not near_wheel:
        return "lower"
    for cut in spec.get("shut_lines", []):
        if abs(frac - cut[0]) < 0.0018 and cut[1] <= z <= cut[2] and abs(n.x) > cut[3]:
            return "trim"
    for cut in spec.get("shut_lines_top", []):
        if abs(frac - cut[0]) < 0.0018 and z > zb - 0.02 and abs(n.z) > 0.5:
            return "trim"
    return "paint"

bm = bmesh.new(); bm.from_mesh(body.data)
for f in bm.faces:
    f.material_index = mat_index[zone(f)]
    f.smooth = True
bm.to_mesh(body.data); bm.free()
body.data.shade_smooth()


# ---------------------------------------------------------------- decals
# A decal is a smooth outline drawn in a projection plane, filled with a fine
# grid and shrink-wrapped onto the body along the plane's normal, 3 mm proud.
# Glass, lamps and grilles get clean edges without cutting the skin.
def inside(poly, p):
    hit = False
    for (ax, ay), (bx, by) in zip(poly, poly[1:] + poly[:1]):
        if (ay > p[1]) != (by > p[1]) and p[0] < (bx - ax) * (p[1] - ay) / (by - ay) + ax:
            hit = not hit
    return hit

def round_corners(poly, radius=0.05, steps=5):
    """Fixed-radius rounded corners (metres), like pressed glass and lenses."""
    out = []
    n = len(poly)
    for i in range(n):
        p0, p1, p2 = Vector(poly[i - 1]), Vector(poly[i]), Vector(poly[(i + 1) % n])
        a, b = (p0 - p1), (p2 - p1)
        r = min(radius, a.length / 3, b.length / 3)
        if r < 1e-4:
            out.append(tuple(p1)); continue
        s0 = p1 + a.normalized() * r; s1 = p1 + b.normalized() * r
        for k in range(steps + 1):
            t = k / steps
            q = s0 * (1 - t) ** 2 + p1 * 2 * (1 - t) * t + s1 * t * t   # quadratic Bezier
            out.append((q.x, q.y))
    return out

def decal(name, plane, poly, mat, step=0.012, offset=0.003, mirror=True, radius=0.05):
    """plane: 'side' (frac, z) seen from +X; 'front'/'rear' (x, z); 'top' (frac, x)."""
    # round in metres: side and top planes give u as a fraction of length
    scale_u = L if plane in ("side", "top") else 1.0
    poly = [(u * scale_u, v) for (u, v) in poly]
    poly = round_corners(poly, radius)
    poly = [(u / scale_u, v) for (u, v) in poly]
    us = [p[0] for p in poly]; vs = [p[1] for p in poly]
    bm = bmesh.new()
    def to3(u, v, side):
        if plane == "side":
            return (side * (W / 2 + 0.6), Y(u), v)
        if plane == "front":
            return (u * side, y_front + 0.6, v)
        if plane == "rear":
            return (u * side, y_rear - 0.6, v)
        return (v * side, Y(u), H + 0.6)        # top: v is x
    du = step / L if plane in ("side", "top") else step
    dv = step
    sides = (1, -1) if mirror else (1,)
    for side in sides:
        grid = {}
        u0, u1, v0, v1 = min(us), max(us), min(vs), max(vs)
        nu = max(2, int((u1 - u0) / du) + 2); nv = max(2, int((v1 - v0) / dv) + 2)
        for i in range(nu):
            for j in range(nv):
                u = u0 + (u1 - u0) * i / (nu - 1); v = v0 + (v1 - v0) * j / (nv - 1)
                if inside(poly, (u, v)):
                    grid[(i, j)] = bm.verts.new(to3(u, v, side))
        for (i, j), va in grid.items():
            q = [va, grid.get((i + 1, j)), grid.get((i + 1, j + 1)), grid.get((i, j + 1))]
            if all(q):
                bm.faces.new(q if (side == 1) == (plane != "rear") else list(reversed(q)))
    me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free()
    o = bpy.data.objects.new(name, me); scene.collection.objects.link(o)
    o.data.materials.append(M[mat])
    sw = o.modifiers.new("wrap", "SHRINKWRAP"); sw.target = body; sw.wrap_method = "PROJECT"
    sw.use_project_x = plane == "side"; sw.use_project_y = plane in ("front", "rear"); sw.use_project_z = plane == "top"
    sw.use_negative_direction = True; sw.use_positive_direction = True; sw.offset = offset
    bpy.context.view_layer.objects.active = o
    bpy.ops.object.modifier_apply(modifier="wrap")
    for p in o.data.polygons:
        p.use_smooth = True
    return o

for d in spec.get("decals", []):
    decal(d["name"], d["plane"], [tuple(p) for p in d["outline"]], d["material"], d.get("step", 0.012), d.get("offset", 0.003), d.get("mirror", True), d.get("radius", 0.05))

# Cut the skin away under every glass panel (2 cm inside its outline, so the
# glass still covers the edge): the cabin sees out and the outside sees in.
def plane_coords(plane, c):
    if plane == "side":
        return ((c.y - y_rear), c.z)
    if plane == "top":
        return ((c.y - y_rear), c.x)
    return (c.x, c.z)

def inset(poly, d):
    cx = sum(p[0] for p in poly) / len(poly); cy = sum(p[1] for p in poly) / len(poly)
    out = []
    for (x, y) in poly:
        v = Vector((x - cx, y - cy)); l = v.length
        out.append((x - v.x / max(l, 1e-6) * d, y - v.y / max(l, 1e-6) * d) if l > d else (x, y))
    return out

holes = []
for d in spec.get("decals", []):
    if d["material"] != "glass":
        continue
    scale_u = L if d["plane"] in ("side", "top") else 1.0
    poly = [(u * scale_u, v) for (u, v) in d["outline"]]
    poly = inset(round_corners(poly, d.get("radius", 0.05)), 0.02)
    sides = (1, -1) if d.get("mirror", True) else (1,)
    for side in sides:
        holes.append((d["plane"], side, poly))
bm = bmesh.new(); bm.from_mesh(body.data)
dead = []
for f in bm.faces:
    c = f.calc_center_median(); n = f.normal
    for (plane, side, poly) in holes:
        facing = {"side": n.x * side > 0.3, "top": n.z > 0.2, "rear": n.y < -0.3, "front": n.y > 0.3}[plane]
        if not facing:
            continue
        u, v = plane_coords(plane, c)
        if plane in ("side",) and c.x * side <= 0:
            continue
        if plane in ("front", "rear"):
            u = u * side
        if inside(poly, (u, v)):
            dead.append(f); break
bmesh.ops.delete(bm, geom=dead, context="FACES")
bm.to_mesh(body.data); bm.free()
print("glass openings", len(dead), "faces")

# ---------------------------------------------------------------- parts
def box(name, size, loc, mat, bevel=0.01, parent=None):
    bpy.ops.mesh.primitive_cube_add(size=1, location=loc)
    o = bpy.context.active_object; o.name = name; o.scale = size
    bpy.ops.object.transform_apply(scale=True)
    if bevel:
        b = o.modifiers.new("bevel", "BEVEL"); b.width = bevel; b.segments = 3
        bpy.ops.object.modifier_apply(modifier="bevel")
    o.data.materials.append(M[mat])
    bpy.ops.object.shade_smooth()
    if parent:
        o.parent = parent
    return o

# mirrors on stalks at the A-pillar base
a_base = Y(cabin[3] - 0.035); zb_a = curve(belt, cabin[3] - 0.035)
for s, nm in ((1, "mirror_r"), (-1, "mirror_l")):
    w_here = curve(half, cabin[3] - 0.035)
    m = box(nm, (0.06, 0.16, 0.10), (s * (w_here + 0.09), a_base - 0.02, zb_a + 0.08), "paint", 0.025)
    glass = box(nm + "_glass", (0.005, 0.13, 0.08), (s * (w_here + 0.09 + 0.032), a_base - 0.06, zb_a + 0.08), "chrome", 0.0, parent=None)
    glass.parent = m; glass.matrix_parent_inverse = m.matrix_world.inverted()
    box(nm + "_stalk", (0.08, 0.04, 0.025), (s * (w_here + 0.04), a_base, zb_a + 0.05), "trim", 0.008)

# extra parts: [name, size xyz, at (fraction of length, x, z), material, mirror]
for prt in spec.get("parts", []):
    sx, sy, sz = prt["size"]; fr, px, pz = prt["at"]
    for side in ((1, -1) if prt.get("mirror") else (1,)):
        o = box(prt["name"], (sx, sy, sz), (side * px, Y(fr), pz), prt["material"], prt.get("bevel", 0.01))
        if prt.get("tilt"):
            o.rotation_euler = (math.radians(prt["tilt"]), 0, 0)

# door handles at the shut-line stations
for h in spec.get("handles", []):
    fr = h[0]; z = h[1]
    for s in (1, -1):
        w_here = curve(half, fr)
        box("handle", (0.012, 0.13, 0.025), (s * (w_here * 1.02 + 0.004), Y(fr), z), "trim" if spec.get("dark_handles") else "chrome", 0.008)

# plates
for nm, fr, z in (("plate_r", 0.0, spec.get("plate_rear_z", 0.6)), ("plate_f", 1.0, spec.get("plate_front_z", 0.35))):
    y = Y(fr) + (0.004 if fr == 0 else -0.004)
    box(nm, (0.305, 0.008, 0.152), (0, y - (0.03 if fr == 0 else -0.03) * 0, z), "plate", 0.004)

# simple cabin: floor, two seats per row, dash, wheel
cab = []
seat_rows = spec.get("seat_rows", [0.56])
for fr in seat_rows:
    for s in (1, -1):
        x = s * W * 0.2
        y = Y(fr)
        cab.append(box("seat_base", (0.5, 0.52, 0.12), (x, y, spec["seat_z"]), "seat", 0.04))
        back = box("seat_back", (0.5, 0.1, 0.62), (x, y - 0.27, spec["seat_z"] + 0.34), "seat", 0.05)
        back.rotation_euler = (math.radians(-12), 0, 0)
        cab.append(back)
dash_y = Y(cabin[3] - 0.02)
cab.append(box("dash", (W * 0.82, 0.38, 0.22), (0, dash_y - 0.15, curve(belt, cabin[3] - 0.02) - 0.08), "dash", 0.05))
bpy.ops.mesh.primitive_torus_add(major_radius=0.18, minor_radius=0.018, location=(-W * 0.2, dash_y - 0.38, curve(belt, cabin[3] - 0.02) + 0.02),
                                 rotation=(math.radians(-70), 0, 0))
sw = bpy.context.active_object; sw.data.materials.append(M["trim"]); cab.append(sw)

# ---------------------------------------------------------------- wheels
def lathe(name, profile, segments, mat):
    """Revolve [(radius, x)] around the local X axis."""
    bm = bmesh.new()
    rings = []
    for (r, x) in profile:
        ring = []
        for k in range(segments):
            a = 2 * math.pi * k / segments
            ring.append(bm.verts.new((x, r * math.cos(a), r * math.sin(a))))
        rings.append(ring)
    for a, b in zip(rings, rings[1:]):
        for k in range(segments):
            bm.faces.new((a[k], a[(k + 1) % segments], b[(k + 1) % segments], b[k]))
    me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free()
    o = bpy.data.objects.new(name, me); scene.collection.objects.link(o)
    o.data.materials.append(M[mat])
    for p in o.data.polygons:
        p.use_smooth = True
    return o

def wheel(name, loc, outward):
    rim_r = spec["rim_in"] * 0.0254 / 2
    hw = tire_w / 2
    tyre = lathe(name, [
        (rim_r, -hw * 0.92), (rim_r + 0.02, -hw), (Rw - 0.035, -hw * 1.02), (Rw - 0.008, -hw * 0.82),
        (Rw, -hw * 0.5), (Rw, hw * 0.5), (Rw - 0.008, hw * 0.82), (Rw - 0.035, hw * 1.02), (rim_r + 0.02, hw), (rim_r, hw * 0.92)], 48, "rubber")
    tyre.location = loc
    # rim barrel and face with spokes
    barrel = lathe(name + "_barrel", [(rim_r - 0.012, -hw * 0.9), (rim_r - 0.012, hw * 0.9)], 40, "rim")
    face_x = outward * hw * 0.62
    spokes = spec.get("spokes", 5)
    bm = bmesh.new()
    hub_r = 0.075
    for k in range(spokes):
        a = 2 * math.pi * k / spokes
        for da in (-0.13, 0.13):
            pass
        # each spoke: a tapered box from the hub to the barrel, slightly dished
        v = []
        for (r, x, ww) in ((hub_r, face_x - outward * 0.01, 0.05), (rim_r - 0.02, face_x + outward * 0.012, 0.075)):
            for side in (-1, 1):
                for depth in (0, 1):
                    aa = a + side * ww / max(r, 0.01) * 0.5
                    xx = x - outward * depth * 0.035
                    v.append(bm.verts.new((xx, r * math.cos(aa), r * math.sin(aa))))
        idx = [(0, 2, 6, 4), (1, 5, 7, 3), (0, 4, 5, 1), (2, 3, 7, 6), (0, 1, 3, 2), (4, 6, 7, 5)]
        for q in idx:
            try:
                bm.faces.new([v[i] for i in q])
            except ValueError:
                pass
    me = bpy.data.meshes.new(name + "_spokes"); bm.to_mesh(me); bm.free()
    sp = bpy.data.objects.new(name + "_spokes", me); scene.collection.objects.link(sp); sp.data.materials.append(M["rim"])
    hub = lathe(name + "_hub", [(0.0, face_x + outward * 0.012), (hub_r, face_x + outward * 0.006), (hub_r, face_x - outward * 0.03)], 24, "rim")
    disc_r = rim_r - 0.035
    disc = lathe(name + "_disc", [(0.06, -outward * 0.01), (disc_r, -outward * 0.01), (disc_r, outward * 0.018), (0.06, outward * 0.018)], 40, "disc")
    for part in (barrel, sp, hub):
        part.parent = tyre
    # disc and caliper do not spin: parented to an empty at the hub
    pivot = bpy.data.objects.new(name + "_upright", None); scene.collection.objects.link(pivot); pivot.location = loc
    disc.parent = pivot
    cal = box(name + "_caliper", (0.07, 0.12, 0.09), (loc[0] - outward * 0.0, loc[1] - 0.0, loc[2] + disc_r * 0.62), "caliper", 0.015)
    cal.parent = pivot; cal.matrix_parent_inverse = pivot.matrix_world.inverted()
    return tyre

for nm, y, tr in (("wheel_fl", wb / 2, track_f), ("wheel_fr", wb / 2, track_f), ("wheel_rl", -wb / 2, track_r), ("wheel_rr", -wb / 2, track_r)):
    s = 1 if nm.endswith("r") else -1
    wheel(nm, (s * tr / 2, y, Rw), s)

# ---------------------------------------------------------------- export
if render_path:
    bpy.ops.object.light_add(type="SUN", location=(4, -4, 8)); sun = bpy.context.active_object; sun.data.energy = 4; sun.rotation_euler = (math.radians(40), 0, math.radians(35))
    world = bpy.data.worlds.new("w"); scene.world = world; world.use_nodes = True
    world.node_tree.nodes["Background"].inputs[0].default_value = (0.55, 0.62, 0.7, 1); world.node_tree.nodes["Background"].inputs[1].default_value = 0.9
    bpy.ops.mesh.primitive_plane_add(size=40, location=(0, 0, 0)); ground = bpy.context.active_object
    gm = material("ground", srgb("#7d7b74"), 0.9); ground.data.materials.append(gm)
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam")); scene.collection.objects.link(cam); scene.camera = cam
    view = argv[argv.index("--view") + 1] if "--view" in argv else "three-quarter"
    cam.location = {"three-quarter": (L * 1.25, L * 1.05, H * 1.3), "side": (L * 1.6, 0.0, H * 0.6),
                    "rear": (-L * 0.9, -L * 1.1, H * 1.4)}[view]
    cam.data.lens = 40
    direction = Vector((0, 0, H * 0.45)) - cam.location; cam.rotation_euler = direction.to_track_quat("-Z", "Y").to_euler()
    scene.render.engine = "CYCLES"; scene.cycles.device = "CPU"; scene.cycles.samples = 24; scene.cycles.use_denoising = True; scene.render.resolution_x = 960; scene.render.resolution_y = 540
    scene.render.filepath = render_path
    bpy.ops.render.render(write_still=True)
    bpy.data.objects.remove(ground); bpy.data.objects.remove(cam); bpy.data.objects.remove(sun)

bpy.ops.export_scene.gltf(filepath=out_path, export_format="GLB", export_apply=True, export_yup=True)
tri = sum(len(o.data.polygons) for o in scene.objects if o.type == "MESH")
print("BUILT", out_path, "faces", tri)
