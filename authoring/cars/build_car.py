"""Build a SimCruise car model in Blender from a design file (v3).

    blender -b -P authoring/cars/build_car.py -- authoring/cars/designs/kestrel_s.py game/art/cars/kestrel_s.glb
        [--render out.png --view three-quarter|side|front|rear|top|cabin|rear-three-quarter]
        [--camera cam.json --render out.png]      (match a reference photograph)
        [--fast]                                  (Workbench render, shape checks)

The body is one smooth skin interpolated through the design's control net
(surface.py): design lines run the length of the car (keel, rocker, sill,
shoulder, belt, glass, roof rail, crown) and rows tie them together, leaning
where a pillar or the windscreen needs an edge. Paint, glass, black trim and
the underbody are regions of that skin in net coordinates, so every window
and pillar edge is an exact grid line; glass is recessed with a dark wall.
Wheel arches are cut afterwards with the original shading kept. Lamps,
grilles, vents, mirrors, handles, wheels and the cabin are separate parts
(features.py, cabin.py, wheels.py).

Coordinates: metres, Blender +Y forward, +Z up, origin on the ground midway
between the axles (the physics body origin). The glTF exporter turns +Y
forward into Godot's -Z forward.

Nodes: body, wheel_fl/fr/rl/rr (origin at the hub; spins), wheel_*_upright
(disc and caliper; steers only), steering_wheel (origin at the hub, rotates
about its local column axis). Materials named lamp_rear become brake lamps,
glass is drawn tinted and transparent by game/CarBody.gd.

Original MIT work for SimCruise. Shapes are measured from photographs and
published dimensions of the reference cars; no logos or badges.
"""
import bpy, bmesh, math, os, sys, json, importlib.util
import numpy as np
from mathutils import Vector, Matrix

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import surface  # noqa: E402
import design_tools  # noqa: E402

argv = sys.argv[sys.argv.index("--") + 1:]
design_path, out_path = argv[0], argv[1]
def opt(name, default=None):
    return argv[argv.index(name) + 1] if name in argv else default
render_path = opt("--render")
view = opt("--view", "three-quarter")
camera_json = opt("--camera")
FAST = "--fast" in argv

spec_mod = importlib.util.spec_from_file_location("design", design_path)
design_mod = importlib.util.module_from_spec(spec_mod)
spec_mod.loader.exec_module(design_mod)
D = design_mod.DESIGN

bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene

# ------------------------------------------------------------------ materials
def srgb(h):
    h = h.lstrip("#")
    c = [int(h[i:i + 2], 16) / 255 for i in (0, 2, 4)]
    return tuple(((v + 0.055) / 1.055) ** 2.4 if v > 0.04045 else v / 12.92 for v in c)

MATS = {}
DEBUG_COLORS = {"trim": "#00ff00", "satin": "#ff00ff", "glass": "#0000ff", "under": "#ffff00", "gap": "#00ffff"} if "--debug-mats" in argv else {}
def material(name, color, rough=0.5, metal=0.0, coat=0.0, alpha=1.0, emission=None):
    if name in MATS:
        return MATS[name]
    m = bpy.data.materials.new(name); m.use_nodes = True
    m.diffuse_color = (*srgb(DEBUG_COLORS.get(name, color)), 1)
    b = m.node_tree.nodes["Principled BSDF"]
    b.inputs["Base Color"].default_value = (*srgb(color), 1)
    b.inputs["Roughness"].default_value = rough; b.inputs["Metallic"].default_value = metal
    if coat:
        b.inputs["Coat Weight"].default_value = coat; b.inputs["Coat Roughness"].default_value = 0.03
    if emission:
        b.inputs["Emission Color"].default_value = (*srgb(emission), 1); b.inputs["Emission Strength"].default_value = 0.0
    if alpha < 1.0:
        b.inputs["Alpha"].default_value = alpha; m.surface_render_method = "BLENDED"
    MATS[name] = m
    return m

