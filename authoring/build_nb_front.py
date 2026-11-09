"""Replace the NB1's protruding generic front light ellipsoids and frontal cap.
The photographed clear lenses are adaptations of Elise240SX's 1999 Miata image (CC BY-SA 4.0).
The surrounding sculpted geometry is original. All units are metres, +Y upward, -Z forward.
"""

from pathlib import Path
import argparse, json, struct, io, hashlib
import numpy as np
from PIL import Image
from scipy.ndimage import map_coordinates
from scipy.sparse import coo_matrix
from scipy.sparse.csgraph import connected_components
from scipy.interpolate import PchipInterpolator

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("original", type=Path, help="1.0.3 game/art/miata.glb")
parser.add_argument(
    "photo", type=Path, help="Original 3486 x 2214 Elise240SX photograph"
)
parser.add_argument("output", type=Path, help="Destination for corrected GLB")
args = parser.parse_args()
if args.original.resolve() == args.output.resolve():
    parser.error("Use a separate output to preserve the original 1.0.3 input.")
raw = args.original.read_bytes()
if (
    hashlib.sha256(raw).hexdigest()
    != "aef60de47e3f1bd1c2b88261169caf6f42177995447048287e6c7f8650aa69ae"
):
    parser.error("Input must be the unmodified 1.0.3 exterior.")
if (
    hashlib.sha256(args.photo.read_bytes()).hexdigest()
    != "2a213da1394a132512f229cee73b0b674aba501ae5877e0e663aab9a0bf92280"
):
    parser.error("Use the full original photograph linked in the vehicle receipt.")
n = struct.unpack_from("<I", raw, 12)[0]
g = json.loads(raw[20 : 20 + n])
buf = bytearray(raw[20 + n + 8 :])
types = {5121: "u1", 5123: "<u2", 5125: "<u4", 5126: "<f4"}


def load(i):
    a = g["accessors"][i]
    b = g["bufferViews"][a["bufferView"]]
    k = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}[a["type"]]
    return np.frombuffer(
        bytes(buf),
        types[a["componentType"]],
        count=a["count"] * k,
        offset=b.get("byteOffset", 0) + a.get("byteOffset", 0),
    ).reshape(-1, k)


def view(data, target=None):
    buf.extend(b"\0" * (-len(buf) % 4))
    offset = len(buf)
    buf.extend(data)
    b = {"buffer": 0, "byteOffset": offset, "byteLength": len(data)}
    if target:
        b["target"] = target
    g["bufferViews"].append(b)
    return len(g["bufferViews"]) - 1


def store(data, component, kind):
    a = np.array(data, dtype=types[component])
    ii = view(a.tobytes(), 34963 if kind == "SCALAR" else 34962)
    record = {
        "bufferView": ii,
        "componentType": component,
        "type": kind,
        "count": len(a),
    }
    if kind == "VEC3":
        record.update(min=a.min(axis=0).tolist(), max=a.max(axis=0).tolist())
    g["accessors"].append(record)
    return len(g["accessors"]) - 1


def addmaterial(name, srgb, metal, rough, tex=None):
    color = [
        (i / 255 / 12.92 if i / 255 < 0.04045 else ((i / 255 + 0.055) / 1.055) ** 2.4)
        for i in srgb
    ]
    p = {
        "baseColorFactor": color + [1.0],
        "metallicFactor": metal,
        "roughnessFactor": rough,
    }
    if tex is not None:
        p["baseColorTexture"] = {"index": tex}
    g["materials"].append(
        {"name": name, "doubleSided": True, "pbrMetallicRoughness": p}
    )
    return len(g["materials"]) - 1


def primitive(name, V, F, uv, mat, aim):
    v = np.array(V, float)
    f = np.array(F, int)
    cross = np.cross(v[f[:, 1]] - v[f[:, 0]], v[f[:, 2]] - v[f[:, 0]])
    t = np.dot(cross, aim)
    if np.sum(t) < 0:
        f = f[:, [0, 2, 1]]
        cross *= -1
    norm = np.zeros_like(v)
    for k in range(3):
        np.add.at(norm, f[:, k], cross)
    norm /= np.maximum(1e-12, np.linalg.norm(norm, axis=1))[:, None]
    attrs = {"POSITION": store(v, 5126, "VEC3"), "NORMAL": store(norm, 5126, "VEC3")}
    if uv is not None:
        attrs["TEXCOORD_0"] = store(uv, 5126, "VEC2")
    geo = {
        "name": name,
        "primitives": [
            {
                "attributes": attrs,
                "indices": store(f.ravel(), 5123, "SCALAR"),
                "material": mat,
                "mode": 4,
            }
        ],
    }
    g["meshes"].append(geo)
    g["nodes"].append({"name": name, "mesh": len(g["meshes"]) - 1})
    g["scenes"][g.get("scene", 0)]["nodes"].append(len(g["nodes"]) - 1)


