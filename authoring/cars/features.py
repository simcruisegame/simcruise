"""Skin features for build_car.py: lamps, grilles, intakes, cladding and shut
lines as patches that follow the body skin exactly (see skinmap.py), with
painted textures for lamp internals and grille meshes (paint.py).

Design entries (DESIGN["features"]):
  {"kind": "patch", "name", "view": side|front|rear|top|sl, "outline": [(u, v)...],
   "mirror": True, "mat": "satin" or {"tex": style, ...}, "lift": 0.002, "radius": 0.0}
  {"kind": "line", "name", "view", "path": [(u, v)...], "width": 0.0035, "mirror": True}
Outlines in a view are in metres in that view's plane (side: (y, z); front and
rear: (x, z); top: (x, y)); "sl" outlines are net coordinates (row, level).
"""
import math
import numpy as np
import bpy

import paint
from skinmap import SkinMap, resample, round_poly


class Features:
    def __init__(self, skin, MATS, material, link):
        self.map = SkinMap(skin.grid, skin.s, skin.l)
        self.MATS, self.material, self.link = MATS, material, link
        self.images = {}

    # -------------------------------------------------------------- geometry
    def outline_sl(self, f, n=88):
        pts = f["outline"]
        if f["view"] == "xyz":
            # 3D points snapped to the skin, then rounded and resampled in (s, l)
            sl = self.map.nearest_sl(pts)
            if f.get("radius"):
                sl = round_poly(sl, f["radius"])
            return [tuple(p) for p in resample(sl, n)]
        if f.get("radius"):
            pts = round_poly(pts, f["radius"])
        pts = resample(pts, n)
        if f["view"] == "sl":
            return [tuple(p) for p in pts]
        return self.map.to_sl(f["view"], pts)

    def patch_mesh(self, sl, lift, rings=7, side=1, step=0.016):
        """Constrained Delaunay triangulation of the (s, l) outline with
        interior points on a ~1.6 cm metric grid, laid on the skin: small,
        even triangles that follow the curvature (no chords under the skin)."""
        from mathutils import Vector
        from mathutils.geometry import delaunay_2d_cdt
        sl = np.asarray(sl, float)
        s0, l0 = sl.min(axis=0); s1, l1 = sl.max(axis=0)
        sc, lc = (s0 + s1) / 2, (l0 + l1) / 2
        # metres per unit of s and l around the patch centre
        ms = np.linalg.norm(self.map.point(s1, lc) - self.map.point(s0, lc)) / max(s1 - s0, 1e-9)
        ml = np.linalg.norm(self.map.point(sc, l1) - self.map.point(sc, l0)) / max(l1 - l0, 1e-9)
        ms = max(ms, 1e-3); ml = max(ml, 1e-3)
        P = [((s - s0) * ms, (l - l0) * ml) for (s, l) in sl]
        n = len(P)
        W, H = (s1 - s0) * ms, (l1 - l0) * ml
        inner = []
        Pa = np.asarray(P)
        for u in np.arange(step * 0.5, W, step):
            for v in np.arange(step * 0.5, H, step):
                # point in polygon, and not too close to the outline
                x, y = u, v; hit = False
                for (ax, ay), (bx, by) in zip(Pa, np.roll(Pa, -1, axis=0)):
                    if (ay > y) != (by > y) and x < (bx - ax) * (y - ay) / (by - ay + 1e-12) + ax:
                        hit = not hit
                if hit and np.min(np.hypot(Pa[:, 0] - x, Pa[:, 1] - y)) > step * 0.45:
                    inner.append((u, v))
        pts = [Vector((x, y)) for (x, y) in P + inner]
        edges = [(i, (i + 1) % n) for i in range(n)]
        out = delaunay_2d_cdt(pts, edges, [list(range(n))], 1, 1e-7)
        vco, fcs = out[0], out[2]
        verts, uvs = [], []
        for v in vco:
            s = s0 + v.x / ms; l = l0 + v.y / ml
            verts.append(self.map.point(s, l, lift, side))
            uvs.append((v.x / max(W, 1e-9), v.y / max(H, 1e-9)))
        faces = [list(f) for f in fcs]
        return verts, faces, uvs

    def path_sl(self, f, pts, n=90):
        if f["view"] == "xyz":
            return resample(self.map.nearest_sl(pts), n, closed=False)
        P = resample(pts, n, closed=False)
        return np.asarray(self.map.to_sl(f["view"], P)) if f["view"] != "sl" else P

    def band_mesh(self, outer, inner, lift, thick, side=1):
        """Quads between two (s, l) polylines; with thick > 0 the band stands
        proud of the skin by `thick` and gets an edge wall along the outer line
        and both ends (cladding, flares)."""
        verts, faces, uvs = [], [], []
        n = len(outer)
        top = lift + thick
        for i in range(n):
            for k, (s, l) in enumerate((outer[i], inner[i])):
                verts.append(self.map.point(s, l, top, side)); uvs.append((i / (n - 1), float(k)))
        for i in range(n - 1):
            a = 2 * i
            faces.append([a, a + 2, a + 3, a + 1])
        if thick > 0:
            base = len(verts)
            for i in range(n):
                s, l = outer[i]
                verts.append(self.map.point(s, l, lift * 0.5, side)); uvs.append((i / (n - 1), 0.0))
            for i in range(n - 1):
                faces.append([base + i, base + i + 1, 2 * (i + 1), 2 * i])
            # inner wall too (down to the skin), so the edge has thickness from any side
            base2 = len(verts)
            for i in range(n):
                s, l = inner[i]
                verts.append(self.map.point(s, l, lift * 0.5, side)); uvs.append((i / (n - 1), 1.0))
            for i in range(n - 1):
                faces.append([2 * i + 1, 2 * (i + 1) + 1, base2 + i + 1, base2 + i])
        return verts, faces, uvs

    def strip_mesh(self, sl, width, lift, side=1):
        sl = resample(np.asarray(sl, float), max(8, len(sl) * 3), closed=False)
        P = [self.map.point(s, l, lift, 1) for (s, l) in sl]
        Nn = [self.map.normal(s, l, 1) for (s, l) in sl]
        verts, faces = [], []
        for i, p in enumerate(P):
            t = P[min(i + 1, len(P) - 1)] - P[max(i - 1, 0)]
            t = t / max(np.linalg.norm(t), 1e-9)
            b = np.cross(Nn[i], t); b = b / max(np.linalg.norm(b), 1e-9)
            for sg in (-1, 1):
                q = p + b * sg * width / 2
                verts.append(q * np.array([side, 1, 1]))
        for i in range(len(P) - 1):
            a, b = 2 * i, 2 * i + 2
            faces.append([a, b, b + 1, a + 1])
        return verts, faces

    def make_object(self, name, verts, faces, mat, uvs=None, flip=False):
        me = bpy.data.meshes.new(name)
        fs = [list(reversed(f)) for f in faces] if flip else faces
        me.from_pydata([tuple(v) for v in verts], [], [tuple(f) for f in fs])
        if uvs is not None:
            uv = me.uv_layers.new(name="UVMap")
            for poly in me.polygons:
                for li in poly.loop_indices:
                    uv.data[li].uv = uvs[me.loops[li].vertex_index]
        me.update()
        o = self.link(bpy.data.objects.new(name, me))
        o.data.materials.append(mat)
        for p in o.data.polygons:
            p.use_smooth = True
        return o

    def orient(self, o):
        """Make the patch face outward (away from the body centre)."""
        me = o.data
        if not len(me.polygons):
            return
        me.calc_normals_split() if hasattr(me, "calc_normals_split") else None
        score = 0.0
        for p in me.polygons:
            c = p.center
            out = np.array([c.x, c.y * 0.6, c.z - 0.6])
            score += float(np.dot(np.array(p.normal), out)) * p.area
        if score < 0:
            for p in me.polygons:
                p.flip()

    # -------------------------------------------------------------- build
    def build(self, entries):
        made = []
        for f in entries:
            kind = f.get("kind", "patch")
            sides = (1, -1) if f.get("mirror", True) else (1,)
            if kind == "patch":
                sl = self.outline_sl(f)
                mat = self.patch_material(f, sl)
                for sd in sides:
                    v, fc, uv = self.patch_mesh(sl, f.get("lift", 0.002), f.get("rings", 7), sd)
                    if isinstance(f.get("mat"), dict) and f["mat"].get("tex") == "photo":
                        # the photograph projected head-on: each side samples its own half
                        pm = f["mat"]["map"]; img = mat.node_tree.nodes["photo"].image
                        Wi, Hi = img.size
                        sg = -1.0 if pm.get("view", "front") == "front" else 1.0
                        uv = [((pm["cx"] + sg * q[0] * pm["sx"]) / Wi, 1.0 - (pm["py0"] + (pm["z0"] - q[2]) * pm["sz"]) / Hi) for q in v]
                    elif f.get("uv") == "view":
                        # centred parts (plates): one texture across both halves, seen upright
                        P = np.asarray(f["outline"], float)
                        Wd = max(P[:, 0].max(), 1e-6); z0, z1 = P[:, 1].min(), P[:, 1].max()
                        sign = -1.0 if f["view"] == "front" else 1.0
                        uv = [((sign * q[0] + Wd) / (2 * Wd), (q[2] - z0) / max(z1 - z0, 1e-6)) for q in v]
                    o = self.make_object(f["name"] + ("_l" if sd < 0 else ""), v, fc, mat, uv)
                    self.orient(o); made.append(o)
            elif kind == "band":
                # a strip between an outer and an inner outline (open ends), e.g. arch cladding
                outer = self.path_sl(f, f["outer"]); inner = self.path_sl(f, f["inner"])
                n = max(len(outer), len(inner))
                outer = resample(outer, n, closed=False); inner = resample(inner, n, closed=False)
                mat = self.patch_material(f, np.vstack([outer, inner]))
                for sd in sides:
                    v, fc, uv = self.band_mesh(outer, inner, f.get("lift", 0.002), f.get("thick", 0.0), sd)
                    o = self.make_object(f["name"] + ("_l" if sd < 0 else ""), v, fc, mat, uv)
                    self.orient(o); made.append(o)
            elif kind == "line":
                pts = f["path"]
                if f["view"] == "xyz":
                    sl = resample(self.map.nearest_sl(pts), 80, closed=False)
                else:
                    pts = resample(pts, 60, closed=False) if f["view"] != "sl" else np.asarray(pts, float)
                    sl = pts if f["view"] == "sl" else self.map.to_sl(f["view"], pts)
                for sd in sides:
                    v, fc = self.strip_mesh(sl, f.get("width", 0.0035), f.get("lift", 0.0008), sd)
                    o = self.make_object(f["name"] + ("_l" if sd < 0 else ""), v, fc, self.MATS[f.get("mat", "gap")])
                    self.orient(o); made.append(o)
        return made

    # -------------------------------------------------------------- materials
    def patch_material(self, f, sl):
        mat = f.get("mat", "satin")
        if isinstance(mat, str):
            return self.MATS[mat]
        style = mat["tex"]
        if style == "photo":
            name = mat["name"]
            if name in self.MATS:
                return self.MATS[name]
            m = bpy.data.materials.new(name); m.use_nodes = True
            nt = m.node_tree; b = nt.nodes["Principled BSDF"]
            t = nt.nodes.new("ShaderNodeTexImage"); t.name = "photo"
            t.image = bpy.data.images.load(mat["path"]); t.image.pack(); t.extension = "EXTEND"
            nt.links.new(t.outputs["Color"], b.inputs["Base Color"])
            b.inputs["Roughness"].default_value = mat.get("rough", 0.2)
            if mat.get("coat", 0):
                b.inputs["Coat Weight"].default_value = mat["coat"]; b.inputs["Coat Roughness"].default_value = 0.03
            if mat.get("emit_mask"):
                # brake lamps: only the masked (red lens) pixels glow; the game drives the strength
                e = nt.nodes.new("ShaderNodeTexImage"); e.image = bpy.data.images.load(mat["emit_mask"]); e.image.pack()
                nt.links.new(e.outputs["Color"], b.inputs["Emission Color"])
                b.inputs["Emission Strength"].default_value = 0.2   # the game sets it (0 at rest, bright when braking)
            elif mat.get("emission"):
                nt.links.new(t.outputs["Color"], b.inputs["Emission Color"])
                b.inputs["Emission Strength"].default_value = mat["emission"]
            m.diffuse_color = (0.2, 0.2, 0.2, 1)
            self.MATS[name] = m
            return m
        if style == "image":
            m = bpy.data.materials.new(mat["name"]); m.use_nodes = True
            nt = m.node_tree; b = nt.nodes["Principled BSDF"]
            t = nt.nodes.new("ShaderNodeTexImage"); t.image = bpy.data.images.load(mat["path"]); t.image.pack()
            t.extension = "EXTEND"
            nt.links.new(t.outputs["Color"], b.inputs["Base Color"])
            b.inputs["Roughness"].default_value = mat.get("rough", 0.45)
            m.diffuse_color = (0.8, 0.8, 0.8, 1)
            self.MATS[mat["name"]] = m
            return m
        sl = np.asarray(sl, float)
        s0, l0 = sl.min(axis=0); s1, l1 = sl.max(axis=0)
        uvo = [((s - s0) / max(s1 - s0, 1e-9), (l - l0) / max(l1 - l0, 1e-9)) for (s, l) in sl]
        w, h = mat.get("size", (512, 192))
        base, emit = STYLES[style](uvo, w, h, mat)
        name = mat.get("name", f["name"])
        m = bpy.data.materials.new(name); m.use_nodes = True
        nt = m.node_tree; b = nt.nodes["Principled BSDF"]
        img = self.image(name + "_albedo", base)
        tex = nt.nodes.new("ShaderNodeTexImage"); tex.image = img
        nt.links.new(tex.outputs["Color"], b.inputs["Base Color"])
        b.inputs["Roughness"].default_value = mat.get("rough", 0.15)
        b.inputs["Metallic"].default_value = mat.get("metal", 0.0)
        if mat.get("coat", 1.0):
            b.inputs["Coat Weight"].default_value = mat.get("coat", 1.0); b.inputs["Coat Roughness"].default_value = 0.02
        if emit is not None:
            eimg = self.image(name + "_emission", emit)
            et = nt.nodes.new("ShaderNodeTexImage"); et.image = eimg
            nt.links.new(et.outputs["Color"], b.inputs["Emission Color"])
            b.inputs["Emission Strength"].default_value = mat.get("emission", 1.0)
        m.diffuse_color = tuple(base[..., :3].reshape(-1, 3).mean(axis=0)) + (1,)
        self.MATS[name] = m
        return m

    def image(self, name, arr):
        h, w = arr.shape[:2]
        img = bpy.data.images.new(name, w, h, alpha=True)
        img.pixels.foreach_set(np.ascontiguousarray(arr[::-1].astype(np.float32)).ravel())
        img.file_format = "PNG"
        img.pack()
        self.images[name] = img
        return img