def base_materials():
    material("paint", D.get("paint", "#1f4e79"), 0.3, D.get("paint_metal", 0.35), coat=1.0)
    material("trim", "#0a0b0c", 0.5, 0.0)                       # black window trim, pillars
    MATS["trim"].node_tree.nodes["Principled BSDF"].inputs["Specular IOR Level"].default_value = 0.3
    material("satin", "#16171a", 0.6)                          # satin black plastic
    material("under", "#121314", 0.9)
    material("glass", "#0a0f13", 0.03, 0.0, alpha=0.55)
    material("chrome", "#d6dadf", 0.07, 1.0)
    material("gap", "#050506", 0.85)
    material("liner", "#0a0a0a", 0.95)
    material("rubber", "#141414", 0.9)
    material("rim", D.get("rim", "#9aa0a6"), 0.25, 0.9)
    material("disc", "#6b6d70", 0.45, 0.85)
    material("caliper", D.get("caliper", "#2b2d30"), 0.35, 0.2, coat=0.5)
    material("plate", "#ebe9e2", 0.5)
    material("trim_in", D.get("cabin", {}).get("trim_in", "#1c1d1f"), 0.75)
    material("headliner", D.get("cabin", {}).get("headliner", "#6f6e6a"), 0.9)
base_materials()

def link(o):
    if o.name not in scene.collection.objects:
        scene.collection.objects.link(o)
    return o

def mesh_object(name, verts, faces, mats, mat_index=None, smooth=True):
    me = bpy.data.meshes.new(name)
    me.from_pydata([tuple(v) for v in verts], [], [tuple(f) for f in faces])
    me.update()
    o = link(bpy.data.objects.new(name, me))
    for m in mats:
        o.data.materials.append(MATS[m])
    if mat_index is not None:
        o.data.polygons.foreach_set("material_index", list(mat_index))
    if smooth:
        o.data.polygons.foreach_set("use_smooth", [True] * len(o.data.polygons))
    return o

# ------------------------------------------------------------------ the skin
LV = design_tools.LEVELS
NL = len(LV)
rows = D["rows"]
R = len(rows)
net = np.array([r["pts"] for r in rows], float)
row_crease = np.zeros((R, NL), bool)
for i, r in enumerate(rows):
    for name in r["crease"]:
        row_crease[i, LV.index(name)] = True
level_crease = np.zeros((R, NL), bool)
rindex = {r["name"]: i for i, r in enumerate(rows)}
for name, span in D.get("level_crease", {}).items():
    j = LV.index(name)
    if span == "all":
        level_crease[:, j] = True
    else:
        level_crease[rindex[span[0]]:rindex[span[1]] + 1, j] = True

regions = D["regions"]
s_ins = sorted(set(v for reg in regions for v in reg["s"]))
l_ins = sorted(set(v for reg in regions for v in reg["l"]))
skin = surface.Skin(net, row_crease, level_crease, ds=max(D.get("ds", 0.028), 0.055), dl=max(D.get("dl", 0.02), 0.036),
                    s_inserts=s_ins, l_inserts=l_ins)
G, n_right = skin.ring()                        # (Ns, ring, 3)
NS, NR = G.shape[0], G.shape[1]
S = np.asarray(skin.s); Lr = np.asarray(skin.l)

def ring_l(c):
    """Net level coordinate of ring column c (left half mirrored)."""
    c %= NR
    return Lr[c] if c <= n_right - 1 else Lr[2 * (n_right - 1) - c]

# per-face region lookup
mat_names = ["paint"]
for reg in regions:
    if reg["mat"] not in mat_names:
        mat_names.append(reg["mat"])
face_mat = np.zeros((NS - 1, NR), int)
face_inset = -np.ones((NS - 1, NR), int)
s_mid = (S[:-1] + S[1:]) / 2
l_mid = np.array([(ring_l(c) + ring_l(c + 1)) / 2 for c in range(NR)])
eps = 1e-6
for ri, reg in enumerate(regions):
    a, b = reg["s"]; la, lb = reg["l"]
    rs = (s_mid > a - eps) & (s_mid < b + eps)
    cs = (l_mid > la - eps) & (l_mid < lb + eps)
    mask = rs[:, None] & cs[None, :]
    face_mat[mask] = mat_names.index(reg["mat"])
    if reg.get("inset"):
        face_inset[mask] = ri

N = surface.normals(G)
verts = [tuple(p) for p in G.reshape(-1, 3)]
def vid(i, c):
    return i * NR + (c % NR)
