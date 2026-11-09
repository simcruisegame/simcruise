"""Build and score an opt-in landmark profile from a frozen public survey."""

import argparse
import hashlib
import io
import json
from pathlib import Path
from urllib.request import urlopen

import laspy
import numpy as np
from pyproj import Transformer
from shapely.geometry import Point, shape

from appearance import finish
from envelope import build
from sphere import fit_candidate
from validation import check


def read_json(path):
    return json.loads(path.read_text())


def frozen_cloud(manifest, cache, metric_crs, online):
    """Read only the listed public LAS objects. Every object must match its source hash."""
    cache.mkdir(parents=True, exist_ok=True)
    xmin, ymin, xmax, ymax = manifest["bounds"]
    transform = Transformer.from_crs(manifest["crs"], metric_crs, always_xy=True)
    chunks = []
    for node in manifest["nodes"]:
        path = cache / node["path"].replace("/", "__")
        if not path.is_file():
            if not online:
                raise FileNotFoundError(
                    f"{path} missing. Use --fetch to download the pinned public nodes."
                )
            with urlopen(manifest["source"] + node["path"], timeout=25) as response:
                payload = response.read(node["bytes"] + 1)
        else:
            payload = path.read_bytes()
        if (
            len(payload) != node["bytes"]
            or hashlib.sha256(payload).hexdigest() != node["sha256"]
        ):
            raise ValueError(f"Survey object differs from manifest: {node['path']}")
        if not path.exists():
            path.write_bytes(payload)
        las = laspy.read(io.BytesIO(payload))
        x, y = np.asarray(las.x), np.asarray(las.y)
        inside = (x >= xmin) & (x <= xmax) & (y >= ymin) & (y <= ymax)
        east, north = transform.transform(x[inside], y[inside])
        fields = (
            las.z,
            las.classification,
            las.point_source_id,
            las.return_number,
            las.number_of_returns,
            las.intensity,
        )
        chunks.append(
            np.column_stack(
                [east, north, *(np.asarray(field)[inside] for field in fields)]
            )
        )
    return np.vstack(chunks)


def replay(path, cache, output, online):
    site = read_json(path)
    manifest = read_json(path.parent / site["nodes"])
    entire = frozen_cloud(manifest, cache, site["metric_crs"], online)
    cell = np.floor(entire[:, :2] / 0.48).astype(int)
    withheld = (cell[:, 0] + 3 * cell[:, 1]) % 7 == 0
    training = entire[~withheld]
    footprints = read_json(path.parent / site["footprints"])["features"]
    anchor = Point(*site["anchor"])
    outer = max(
        (f for f in footprints if shape(f["geometry"]).covers(anchor)),
        key=lambda f: shape(f["geometry"]).area,
    )
    tile, record = build(outer, training, site["metric_crs"])
    if tile.tolist() != site["tile"]:
        raise ValueError("Footprint and case tile disagree")
    document = dict(
        schema=1,
        tile=tile.tolist(),
        source=site["source_label"],
        source_url=manifest["source"] + "ept.json",
        acquired=site["survey_dates"],
        roofs=[record],
    )
    # One supported cap per profile. Complex or ambiguous cases keep the measured grid.
    caps = [
        model
        for f in footprints
        if f is not outer
        and shape(f["geometry"]).intersects(shape(outer["geometry"]))
        and (
            model := fit_candidate(
                f, training, site["metric_crs"], site["vertical_origin_m"]
            )
        )
    ]
    cap = caps[0] if len(caps) == 1 else None
    validation = check(document, entire[withheld], cap, site["metric_crs"])
    colors = (
        finish(document, entire, site, path.parent, cap)
        if len(site.get("training", [])) == 2
        else {}
    )
    output.mkdir(parents=True, exist_ok=True)
    profile = output / ("lidar-roofs-%d-%d.json" % tuple(tile))
    profile.write_text(json.dumps(document, separators=(",", ":")) + "\n")
    (output / "validation.json").write_text(json.dumps(validation, indent=2) + "\n")
    (output / "appearance.json").write_text(json.dumps(colors, indent=2) + "\n")
    print(
        f"{profile}\n{len(entire)} survey returns, {withheld.sum()} reserved; {validation['all_first_returns']}; SHA256 {hashlib.sha256(profile.read_bytes()).hexdigest()}"
    )


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("site", type=Path)
    parser.add_argument("--cache", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument(
        "--fetch",
        action="store_true",
        help="download missing frozen EPT objects; checksums verified",
    )
    args = parser.parse_args()
    replay(args.site, args.cache, args.output, args.fetch)