def grid(name, us, vs, fn, mat, aim, texture=False):
    uv = [(u, v) for v in vs for u in us]
    V = [fn(u, v) for u, v in uv]
    F = []
    for j in range(len(vs) - 1):
        for i in range(len(us) - 1):
            a = j * len(us) + i
            b = a + 1
            c = b + len(us)
            d = c - 1
            F.extend([[a, b, c], [a, c, d]])
    primitive(name, V, F, uv if texture else None, mat, aim)


# Remove only the original disconnected front bumper and projecting grille shape.
# Surface/mesh identity guards protect the rear, the dashboard and all wheel and lamp pivots.
for i, node in enumerate(g["nodes"]):
    name = node["name"]
    z = g["meshes"][node["mesh"]]["primitives"][0]
    if name in ("body_lamp", "body_amber"):
        g["scenes"][g.get("scene", 0)]["nodes"].remove(i)
        continue
    if name not in ("body_British racing green", "body_trim"):
        continue
    f = load(z["indices"]).reshape(-1, 3)
    v = load(z["attributes"]["POSITION"])
    adj = coo_matrix(
        (np.ones(f.size), (f.ravel(), f[:, [1, 2, 0]].ravel())), shape=(len(v), len(v))
    )
    count, group = connected_components(adj, directed=False)
    ids = []
    for j in range(count):
        xyz = v[group == j]
        if (
            xyz[:, 2].max() < (-1.64 if name.startswith("body_British") else -1.91)
            and xyz[:, 2].min() < -1.94
            and np.ptp(xyz[:, 0]) > 0.70
        ):
            ids.append(j)
    if name.startswith("body_British"):
        hull = v[f[~np.isin(group[f[:, 0]], ids)]]
        hull = hull[(np.min(hull[:, :, 2], axis=1) < -1.2)]
    assert len(ids) == 1, (name, ids)
    z["indices"] = store(f[~np.isin(group[f[:, 0]], ids)].ravel(), 5123, "SCALAR")
paint = addmaterial("Emerald Green Mica bumper", (24, 76, 56), 0.12, 0.39)
inner = addmaterial("Recessed intake", (19, 23, 23), 0.08, 0.69)
black = addmaterial("Headlamp seal", (27, 27, 26), 0, 0.71)
amber = addmaterial("Amber marker lens", (183, 85, 20), 0.06, 0.34)
slat = addmaterial("Radiator fins", (53, 57, 55), 0.48, 0.50)


# A smooth oval-sided intake terminates in a real 8cm pocket. Rounded bumper shoulders
# fall around the opening. The original bonnet and fenders meet the same 0.77m half-width.
def front(x, y):
    u = min(1.0, abs(x) / 0.771)
    return (
        -1.945
        + 0.240 * u**2.9
        - 0.048 * np.exp(-(((y - 0.393) / 0.15) ** 2))
        + 0.029 * np.exp(-(((y - 0.174) / 0.05) ** 2))
    )


def captop(x):
    return 0.560 - 0.024 * (abs(x) / 0.771) ** 3


mouth_w = 0.458


def bounds(x):
    s = min(1.0, abs(x) / mouth_w)
    a = max(0.0, 1 - s**4.1) ** 0.52
    mid = 0.316 + 0.013 * s * s
    return mid - 0.070 * a, mid + 0.079 * a


xx = np.r_[
    np.linspace(-0.772, -mouth_w, 22)[:-1],
    np.linspace(-mouth_w, mouth_w, 69),
    np.linspace(mouth_w, 0.772, 22)[1:],
]


def bumper(x, v, upper):
    lo, hi = bounds(x)
    y = (
        (hi * (1 - v) + captop(x) * v)
        if upper
        else (0.176 + (abs(x) / 0.772) ** 3 * 0.028) * (1 - v) + lo * v
    )
    # The rolled-in edge falls gently into the intake, with maximum inset at the mouth.
    lip = (
        0.014
        * np.exp(-(((y - (hi if upper else lo)) / 0.014) ** 2))
        * max(0.0, 1 - (abs(x) / mouth_w) ** 6)
    )
    return [x, y, front(x, y) + lip]


for upper in [False, True]:
    grid(
        "Upper bumper" if upper else "Lower bumper",
        xx,
        np.linspace(0, 1, 18),
        lambda x, v: bumper(x, v, upper),
        paint,
        [0, 0, -1],
    )


def oval(t, depth):
    s = np.sin(t)
    c = np.cos(t)
    x = mouth_w * np.sign(s) * abs(s) ** (0.5)
    lo, hi = bounds(x)
    y = hi if c > 0 else lo
    return [
        x * (1 - 0.07 * depth),
        y + (0.317 - y) * 0.17 * depth,
        front(x, y) + 0.009 + 0.080 * depth,
    ]


