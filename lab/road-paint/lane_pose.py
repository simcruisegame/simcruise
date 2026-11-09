"""Test the independent hypothesis that an uncalibrated GPS sample lay on the center of its observed travel lane."""

from optics import *


def solve_lane(site):
    geoms, poses, a, r = load_site(site)
    doc = json.load(open(site + "/camera-fit.json"))
    model = doc["parameters"]
    mid = poses[1]
    dashes = np.array(
        sorted(
            (g["cen"] for g in geoms if g["kind"] == "dash"),
            key=lambda v: np.linalg.norm(v - mid),
        )[:4]
    )
    c = np.polyfit((dashes - mid) @ a, (dashes - mid) @ r, 1)
    width = json.load(open(site + "/width.json"))["aerial_lane_spacing_m"]
    q = list(model)
    q[3] = float(c[1] + width / 2)
    q[4] = 0.0
    labels = ANNOT[site][1]
    arrow = []
    dash = []
    im = Image.open(site + "/photo-1.jpg")
    dr = ImageDraw.Draw(im)
    for i, g in enumerate(geoms):
        ends = project(g["ends"], mid, a, r, q)
        uv = project(g["poly"], mid, a, r, q)
        if g["kind"] == "dash" and np.isfinite(ends).all():
            dash.append(
                (float(np.linalg.norm(ends - labels["dash"], axis=1).mean()), i)
            )
        if g["kind"] == "symbol":
            arrow.append(project(g["cen"][None, :], mid, a, r, q)[0])
        if not np.isfinite(uv).all() or np.max(abs(uv)) > 6000:
            continue
        dr.line(
            [tuple(cn) for cn in uv],
            fill=(255, 122, 194) if g["kind"] == "symbol" else (100, 175, 255),
            width=2,
        )
    result = {
        "method": "Take the raw middle GPS downtrack station. Center it laterally between the repeated actual divider and right stripe. Shared optics and eye height come only from the outer camera images.",
        "middle_paint_used_to_fit": False,
        "middle_image_used_to_fit": False,
        "right_lane_width_m": width,
        "assumed_camera_params": q,
        "nearest_dash_mean_endpoint_error_px": min(dash)[0],
    }
    if arrow:
        arrow.sort(key=lambda p: p[0])
        result.update(
            predicted_arrow_centroids=[p.tolist() for p in arrow],
            mean_arrow_centroid_error_px=float(
                np.linalg.norm(np.array(arrow) - labels["symbols"], axis=1).mean()
            ),
        )
    print(site, result)
    im.save(site + "/lane-registered.jpg", quality=93)
    Path(site + "/lane-registration.json").write_text(json.dumps(result, indent=2))


if __name__ == "__main__":
    for site in SITES:
        solve_lane(site)