faces, fmat = [], []
dup = {}          # (inset region, base vertex id) -> new vertex id
walls = []
for i in range(NS - 1):
    for c in range(NR):
        q = [vid(i, c), vid(i + 1, c), vid(i + 1, c + 1), vid(i, c + 1)]
        g = face_inset[i, c]
        if g >= 0:
            d = regions[g]["inset"]
            qq = []
            for v in q:
                key = (g, v)
                if key not in dup:
                    ii, cc = divmod(v, NR)
                    p = G[ii, cc] - N[ii, cc] * d
                    dup[key] = len(verts); verts.append(tuple(p))
                qq.append(dup[key])
            q = qq
        # split along the same diagonal SkinMap interpolates on (mirrored on the left)
        if c < n_right - 1:
            faces.append([q[0], q[1], q[2]]); faces.append([q[0], q[2], q[3]])
        else:
            faces.append([q[0], q[1], q[3]]); faces.append([q[1], q[2], q[3]])
        fmat.append(face_mat[i, c]); fmat.append(face_mat[i, c])
# walls around inset regions: an edge on the region boundary (neighbour face
# outside the region) gets a dark quad from the skin down to the glass
def inset_of(i, c, g=-1):
    if i < 0 or i >= NS - 1:
        return g          # past a tip row: the region closes on the centre plane, no wall
    return face_inset[i, c % NR]
wall_mat = len(mat_names)
mat_names.append("satin")
for i in range(NS - 1):
    for c in range(NR):
        g = face_inset[i, c]
        if g < 0:
            continue
        # edges in winding order: (i,c)->(i+1,c) [left], (i+1,c)->(i+1,c+1) [front],
        # (i+1,c+1)->(i,c+1) [right], (i,c+1)->(i,c) [back]
        edges = [((i, c), (i + 1, c), inset_of(i, c - 1)),
                 ((i + 1, c), (i + 1, c + 1), inset_of(i + 1, c, g)),
                 ((i + 1, c + 1), (i, c + 1), inset_of(i, c + 1)),
                 ((i, c + 1), (i, c), inset_of(i - 1, c, g))]
        for (pa, pb, nb) in edges:
            if nb == g:
                continue
            a, b = vid(*pa), vid(*pb)
            faces.append([a, b, dup[(g, b)], dup[(g, a)]]); fmat.append(wall_mat)

body = mesh_object("body", verts, faces, mat_names, fmat)
bm = bmesh.new(); bm.from_mesh(body.data)
tip_verts = [v for v in bm.verts if abs(v.co.x) < 1e-7]
bmesh.ops.remove_doubles(bm, verts=tip_verts, dist=1e-6)
bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
for e in bm.edges:
    if len(e.link_faces) == 2 and e.calc_face_angle(0) > math.radians(D.get("sharp_deg", 48)):
        e.smooth = False
bm.to_mesh(body.data); bm.free()
print("SKIN", NS, "x", NR, "faces", len(body.data.polygons))

# ------------------------------------------------------------------ cabin lining
# the skin offset inward over the cabin, facing inside: door trims below the
# glass, headliner and pillar trims above
cab_rows = [rindex[n] for n in D.get("cabin_rows", ("c_base", "cowl"))]
glass_i = mat_names.index("glass") if "glass" in mat_names else -1
lin_ix, lv, lf, lm = {}, [], [], []
for i in range(NS - 1):
    if not (cab_rows[0] - eps <= s_mid[i] <= cab_rows[1] + eps):
        continue
    for c in range(NR):
        if face_mat[i, c] == glass_i or face_inset[i, c] >= 0 or l_mid[c] < LV.index(D.get("lining_from", "lower")):
            continue
        q = []
        for (ii, cc) in ((i, c), (i + 1, c), (i + 1, c + 1), (i, c + 1)):
            key = (ii, cc % NR)
            if key not in lin_ix:
                lin_ix[key] = len(lv); lv.append(G[ii, cc % NR] - N[ii, cc % NR] * D.get("lining_depth", 0.03))
            q.append(lin_ix[key])
        lf.append(list(reversed(q))); lm.append(1 if l_mid[c] > LV.index("rail") + 0.3 else 0)
lining = mesh_object("lining", lv, lf, ["trim_in", "headliner"], lm)
dec = lining.modifiers.new("dec", "DECIMATE"); dec.decimate_type = "DISSOLVE"; dec.angle_limit = math.radians(4)
bpy.context.view_layer.objects.active = lining; bpy.ops.object.modifier_apply(modifier="dec")