grid(
    "Rolled intake throat",
    np.linspace(0, 2 * np.pi, 121),
    np.linspace(0, 1, 7),
    oval,
    inner,
    [0, 0, -1],
)


# Rear plane and recessed radiator rows. The center of the grille remains visibly deep.
def back(x, v):
    lo, hi = bounds(x)
    y = lo + (hi - lo) * v
    return [x * 0.95, y, front(x, y) + 0.092]


grid(
    "Radiator core",
    np.linspace(-mouth_w, mouth_w, 70),
    [0.0, 1.0],
    back,
    inner,
    [0, 0, -1],
)
for i, y in enumerate(np.linspace(0.268, 0.372, 6)):
    grid(
        "Cooling fin " + str(i),
        np.linspace(-0.420, 0.420, 60),
        [-0.0009, 0.0009],
        lambda x, dy: [x, y + dy, front(x, y) + 0.081],
        slat,
        [0, 0, -1],
    )
# Close the bumper returns around the outer corners to the front-wheel opening.
for sign in [-1, 1]:

    def shoulder(u, v):
        x = 0.772 + 0.027 * u + 0.004 * np.sin(np.pi * u)
        bot = 0.203
        top = 0.535 + 0.096 * u
        y = bot + (top - bot) * v
        z = (1 - u) * front(0.772, y) + u * (-1.465)
        return [sign * x, y, z]

    grid(
        "Bumper return " + str(sign),
        np.linspace(0, 1, 16),
        np.linspace(0, 1, 18),
        shoulder,
        paint,
        [sign, 0, 0],
    )
# Normal production wheelhouses are opaque above and behind each wheel. Preserve
# the 57-mm geometric jounce gap and the stock tire size and suspension travel.
for sign in [-1, 1]:
    for axle in [-1.13, 1.13]:

        def lining(u, v):
            radius = 0.349 - 0.015 * v
            return [
                sign * (0.816 - 0.235 * v),
                0.305 + radius * np.sin(u),
                axle + radius * np.cos(u),
            ]

        grid(
            "Wheelhouse lining " + str((sign, axle)),
            np.linspace(-0.39, np.pi + 0.39, 54),
            [0.0, 0.38, 0.75, 1.0],
            lining,
            inner,
            [0, -1, 0],
        )

        def tub(u, v):
            x, y, z = lining(u, 1.0)
            return [x, 0.160 + (y - 0.160) * v, z]

        grid(
            "Wheelhouse back " + str((sign, axle)),
            np.linspace(-0.39, np.pi + 0.39, 54),
            [0.0, 1.0],
            tub,
            inner,
            [sign, 0, 0],
        )


def hull_depth(x, y):
    # Intersect a longitudinal ray with the unchanged physical hood/fender shell.
    # A clear lens belongs immediately outside this surface, including at its crown.
    a, b, c = np.moveaxis(hull, 1, 0)
    e = b - a
    f = c - a
    den = e[:, 0] * f[:, 1] - e[:, 1] * f[:, 0]
    valid = abs(den) > 1e-9
    ud = np.zeros(len(a))
    vd = ud.copy()
    xx = x - a[:, 0]
    yy = y - a[:, 1]
    ud[valid] = (xx[valid] * f[valid, 1] - yy[valid] * f[valid, 0]) / den[valid]
    vd[valid] = (e[valid, 0] * yy[valid] - e[valid, 1] * xx[valid]) / den[valid]
    valid &= (ud >= -0.00001) & (vd >= -0.00001) & (ud + vd <= 1.00001)
    if not valid.any():
        return 2.0
    return np.min((a[:, 2] + ud * e[:, 2] + vd * f[:, 2])[valid])


