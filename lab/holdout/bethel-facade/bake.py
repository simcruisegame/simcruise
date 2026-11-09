"""Project the licensed source onto independently located wall planes; exclude occluders and in-glass images."""

import json
import numpy as np
from geometry import base, d, ground, out, plan, projection, r, row, side, st
from PIL import Image, ImageDraw, ImageFilter
from scipy.ndimage import gaussian_filter, map_coordinates, distance_transform_edt
from hashlib import sha256

outdir = r / "assets"
outdir.mkdir(exist_ok=True)
im = Image.open(r / "davidson-original.jpg").convert("RGB")
width, height = im.size
# All manual masks use the original photograph's 640-wide rectilinear coordinate system.
small_scale = width / 640


def surface(poly, fill=1):
    mask = Image.new("L", im.size, 0)
    ImageDraw.Draw(mask).polygon([(x * small_scale, y * small_scale) for x, y in poly], fill=fill)
    return np.asarray(mask) > 0


labels = Image.open(r / "labels.png")
lab = np.asarray(labels.resize(im.size, Image.Resampling.NEAREST))
classes = json.loads((r / "label-ids.json").read_text())
valid = np.isin(lab, [classes.index(k) for k in ["building", "wall", "windowpane", "door"]])
# Exclude the protruding entrance canopy, lighting, structural landscaping, parked hardware,
# bench and signboard. Tree, grass, sky and foreground were excluded above.
exclude = [
    [
        (0, 0),
        (215, 0),
        (212, 135),
        (193, 155),
        (183, 192),
        (161, 221),
        (147, 227),
        (147, 213),
        (137, 197),
        (124, 188),
        (107, 186),
        (102, 179),
        (88, 191),
        (56, 199),
        (26, 215),
        (14, 239),
        (0, 254),
    ],
    [
        (334, 214),
        (394, 207),
        (400, 189),
        (387, 185),
        (388, 178),
        (407, 171),
        (424, 179),
        (421, 190),
        (416, 196),
        (416, 211),
        (435, 214),
        (440, 235),
        (433, 241),
        (428, 253),
        (417, 252),
        (426, 370),
        (406, 378),
        (401, 258),
        (398, 239),
        (337, 238),
    ],
    [
        (239, 310),
        (250, 280),
        (286, 282),
        (291, 258),
        (319, 237),
        (342, 230),
        (371, 238),
        (376, 226),
        (400, 230),
        (416, 238),
        (434, 243),
        (448, 268),
        (439, 281),
        (449, 289),
        (438, 315),
        (235, 355),
    ],
    [(456, 279), (467, 269), (489, 273), (493, 292), (454, 298)],
    [(481, 237), (505, 233), (509, 282), (486, 290)],
    [(505, 298), (513, 278), (567, 274), (585, 282), (586, 300)],
    [(604, 275), (593, 249), (602, 216), (611, 191), (632, 175), (641, 169), (642, 310)],
    [(494, 215), (506, 212), (511, 219), (509, 231), (514, 304), (505, 306), (501, 231)],
]
for polygon in exclude:
    valid &= ~surface(polygon)
# Neutralize the contents of physically observed window openings. Narrow mullions/sills
# survive from the photo; actual reflected sky and tree colors do not travel with the wall.
# Apertures with a sufficiently visible surround were traced at native resolution.
# The crown/lamp hide the remaining apertures. Leave no texture fragments for them.
windows = [
    (46, 204, 52, 231),
    (68, 195, 75, 226),
    (92, 191, 101, 221),
    (20.7, 247, 27, 286),
    (45.4, 242, 53, 284),
    (67.5, 238, 76, 284),
    (91.5, 233.4, 101, 281.3),
    (133, 229, 142.6, 279),
    (20.5, 296.3, 27.4, 327.4),
    (45.2, 295.5, 53.7, 329.8),
    (67.3, 294.9, 76, 331.3),
    (91, 294.1, 101.2, 332.8),
    (131.6, 292.4, 143.2, 337),
    (249.5, 170, 268, 215.8),
    (302.5, 175.4, 317, 219),
    (348.5, 181, 360.5, 214),
    (250.1, 228.7, 268.3, 265.8),
    (451.5, 178.8, 469.4, 221.2),
    (453.4, 229.3, 470.1, 270.7),
    (562, 177, 573, 213.5),
    (589.2, 186, 598.1, 217),
    (563.8, 232, 574, 266.8),
    (591, 233.5, 599, 267),
]
glass = np.zeros_like(valid)
architectural = np.zeros_like(valid)
for x0, y0, x1, y1 in windows:
    outer = surface([(x0 - 1, y0 - 1), (x1 + 0.6, y0), (x1 + 1.4, y1 + 0.4), (x0 - 0.4, y1 + 0.5)])
    architectural |= outer
    # Interior glazing and integral white frames are neutralized later. Retain the
    # source luminance of the frame, discard the chroma of glass and reflected objects.
    glass |= surface(
        [(x0 + 0.5, y0 + 0.5), (x1 - 0.4, y0 + 0.5), (x1 + 0.3, y1 - 0.1), (x0 + 0.8, y1 - 0.3)]
    )