# ------------------------------------------------------------------ wheel arches
WB = D["wheelbase"]; RT = D["tire_r"]; TW = D["tire_w"]
axles = [("f", WB / 2, D["track_f"], D["arch_r"][0]), ("r", -WB / 2, D["track_r"], D["arch_r"][1])]
src = body.copy(); src.data = body.data.copy(); link(src)
for (ax, y, track, ar) in axles:
    bpy.ops.mesh.primitive_cylinder_add(vertices=96, radius=ar, depth=2.2, location=(0, y, D.get("arch_z", RT)),
                                        rotation=(0, math.pi / 2, 0))
    cyl = bpy.context.active_object
    # keep the centre of the car: subtract only the outer parts (two short cylinders)
    bpy.ops.mesh.primitive_cube_add(size=1, location=(0, y, D.get("arch_z", RT)))
    keep = bpy.context.active_object; keep.scale = (2 * (track / 2 - D.get("well_inner", 0.33)), 2.0, 2.0)
    m = cyl.modifiers.new("keep", "BOOLEAN"); m.operation = "DIFFERENCE"; m.object = keep; m.solver = "EXACT"
    bpy.context.view_layer.objects.active = cyl; bpy.ops.object.modifier_apply(modifier="keep")
    bpy.data.objects.remove(keep)
    m = body.modifiers.new("arch", "BOOLEAN"); m.operation = "DIFFERENCE"; m.object = cyl; m.solver = "EXACT"
    bpy.context.view_layer.objects.active = body; bpy.ops.object.modifier_apply(modifier="arch")
    bpy.data.objects.remove(cyl)
# keep the skin's own shading at the cut: copy normals from the uncut skin,
# but only around the arches (thin parts such as the glass walls keep theirs)
zone = body.vertex_groups.new(name="arch_zone")
for v in body.data.vertices:
    for (ax, y, track, ar) in axles:
        r = math.hypot(v.co.y - y, v.co.z - D.get("arch_z", RT))
        if abs(v.co.x) > track / 2 - D.get("well_inner", 0.33) - 0.02 and r < ar + 0.12:
            zone.add([v.index], 1.0, "REPLACE")
dt = body.modifiers.new("normals", "DATA_TRANSFER"); dt.object = src; dt.vertex_group = "arch_zone"
dt.use_loop_data = True; dt.data_types_loops = {"CUSTOM_NORMAL"}; dt.loop_mapping = "POLYINTERP_NEAREST"
bpy.context.view_layer.objects.active = body
try:
    bpy.ops.object.modifier_apply(modifier="normals")
except RuntimeError as e:
    print("normal transfer skipped:", e)
    body.modifiers.remove(dt)
bpy.data.objects.remove(src)

# arch liners: black shells inside the arches
def lathe(name, profile, segments, mat, a0=0.0, a1=2 * math.pi, closed=True):
    """Revolve [(radius, x)] around the local X axis."""
    bm = bmesh.new(); rings = []
    cnt = segments if closed else segments + 1
    for (r, x) in profile:
        rings.append([bm.verts.new((x, r * math.cos(a0 + (a1 - a0) * k / segments), r * math.sin(a0 + (a1 - a0) * k / segments))) for k in range(cnt)])
    for a, b in zip(rings, rings[1:]):
        for k in range(segments):
            bm.faces.new((a[k], a[(k + 1) % cnt], b[(k + 1) % cnt], b[k]))
    me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free()
    o = link(bpy.data.objects.new(name, me)); o.data.materials.append(MATS[mat])
    for p in o.data.polygons:
        p.use_smooth = True
    return o

for (ax, y, track, ar) in axles:
    for s in (1, -1):
        inner = track / 2 - D.get("well_inner", 0.33)
        o = lathe("liner_" + ax, [(ar - 0.006, inner * s), (ar - 0.006, (track / 2 + 0.2) * s)], 48, "liner",
                  math.radians(-25), math.radians(205), closed=False)
        o.location = (0, y, D.get("arch_z", RT))
        if s < 0:
            for p in o.data.polygons:
                p.flip()
        # inner wall closing the well toward the engine bay
        o2 = lathe("well_" + ax, [(0.0, inner * s), (ar - 0.006, inner * s)], 48, "liner", math.radians(-25), math.radians(205), closed=False)
        o2.location = (0, y, D.get("arch_z", RT))

# ------------------------------------------------------------------ skin features
import features  # noqa: E402
FEAT = features.Features(skin, MATS, material, link)
FEAT.build(D.get("features", []))
import parts  # noqa: E402
PARTS = parts.Parts(D, MATS, link, FEAT.map)
PARTS.build()
if "--no-cabin" not in argv:
    import cabin  # noqa: E402
    cabin.build(D, MATS, material, link, PARTS)

