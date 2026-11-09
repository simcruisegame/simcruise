"""Separate exterior parts for build_car.py: mirrors, door handles, spoilers,
exhaust tips, antenna, wipers, roof rails, tow hooks, running boards.

Parts that sit on the body are snapped to the skin (SkinMap): the design
gives a rough 3D point and the part is placed on the nearest skin point,
oriented by the skin normal there.
"""
import math
import bmesh
import bpy
import numpy as np
from mathutils import Vector, Matrix


class Parts:
    def __init__(self, D, MATS, link, skinmap):
        self.D, self.MATS, self.link, self.map = D, MATS, link, skinmap

    # ------------------------------------------------------------ primitives
    def rounded(self, name, size, loc, mat, levels=2, taper=None, rot=None, round_frac=0.45):
        """A rounded block with half-extents `size`: a beveled box (corner
        radius round_frac of the smallest half-extent). taper: list of
        (axis, sign, scale_a, scale_b) narrowing one end. levels=0: sharp."""
        bm = bmesh.new()
        bmesh.ops.create_cube(bm, size=2.0)
        for v in bm.verts:
            v.co.x *= size[0]; v.co.y *= size[1]; v.co.z *= size[2]
        if taper:
            for (axis, sign, sa, sb) in taper:
                ai = "xyz".index(axis)
                others = [k for k in range(3) if k != ai]
                for v in bm.verts:
                    if (v.co[ai] > 0) == (sign > 0):
                        v.co[others[0]] *= sa; v.co[others[1]] *= sb
        if levels:
            r = min(size) * round_frac * 2
            bmesh.ops.bevel(bm, geom=list(bm.edges), offset=r, segments=2 + 2 * levels, profile=0.5, affect="EDGES")
        me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free()
        o = self.link(bpy.data.objects.new(name, me))
        o.data.materials.append(self.MATS[mat])
        for p in o.data.polygons:
            p.use_smooth = True
        o.location = loc
        if rot is not None:
            o.rotation_euler = rot
        return o

    def cylinder(self, name, r, depth, loc, mat, rot=(0, 0, 0), verts=32):
        bpy.ops.mesh.primitive_cylinder_add(vertices=verts, radius=r, depth=depth, location=loc, rotation=rot)
        o = bpy.context.active_object; o.name = name
        o.data.materials.append(self.MATS[mat])
        bpy.ops.object.shade_smooth()
        return o

    def on_skin(self, p):
        """Nearest skin point and normal to a rough 3D point on the right side."""
        (s, l), = self.map.nearest_sl([p])
        return self.map.point(s, l), self.map.normal(s, l)

    # ------------------------------------------------------------ parts
    def mirror(self, m):
        """Door mirror: tapered rounded housing, glass on the rear face and a
        foot on the door or the A-pillar base."""
        at = np.array(m["at"], float)            # rough foot position (right side)
        base, n = self.on_skin(at)
        w, ln, h = m.get("size", (0.21, 0.11, 0.125))
        for side in (1, -1):
            cx = base[0] + n[0] * 0.02 + w * 0.5 + m.get("out", 0.03)
            c = Vector((side * cx, base[1] + m.get("dy", -0.02), base[2] + m.get("dz", 0.06)))
            hous = self.rounded("mirror", (w / 2, ln / 2, h / 2), c, m.get("mat", "paint"), 2,
                                taper=[("x", -side, 0.62, 0.72), ("y", 1, 0.55, 0.85)])
            hous.rotation_euler = (0, 0, math.radians(-side * m.get("yaw", 6)))
            g = self.rounded("mirror_glass", (w / 2 * 0.78, 0.004, h / 2 * 0.74), Vector((side * -w * 0.06, -ln / 2 * 0.62 - 0.004, 0)), "chrome", 2)
            g.parent = hous
            foot = self.rounded("mirror_foot", (0.05, 0.07, 0.03), Vector((side * (base[0] + 0.03), base[1] + m.get("dy", -0.02), base[2] + 0.025)),
                                m.get("foot_mat", "trim"), 2, taper=[("x", side, 0.6, 0.7)])

    def handles(self, hs):
        for h in hs:
            p, n = self.on_skin(np.array(h["at"], float))
            for side in (1, -1):
                o = self.rounded("handle", (0.008, h.get("len", 0.14) / 2, 0.013),
                                 Vector((side * (p[0] + 0.006), p[1], p[2])), h.get("mat", "paint"), 2,
                                 taper=[("y", 1, 0.8, 0.75)])
                o.rotation_euler = (0, 0, -side * math.atan2(n[1], abs(n[0])))
                # dark pocket behind the handle
                pk = self.rounded("handle_pocket", (0.003, h.get("len", 0.14) * 0.42, 0.022),
                                  Vector((side * (p[0] + 0.0012), p[1] - 0.005, p[2] - 0.006)), "gap", 2)

    def wing(self, w):
        """Trunk-lid spoiler: an airfoil between two end plates on short stands."""
        span, chord, thick = w["span"], w["chord"], w.get("thick", 0.018)
        y0, z0 = w["y"], w["z"]
        bm = bmesh.new()
        prof = [(0.0, 0.0), (0.15, 0.55), (0.45, 0.85), (0.8, 0.6), (1.0, 0.25), (1.0, 0.0), (0.7, -0.15), (0.3, -0.2), (0.05, -0.1)]
        rings = []
        n = 24
        for k in range(n + 1):
            x = -span / 2 + span * k / n
            sweep = w.get("sweep", 0.04) * (2 * abs(x) / span) ** 2
            rise = w.get("rise", 0.0) * (2 * abs(x) / span) ** 2
            rings.append([bm.verts.new((x, y0 - u * chord - sweep, z0 + v * thick + rise)) for (u, v) in prof])
        for a, b in zip(rings, rings[1:]):
            for i in range(len(prof)):
                bm.faces.new((a[i], b[i], b[(i + 1) % len(prof)], a[(i + 1) % len(prof)]))
        bm.faces.new(rings[0]); bm.faces.new(list(reversed(rings[-1])))
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
        me = bpy.data.meshes.new("wing"); bm.to_mesh(me); bm.free()
        o = self.link(bpy.data.objects.new("wing", me)); o.data.materials.append(self.MATS[w.get("mat", "trim")])
        for p in o.data.polygons:
            p.use_smooth = True
        for side in (1, -1):
            if w.get("plates", True):
                self.rounded("wing_plate", (0.006, chord * 0.55, w.get("plate_h", 0.035)),
                             Vector((side * span / 2, y0 - chord * 0.5 - w.get("sweep", 0.04), z0 + thick * 0.4 + w.get("rise", 0.0) - 0.01)),
                             w.get("mat", "trim"), 1)
            if w.get("stand_h", 0.0) > 0:
                self.rounded("wing_stand", (0.012, chord * 0.3, w["stand_h"] / 2),
                             Vector((side * span * 0.34, y0 - chord * 0.45, z0 - w["stand_h"] / 2)), w.get("mat", "trim"), 1)

    def exhaust(self, e):
        for x in e["x"]:
            r = e.get("r", 0.048)
            c = (x, e["y"], e["z"])
            self.cylinder("exhaust", r, e.get("len", 0.12), c, "chrome", rot=(math.pi / 2, 0, 0))
            self.cylinder("exhaust_in", r * 0.82, e.get("len", 0.12) + 0.004, c, "gap", rot=(math.pi / 2, 0, 0))

    def fin(self, f):
        """Shark-fin antenna on the roof centre line."""
        p, n = self.on_skin(np.array([0.0005, f["y"], 1.6]))
        ln, h, w = f.get("len", 0.17), f.get("h", 0.055), f.get("w", 0.065)
        bm = bmesh.new()
        prof = [(0.0, 0.0), (0.35, 0.55), (0.75, 1.0), (1.0, 0.9), (1.0, 0.0)]
        L = []
        for (u, v) in prof:
            for sx in (-1, 1):
                L.append(bm.verts.new((sx * w / 2 * (1 - 0.6 * v), p[1] + ln / 2 - u * ln, p[2] + v * h)))
        me = bpy.data.meshes.new("fin"); bm.to_mesh(me); bm.free()
        o = self.link(bpy.data.objects.new("fin", me))
        bpy.context.view_layer.objects.active = o
        bm = bmesh.new(); bm.from_mesh(me)
        bmesh.ops.convex_hull(bm, input=bm.verts)
        bm.to_mesh(me); bm.free()
        o.data.materials.append(self.MATS[f.get("mat", "trim")])
        m = o.modifiers.new("bev", "BEVEL"); m.width = 0.006; m.segments = 3
        bpy.ops.object.modifier_apply(modifier="bev")
        for pl in o.data.polygons:
            pl.use_smooth = True

    def scoop(self, c):
        """Hood scoop: a tapered hump on the hood centre with a dark mouth."""
        p, n = self.on_skin(np.array([0.0005, c["y"], 1.5]))
        ln, w, h = c.get("len", 0.42), c.get("w", 0.5), c.get("h", 0.06)
        o = self.rounded("scoop", (w / 2, ln / 2, h / 2), Vector((0, c["y"], p[2] + h * 0.35)), "paint", 1,
                         taper=[("y", -1, 0.92, 0.25)], round_frac=0.5)
        o.rotation_euler = (math.radians(c.get("pitch", 4)), 0, 0)
        m = self.rounded("scoop_mouth", (w / 2 * 0.86, 0.01, h / 2 * 0.7), Vector((0, c["y"] + ln / 2 - 0.004, p[2] + h * 0.42)), "gap", 1, round_frac=0.4)
        m.rotation_euler = o.rotation_euler

    def box(self, name, lo, hi, mat, bevel=0.0):
        bm = bmesh.new()
        bmesh.ops.create_cube(bm, size=1.0)
        for v in bm.verts:
            v.co.x = lo[0] if v.co.x < 0 else hi[0]
            v.co.y = lo[1] if v.co.y < 0 else hi[1]
            v.co.z = lo[2] if v.co.z < 0 else hi[2]
        if bevel:
            bmesh.ops.bevel(bm, geom=list(bm.edges), offset=bevel, segments=3, profile=0.5, affect="EDGES")
        me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free()
        o = self.link(bpy.data.objects.new(name, me)); o.data.materials.append(self.MATS[mat])
        for p in o.data.polygons:
            p.use_smooth = bool(bevel)
        return o

    def bed(self, b):
        """Pickup bed: a painted tub (outer sides, front wall, tailgate) with
        black rail caps, liner floor, wheel tubs, cut wheel arches, vertical
        tail lamps, a bumper and a hitch; frame rails underneath."""
        y0, y1 = b["y_front"], b["y_rear"]          # y1 < y0
        xo, xi = b["half_w"], b["inner_half_w"]
        zb, zt, zf = b["z_bottom"], b["z_top"], b["z_floor"]
        outer = self.box("bed", (-xo, y1, zb), (xo, y0, zt), "paint", bevel=0.02)
        inner = self.box("bed_inner", (-xi, y1 + b.get("gate", 0.05), zf), (xi, y0 - 0.045, zt + 0.2), "paint")
        m = outer.modifiers.new("tub", "BOOLEAN"); m.operation = "DIFFERENCE"; m.object = inner; m.solver = "EXACT"
        bpy.context.view_layer.objects.active = outer; bpy.ops.object.modifier_apply(modifier="tub")
        bpy.data.objects.remove(inner)
        ya, ar, az = b["axle_y"], b["arch_r"], b["arch_z"]
        bpy.ops.mesh.primitive_cylinder_add(vertices=72, radius=ar, depth=0.5, location=(xo, ya, az), rotation=(0, math.pi / 2, 0))
        c1 = bpy.context.active_object
        bpy.ops.mesh.primitive_cylinder_add(vertices=72, radius=ar, depth=0.5, location=(-xo, ya, az), rotation=(0, math.pi / 2, 0))
        c2 = bpy.context.active_object
        for c in (c1, c2):
            m = outer.modifiers.new("arch", "BOOLEAN"); m.operation = "DIFFERENCE"; m.object = c; m.solver = "EXACT"
            bpy.context.view_layer.objects.active = outer; bpy.ops.object.modifier_apply(modifier="arch")
            bpy.data.objects.remove(c)
        # liner floor, wheel tubs, rail caps
        self.box("bed_floor", (-xi, y1 + 0.05, zf - 0.01), (xi, y0 - 0.045, zf + 0.005), "liner")
        for sx in (1, -1):
            lo, hi = sorted((sx * b["tub_x"], sx * xi))
            self.box("wheel_tub", (lo, ya - ar * 0.95, zf), (hi, ya + ar * 0.95, az + ar * 0.62), "liner", bevel=0.03)
            lo, hi = sorted((sx * (xi - 0.012), sx * (xo + 0.004)))
            self.box("rail_cap", (lo, y1 + 0.01, zt - 0.004), (hi, y0 - 0.01, zt + 0.018), "satin", bevel=0.006)
            # arch liner inside the bed side
            from math import radians
            n = 40
            bm = bmesh.new(); ring_o, ring_i = [], []
            for k in range(n + 1):
                a = radians(-10) + radians(200) * k / n
                ring_o.append(bm.verts.new((sx * xo, ya + ar * 0.99 * math.cos(a), az + ar * 0.99 * math.sin(a))))
                ring_i.append(bm.verts.new((sx * b["tub_x"], ya + ar * 0.99 * math.cos(a), az + ar * 0.99 * math.sin(a))))
            for k in range(n):
                bm.faces.new((ring_o[k], ring_o[k + 1], ring_i[k + 1], ring_i[k]))
            me = bpy.data.meshes.new("bed_liner"); bm.to_mesh(me); bm.free()
            o = self.link(bpy.data.objects.new("bed_liner", me)); o.data.materials.append(self.MATS["liner"])
            # vertical tail lamp on the rear face of each side
            lamp = self.MATS.get("lamp_rear")
            if lamp is None:
                lamp = bpy.data.materials.new("lamp_rear"); lamp.use_nodes = True
                pb = lamp.node_tree.nodes["Principled BSDF"]
                pb.inputs["Base Color"].default_value = (0.32, 0.01, 0.015, 1); pb.inputs["Roughness"].default_value = 0.12
                pb.inputs["Coat Weight"].default_value = 1.0
                lamp.diffuse_color = (0.5, 0.02, 0.03, 1)
                self.MATS["lamp_rear"] = lamp
            lw = b.get("lamp_w", 0.085)
            lo, hi = sorted((sx * (xo - lw - 0.01), sx * (xo - 0.01)))
            lb = self.box("bed_lamp", (lo, y1 - 0.012, b["lamp_z"][0]), (hi, y1 + 0.01, b["lamp_z"][1]), "trim", bevel=0.008)
            lb.data.materials[0] = lamp
        # tailgate handle, bumper, hitch, frame rails
        self.box("gate_handle", (-0.14, y1 - 0.012, zt - 0.12), (0.14, y1 + 0.002, zt - 0.07), "satin", bevel=0.008)
        self.box("bumper_rear", (-xo + 0.04, y1 - 0.09, b["bumper_z"][0]), (xo - 0.04, y1 + 0.05, b["bumper_z"][1]), b.get("bumper_mat", "chrome"), bevel=0.02)
        self.box("bumper_step", (-xo + 0.08, y1 - 0.10, b["bumper_z"][1] - 0.01), (xo - 0.08, y1 - 0.02, b["bumper_z"][1] + 0.004), "satin")
        self.box("hitch", (-0.04, y1 - 0.16, b["bumper_z"][0] + 0.02), (0.04, y1 - 0.05, b["bumper_z"][0] + 0.09), "satin")
        for sx in (1, -1):
            lo, hi = sorted((sx * 0.42, sx * 0.55))
            self.box("frame_rail", (lo, y1 + 0.1, zb - 0.22), (hi, b["frame_front"], zb - 0.02), "under")
        self.box("spare", (-0.4, y1 + 0.15, zb - 0.32), (0.4, y1 + 0.8, zb - 0.06), "rubber", bevel=0.05)

    def wipers(self, w):
        for (x0, x1, y, z) in w["blades"]:
            ln = abs(x1 - x0)
            o = self.rounded("wiper", (ln / 2, 0.012, 0.01), Vector(((x0 + x1) / 2, y, z)), "satin", 1)
            o.rotation_euler = (math.radians(-28), 0, math.radians(w.get("angle", 4)))

    def build(self):
        D = self.D
        if D.get("mirror"):
            self.mirror(D["mirror"])
        if D.get("handles"):
            self.handles(D["handles"])
        if D.get("wing"):
            self.wing(D["wing"])
        if D.get("exhaust"):
            self.exhaust(D["exhaust"])
        if D.get("fin"):
            self.fin(D["fin"])
        if D.get("wipers"):
            self.wipers(D["wipers"])
        if D.get("scoop"):
            self.scoop(D["scoop"])
        if D.get("bed"):
            self.bed(D["bed"])
        for b in D.get("boxes", []):
            self.box(b["name"], b["lo"], b["hi"], b["mat"], b.get("bevel", 0.0))