# A few source pixels near a silhouette can contain both the wall and the foreground.
valid = ~(
    np.asarray(Image.fromarray((~valid).astype("uint8") * 255).filter(ImageFilter.MaxFilter(9))) > 0
)
pic = np.asarray(im) / 255
brick = (
    (pic[:, :, 0] > 0.07)
    & (pic[:, :, 0] > pic[:, :, 1] * 1.18)
    & (pic[:, :, 1] > pic[:, :, 2] * 1.04)
    & valid
    & ~glass
)
# Per-face illumination recovery may only use valid masonry. Homogenize the low-frequency
# light field from each wall while retaining variation at the scale of mortar and lintels.
faces = [(0, 7), (7, 6), (6, 5), (5, 4)]
face_colors = np.array(
    [0.53, 0.294, 0.224]
)  # diffuse brick from the exposed eastern face, recomputed below
patch = surface([(472, 200), (526, 195), (528, 230), (474, 235)]) & brick
if patch.sum() > 200:
    face_colors = np.median(pic[patch], axis=0) * 0.80
top = st[5]
bottom = ground((np.array(row[4:6]) - base % 1) * np.array([side, -side])) - 2.0
photo_source = json.loads((r / "sources.json").read_text())["photograph"]
meta = {
    "photograph": {
        key: photo_source[key]
        for key in ("title", "author", "capture_utc", "page", "license", "license_url")
    },
    "adaptations": "Occlusion masking, wall-plane rectification, illumination correction, neutralized glazing and a masonry sample.",
    "source": "Gould Academy Davidson Hall",
    "tile": d["id"],
    "center": row[4:6],
    "ring": row[0][0],
    "index": 2,
    "eave_above_center": out["eave_center_m"],
    "bottom_above_center": -2.0,
    "faces": [],
    "image_sha256": sha256((r / "davidson-original.jpg").read_bytes()).hexdigest(),
}
review = im.copy()
draw = ImageDraw.Draw(review)
summary = []
for a, b in faces:
    xaxis = plan[b] - plan[a]
    length = float(np.linalg.norm(xaxis))
    W = int(np.ceil(length * 48))
    H = int(np.ceil((top - bottom) * 48))
    s = (np.arange(W) + 0.5) / W
    t = (np.arange(H) + 0.5) / H
    uv1, uv2 = np.meshgrid(s, t)
    xyz = np.zeros((H, W, 3))
    xyz[:, :, :2] = plan[a] + uv1[:, :, None] * xaxis
    xyz[:, :, 2] = top - uv2 * (top - bottom)
    px = projection(xyz.reshape(-1, 3), st).reshape(H, W, 2) * small_scale
    xy = np.array([px[:, :, 1], px[:, :, 0]])
    colors = np.stack(
        [map_coordinates(pic[:, :, k], xy, order=1, mode="constant") for k in range(3)], axis=-1
    )
    allowed = map_coordinates(valid.astype("uint8"), xy, order=0, mode="constant") > 0
    masonry = map_coordinates(brick.astype("uint8"), xy, order=0, mode="constant") > 0
    neutral = map_coordinates(glass.astype("uint8"), xy, order=0, mode="constant") > 0
    denom = gaussian_filter(masonry.astype(float), 21)
    illum = np.zeros_like(colors)
    median = np.median(colors[masonry], axis=0)
    for i in range(3):
        weighted = gaussian_filter(colors[:, :, i] * masonry, 21) / np.maximum(denom, 0.00001)
        illum[:, :, i] = np.where(denom > 0.04, weighted, median[i])
    ratio = np.clip(colors / np.maximum(0.05, illum), 0.34, 2.0)
    corrected = np.clip(ratio * face_colors, 0, 1)
    # Preserve photographed white trim and darker visible openings. Neutral window
    # interiors use their measured luminance, with the cross-frame reflection removed.
    lumin = np.sum(colors * np.array([0.2126, 0.7152, 0.0722]), axis=-1)
    trim = (np.max(colors, axis=-1) - np.min(colors, axis=-1) < 0.12) & (lumin > 0.38)
    corrected[trim] = np.clip(
        colors[trim] / np.maximum(0.58, np.median(illum[masonry], axis=0).mean() / 0.48), 0.12, 0.78
    )
    # Separate masonry reflectance from the photographed cast shadows. The dry-lay texture
    # comes from the high-frequency residual of each wall's own photograph. In the
    # observed window/trim zones retain stronger local structure after illumination correction.
    architectural_uv = (
        map_coordinates(architectural.astype("uint8"), xy, order=0, mode="constant") > 0
    )
    measured_mortar = lumin - np.exp(gaussian_filter(np.log(np.maximum(lumin, 0.006)), 1.9))
    intrinsic = np.clip(
        face_colors[None, None, :] + np.clip(measured_mortar, -0.13, 0.13)[:, :, None] * 0.95,
        0.0,
        0.95,
    )
    interior_weight = gaussian_filter(architectural_uv.astype(float), 0.7)[:, :, None]
    corrected = intrinsic * (1 - interior_weight) + corrected * interior_weight
    # The low-chroma roof-edge trim is visible; transparent or saturated blue
    # silhouette pixels cannot color that edge. The left edge is hidden by trees.
    if a != 0:
        trim_strip = (
            (uv2 < 0.039) & (np.min(colors, axis=-1) > 0.4) & (np.ptp(colors, axis=-1) < 0.20)
        )
        corrected[trim_strip] = [0.77, 0.755, 0.715]
    # Recover white frames and sills from measured local contrast against the adjacent
    # brick. The neutral opening tint removes source-window reflections. Bright feature
    # support still comes only from photographed pixels inside the traced opening.
    rel = lumin / np.maximum(0.046, np.sum(illum * np.array([0.2126, 0.7152, 0.0722]), axis=-1))
    strength = np.clip((rel - 0.67) / 0.66, 0, 1)
    glazing = np.array([0.205, 0.222, 0.232]) + strength[:, :, None] * np.array(
        [0.59, 0.578, 0.552]
    )
    corrected[neutral] = glazing[neutral]
    if (a, b) == (0, 7):
        orig = px / small_scale
        band_top = 286.3 - (orig[:, :, 0] - 14.0) * 0.062
        band_bottom = 296.0 - (orig[:, :, 0] - 14.0) * 0.042
        belt = (orig[:, :, 1] > band_top) & (orig[:, :, 1] < band_bottom) & ~architectural_uv
        corrected[belt] *= [1.1, 0.93, 0.89]

    # Limit the texture to observed pixels. The renderer uses plain sampled brick for occlusions.
    alpha = np.uint8(np.clip(distance_transform_edt(allowed) / 3.0, 0, 1) * 255)
    if (a, b) == (6, 5):
        square_side = 48
        index = np.unravel_index(
            np.argmin(((px / small_scale - [524, 219]) ** 2).sum(axis=-1)), (H, W)
        )
        ii, jj = index
        chip = np.uint8(
            (0.68 * face_colors + 0.32 * intrinsic[ii - 24 : ii + 24, jj - 24 : jj + 24]) * 255
        )
        assert chip.shape == (48, 48, 3)
        # The unobserved fragments have a low-contrast 1m sample of actual brick
        # from the same wall. No openings, obscured objects or architectural features
        # are extrapolated. Reflect at the tile edges to remove a wrap discontinuity.
        chip = np.concatenate([chip, chip[:, ::-1]], axis=1)
        chip = np.concatenate([chip, chip[::-1]], axis=0)
        Image.fromarray(chip).save(outdir / "davidson-masonry.png")
    rgba = np.dstack([np.uint8(corrected * 255), alpha])
    name = "davidson-" + str(a) + "-" + str(b) + ".png"
    Image.fromarray(rgba).save(outdir / name, optimize=True)
    meta["faces"].append(
        {
            "a": a,
            "b": b,
            "file": name,
            "width_m": length,
            "valid_fraction": round(float(allowed.mean()), 4),
            "coping_depth_m": 0.14 if a != 0 else 0.0,
            "coping_height_m": 0.27 if a != 0 else 0.0,
        }
    )
    uv = projection(np.c_[plan[[a, b]], np.full(2, top)], st) * small_scale
    draw.line([tuple(v) for v in uv], fill="yellow", width=4)
    thumb = Image.fromarray(np.uint8(np.where(allowed[:, :, None], corrected, face_colors) * 255))
    thumb.thumbnail((370, 250))
    summary.append(thumb)
meta["entrance_roof"] = json.loads((r / "awning-data.json").read_text())
meta["brick_srgb"] = face_colors.tolist()
meta["masonry_file"] = "davidson-masonry.png"
meta["masonry_repeat_m"] = 2.0
(outdir / "photo-facades-19880-23726.json").write_text(json.dumps(meta, indent=2))
review.thumbnail((900, 670))
review.save(r / "fit-review.jpg")
sheet = Image.new("RGB", (sum(v.width for v in summary), max(v.height for v in summary)), "white")
x = 0
for v in summary:
    sheet.paste(v, (x, 0))
    x += v.width
sheet.save(r / "rectified-walls.jpg")
print(meta["eave_above_center"], meta["brick_srgb"], [f["valid_fraction"] for f in meta["faces"]])