# ------------------------------------------------------------------ wheels
def box(name, size, loc, mat, bevel=0.01, rot=None):
    bpy.ops.mesh.primitive_cube_add(size=1, location=loc)
    o = bpy.context.active_object; o.name = name; o.scale = size
    bpy.ops.object.transform_apply(scale=True)
    if bevel:
        b = o.modifiers.new("bevel", "BEVEL"); b.width = bevel; b.segments = 3
        bpy.ops.object.modifier_apply(modifier="bevel")
    if rot:
        o.rotation_euler = rot
    o.data.materials.append(MATS[mat]); bpy.ops.object.shade_smooth()
    return o

import wheels  # noqa: E402
wheels.build(D, MATS, lathe, box, link)

# ------------------------------------------------------------------ render / export
def render(path, view_name):
    Lc = D["length"]; Hc = D["height"]
    bpy.ops.object.light_add(type="SUN"); sun = bpy.context.active_object
    sun.data.energy = 4.0; sun.rotation_euler = (math.radians(48), 0, math.radians(35))
    world = bpy.data.worlds.new("w"); scene.world = world; world.use_nodes = True
    bg = world.node_tree.nodes["Background"]; bg.inputs[1].default_value = 1.0
    hdr = os.path.join(HERE, "..", "..", "game", "art", "day.hdr")
    if os.path.exists(hdr):
        env = world.node_tree.nodes.new("ShaderNodeTexEnvironment"); env.image = bpy.data.images.load(hdr)
        world.node_tree.links.new(env.outputs["Color"], bg.inputs["Color"])
    else:
        bg.inputs[0].default_value = (0.62, 0.68, 0.76, 1)
    bpy.ops.mesh.primitive_plane_add(size=80); ground = bpy.context.active_object
    ground.data.materials.append(material("ground", "#77766f", 0.9))
    cam = link(bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))); scene.camera = cam
    eye = D.get("eye", (-0.36, 0.0, 1.14))
    if camera_json:
        cj = json.load(open(camera_json))
        loc, aim, lens = cj["location"], cj["target"], cj["focal_mm"]
        scene.render.resolution_x, scene.render.resolution_y = cj["resolution"]
        cam.data.sensor_width = cj.get("sensor_mm", 36.0); cam.data.sensor_fit = "HORIZONTAL"
        scene.render.film_transparent = True
        ground.hide_render = True
    else:
        places = {"three-quarter": ((Lc * 0.95, Lc * 1.0, Hc * 1.1), (0, 0.25, Hc * 0.42), 42),
                  "rear-three-quarter": ((-Lc * 0.95, -Lc * 1.0, Hc * 1.15), (0, -0.25, Hc * 0.45), 42),
                  "side": ((-Lc * 2.4, 0.0, Hc * 0.75), (0, 0, Hc * 0.48), 55),
                  "front": ((0, Lc * 2.2, Hc * 0.85), (0, 0, Hc * 0.5), 60),
                  "rear": ((0, -Lc * 2.2, Hc * 0.95), (0, 0, Hc * 0.5), 60),
                  "top": ((0, 0.0, 12.0), (0, 0.001, 0), 50),
                  "cabin": (eye, (eye[0], eye[1] + 4, eye[2] - 0.25), 20)}
        if view_name.startswith("custom:"):
            _, a, b, c = view_name.split(":")
            places[view_name] = (tuple(map(float, a.split(","))), tuple(map(float, b.split(","))), float(c))
        loc, aim, lens = places[view_name]
        scene.render.resolution_x = 1280; scene.render.resolution_y = 720
    cam.location = loc; cam.data.lens = lens; cam.data.clip_start = 0.02
    cam.rotation_euler = (Vector(aim) - Vector(loc)).to_track_quat("-Z", "Y").to_euler()
    if FAST:
        scene.render.engine = "BLENDER_WORKBENCH"
        scene.display.shading.light = "STUDIO"; scene.display.shading.color_type = "MATERIAL"
        scene.display.shading.show_cavity = False
    else:
        scene.render.engine = "BLENDER_EEVEE"
    scene.render.filepath = path
    bpy.ops.render.render(write_still=True)
    for o in (ground, cam, sun):
        bpy.data.objects.remove(o)

if render_path:
    render(render_path, view)

if out_path != "-":
    bpy.ops.export_scene.gltf(filepath=out_path, export_format="GLB", export_apply=True, export_yup=True)
print("BUILT", out_path, "faces", sum(len(o.data.polygons) for o in scene.objects if o.type == "MESH"))