# ------------------------------------------------------------------ texture styles
def _shrink(poly, f, c=None):
    P = np.asarray(poly, float)
    c = P.mean(axis=0) if c is None else np.asarray(c)
    return c + (P - c) * f


def _edge(poly, which="top", frac=0.5):
    """Points of the outline on its top (or bottom) side, ordered along u."""
    P = np.asarray(poly, float)
    vc = P[:, 1].mean()
    sel = P[P[:, 1] >= vc] if which == "top" else P[P[:, 1] < vc]
    return sel[np.argsort(sel[:, 0])]


def headlamp(uv, w, h, o):
    """Slim LED headlamp: smoked housing, chrome reflector band, projector
    cells, a light pipe along the top edge and an amber outboard segment."""
    c = paint.Canvas(w, h, o.get("housing", "#16181b")); e = paint.Canvas(w, h, "#000000")
    P = np.asarray(uv)
    c.poly(_shrink(P, 0.86), o.get("bezel", "#4c5156"))
    c.poly(_shrink(P, 0.74), o.get("inner", "#0d0e10"))
    c.gradient_v("#22262b", "#0b0c0e", 0.5)
    cells = o.get("cells", [0.58, 0.72, 0.86])
    vc = float(np.median(P[:, 1])) - 0.06
    for u in cells:
        r = o.get("cell_r", 0.055)
        c.ellipse(u, vc, r, r * w / h, "#7c8186"); c.ellipse(u, vc, r * 0.72, r * 0.72 * w / h, "#cfd8e2")
        c.ellipse(u - r * 0.2, vc + r * 0.5, r * 0.25, r * 0.25 * w / h, "#ffffff")
        e.ellipse(u, vc, r * 0.72, r * 0.72 * w / h, "#9aa4ad")
    top = _edge(P, "top")
    top = top[(top[:, 0] > 0.04) & (top[:, 0] < 0.97)]
    if len(top) > 2:
        pipe = top - np.array([0.0, o.get("pipe_in", 0.16)])
        c.line(pipe, o.get("pipe_w", 0.022), "#f4f8fb"); e.line(pipe, o.get("pipe_w", 0.022), "#ffffff")
    if o.get("amber", True):
        a0, a1 = o.get("amber_u", (0.02, 0.16))
        m = paint.Canvas(w, h, "#000000")
        c.poly([(a0, 0.0), (a1, 0.0), (a1, 1.0), (a0, 1.0)], "#b8651a", 0.55)
    return c.result(), e.result()