# Dark seals and two flush, compound-curved headlight lenses. The outer, rounder
# ends sweep almost 0.30m around the corner. The amber reflector is inboard.
img = Image.open(args.photo).convert("RGB")
pic = np.asarray(img)
scale = img.width / 850.0
# Measured silhouette in the 850-pixel review copy. Bounds are inside the lens seal.
u_p = np.array([0, 0.04, 0.12, 0.24, 0.40, 0.55, 0.70, 0.82, 0.91, 0.97, 1.0])
sx = np.array([563, 561, 553, 537, 512, 487, 463, 446, 432, 424, 420])
syhigh = np.array([306, 301, 297, 293, 286, 283, 281, 280, 282, 286, 297])
sylow = np.array([309, 314, 320, 323, 326, 327, 326, 324, 321, 316, 307])
H, W = 102, 384
u, v = np.meshgrid((np.arange(W) + 0.5) / W, (np.arange(H) + 0.5) / H)
X = np.interp(u, u_p, sx)
Y = np.interp(u, u_p, syhigh) * (1 - v) + np.interp(u, u_p, sylow) * v
rgb = np.stack(
    [
        map_coordinates(pic[:, :, c], np.array([Y * scale, X * scale]), order=1)
        for c in range(3)
    ],
    axis=-1,
).astype(float)
# Remove the photographed lens's overall yellow cast from clear areas while
# preserving the observed amber indicator at the inboard end.
l = rgb @ [0.2126, 0.7152, 0.0722]
keep = ((rgb[:, :, 0] - rgb[:, :, 2]) > 35) & (u < 0.285)
rgb = np.where(keep[:, :, None], rgb, 0.35 * rgb + 0.65 * l[:, :, None])
a = Image.fromarray(np.uint8(np.clip(rgb * 0.91, 0, 255)))
q = io.BytesIO()
a.save(q, "PNG")
g.setdefault("images", []).append(
    {
        "bufferView": view(q.getvalue()),
        "mimeType": "image/png",
        "name": "1999 NB1 lens detail by Elise240SX",
    }
)
g.setdefault("textures", []).append({"source": len(g["images"]) - 1})
tex = len(g["textures"]) - 1
lensmat = addmaterial("Lens fluting and reflectors", (255, 255, 255), 0.07, 0.31, tex)
us = np.array([0, 0.03, 0.09, 0.21, 0.40, 0.60, 0.76, 0.89, 0.96, 1.0])
h = PchipInterpolator(
    us, [0.513, 0.535, 0.550, 0.563, 0.579, 0.595, 0.606, 0.612, 0.607, 0.592]
)
lo = PchipInterpolator(
    us, [0.513, 0.489, 0.477, 0.472, 0.474, 0.482, 0.496, 0.521, 0.552, 0.592]
)
xs = PchipInterpolator(
    us, [0.328, 0.339, 0.369, 0.425, 0.515, 0.606, 0.670, 0.716, 0.744, 0.765]
)


def light(side, u, v, seal=False):
    x = float(xs(u))
    y = float(h(u) * (1 - v) + lo(u) * v)
    if seal:
        x = 0.551 + (x - 0.551) * 1.035
        y = 0.54 + (y - 0.54) * 1.105
    # Cross the nominal upper seam, such that each lens normal faces sky and oncoming traffic.
    z = (
        front(x, y)
        - 0.004
        + (1 - v) * (0.010 + 0.069 * u**1.5)
        - 0.053 * np.sin(np.pi * u) * np.sin(np.pi * v)
        + 0.050 * u**2.5
    )
    # Upper part lies on the swept bonnet, lower part on the bumper. A narrow
    # outward displacement follows the original shell; the edges never dive under it.
    if y < captop(x):
        z = min(z, front(x, y) - 0.009)
    z = min(z, hull_depth(x, y) - 0.014)
    return [side * x, y, z + (0.001 if seal else -0.004)]


for side in [-1, 1]:
    uv = np.linspace(0, 1, 35)
    for v in [0.0, 1.0]:
        grid(
            "Headlamp seal " + str((side, v)),
            uv,
            [0.0, 1.0],
            lambda u, f: (
                np.asarray(light(side, u, v)) * (1 - f)
                + np.asarray(light(side, u, v, True)) * f
            ),
            black,
            [side * 0.36, 0.28, -1],
        )
    grid(
        "Headlamp " + str(side),
        uv,
        np.linspace(0, 1, 12),
        lambda u, v: light(side, u, v),
        lensmat,
        [side * 0.36, 0.28, -1],
        True,
    )

    # Small amber marker follows the bumper side immediately ahead of the wheel opening.
    def marker(u, v):
        rad = 0.010 * np.sin(v)
        z = -1.625 + 0.068 * np.cos(u) * np.cos(v)
        return [side * (0.786 + 0.009 * np.sin(u) * np.cos(v)), 0.394 + rad, z]

    grid(
        "Bumper side marker " + str(side),
        np.linspace(0, 2 * np.pi, 25),
        np.linspace(-np.pi / 2, np.pi / 2, 10),
        marker,
        amber,
        [side, 0, 0],
    )
# New surfaces are appended after the original protected chassis/lamps and wheels.
g["buffers"][0]["byteLength"] = len(buf)
meta = json.dumps(g, separators=(",", ":")).encode()
meta += b" " * (-len(meta) % 4)
buf.extend(b"\0" * (-len(buf) % 4))
dest = args.output
dest.write_bytes(
    struct.pack("<4sIII4s", b"glTF", 2, 28 + len(meta) + len(buf), len(meta), b"JSON")
    + meta
    + struct.pack("<I4s", len(buf), b"BIN\0")
    + buf
)
print(dest.stat().st_size)
