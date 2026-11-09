from lanes import *
from scipy.optimize import least_squares
from scipy.ndimage import distance_transform_edt
from shapely import affinity
from scipy.spatial import cKDTree
from scipy.spatial.distance import cdist
from PIL import Image, ImageDraw

labels = {"key": "Key-Largo", "cambridge": "Cambridge"}
# Two outer images only. Clicked on recognizable near dash ends and on the right stripe; independently traced center held out.
# Coordinates are measured on a 820x615 resample of original CC BY-SA imagery.
ANNOT = {
    "key": [
        {
            "dash": [(186, 554), (241, 508)],
            "edge": [(471, 481), (585, 548)],
            "vanish": (354, 424),
        },
        {
            "dash": [(243, 502), (270, 482)],
            "edge": [(458, 477), (563, 541)],
            "symbols": [(164, 494), (365, 485)],
            "vanish": (354, 423),
        },
        {
            "dash": [(229, 506), (259, 486)],
            "edge": [(453, 476), (549, 539)],
            "vanish": (356, 427),
        },
    ],
    "cambridge": [
        {
            "dash": [(75, 613), (231, 511)],
            "edge": [(446, 494), (564, 566)],
            "vanish": (314, 444),
        },
        {
            "dash": [(201, 520), (265, 482)],
            "edge": [(432, 486), (576, 578)],
            "vanish": (313, 446),
        },
        {
            "dash": [(246, 499), (285, 475)],
            "edge": [(441, 482), (560, 549)],
            "vanish": (320, 444),
        },
    ],
}


def load_site(site):
    feats = json.loads((Path(site) / "accepted.json").read_text())["features"]
    frames = json.loads((Path(site) / "frames.json").read_text())
    angle = np.deg2rad(SITES[site][-1])
    a = np.array([np.sin(angle), np.cos(angle)])
    r = np.array([np.cos(angle), -np.sin(angle)])
    lon, lat = SITES[site][2:4]
    o = np.array(WGS.transform(lon, lat))
    scale = np.cos(np.deg2rad(lat))

    def en(ll):
        return (np.stack(WGS.transform(*np.array(ll).T), 1) - o) * scale

    poses = en([(f["lon"], f["lat"]) for f in frames])
    geoms = []
    for f in feats:
        poly = Polygon(en(f["polygon"]))
        gg = np.array(poly.exterior.coords)
        proj = gg @ a
        cen = np.array(poly.centroid.coords)[0]
        far = gg[proj >= proj.max() - 0.08].mean(0)
        near = gg[proj <= proj.min() + 0.08].mean(0)
        geoms.append(dict(kind=f["kind"], poly=gg, cen=cen, ends=np.stack([near, far])))
    return geoms, poses, a, r


def project(geom, pose, a, r, param, nonfinite=False):
    yaw, pitch, hf, side, along, height = param
    ey = pose + side * r + along * a
    ang = np.arctan2(a[0], a[1]) + np.deg2rad(yaw)
    head = np.array([np.sin(ang), np.cos(ang)])
    right = np.array([np.cos(ang), -np.sin(ang)])
    dx = geom - ey
    front = dx @ head
    across = dx @ right
    z = -height
    alpha = np.deg2rad(pitch)
    dep = front * np.cos(alpha) - z * np.sin(alpha)
    vert = z * np.cos(alpha) + front * np.sin(alpha)
    f = 410 / np.tan(np.deg2rad(hf / 2))
    pix = np.stack(
        [
            410 + f * across / np.maximum(0.1, dep),
            307.5 - f * vert / np.maximum(0.1, dep),
        ],
        1,
    )
    pix[dep < 1] = np.nan
    return pix