def taillamp(uv, w, h, o):
    """Taillamp: dark red smoked lens, bright red light guide along the top
    edge and a clear reverse segment at the inner end."""
    c = paint.Canvas(w, h, o.get("lens", "#4a0b0f")); e = paint.Canvas(w, h, "#000000")
    P = np.asarray(uv)
    c.gradient_v("#5a0d12", "#22070a", 0.8)
    c.poly(_shrink(P, 0.82), o.get("inner", "#2a0a0d"))
    top = _edge(P, "top")
    for k, (din, wd) in enumerate(o.get("guides", [(0.13, 0.05), (0.32, 0.035)])):
        g = top - np.array([0.0, din])
        g = g[(g[:, 0] > 0.03) & (g[:, 0] < 0.97)]
        if len(g) > 2:
            c.line(g, wd, "#ff2a2a"); e.line(g, wd, "#ff3030")
    ru0, ru1 = o.get("reverse_u", (0.78, 0.94))
    c.rect(ru0, 0.18, ru1, 0.42, "#c9c9c9", 0.9, r=0.02)
    c.rect(ru0 + 0.01, 0.2, ru1 - 0.01, 0.4, "#e8e8e8", 0.6, r=0.02)
    return c.result(), e.result()


def grille(uv, w, h, o):
    c = paint.Canvas(w, h, o.get("base", "#0b0c0d"))
    c.pattern(o.get("pattern", "honeycomb"), o.get("scale", 26), o.get("rib", "#23262a"), line=o.get("line", 0.12))
    if o.get("frame"):
        c.poly(np.asarray(uv), o["frame"], 0.0)
    return c.result(), None


def reflector(uv, w, h, o):
    c = paint.Canvas(w, h, o.get("color", "#6d0c10"))
    c.pattern("mesh", 60, "#9a1418", line=0.2)
    return c.result(), None


def plate(uv, w, h, o):
    c = paint.Canvas(w, h, "#efeee8")
    c.rect(0.03, 0.06, 0.97, 0.94, "#1d3c8a", 1.0)
    c.rect(0.05, 0.1, 0.95, 0.9, "#efeee8", 1.0)
    # SIMCRUISE wordmark as simple bars (no real registration)
    x = 0.12
    for k in range(8):
        c.rect(x, 0.32, x + 0.07, 0.68, "#1d3c8a", 1.0, r=0.01); x += 0.095
    return c.result(), None


STYLES = {"headlamp": headlamp, "taillamp": taillamp, "grille": grille, "reflector": reflector, "plate": plate}
