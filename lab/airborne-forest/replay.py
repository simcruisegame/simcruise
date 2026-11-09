"""Author an opt-in, local survey. All downloads stay in the explicit work/cache directories."""

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import sys

here = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("work", type=Path)
parser.add_argument("--config", type=Path, required=True)
parser.add_argument(
    "--atlas",
    type=Path,
    help="Compiled Sunday Driver atlas helper, required for any missing optical/vector inputs.",
)
parser.add_argument(
    "--cache", type=Path, help="Atlas cache. Defaults to work/atlas-cache."
)
parser.add_argument(
    "--audit",
    type=Path,
    help="Published SHA-256 input manifest, required for an exact historical replay.",
)
args = parser.parse_args()
root = args.work.resolve()
root.mkdir(exist_ok=True, parents=True)
config = json.loads(args.config.read_text())
config["aerialDirectory"] = "aerials"
config["gameDataDirectory"] = "data"
(root / "survey.json").write_text(json.dumps(config))
os.environ["FOREST_WORK"] = str(root)
from config import side, cx, cy, swath, stem, region

for name in ("aerials", "canopy", "data", "vectors"):
    (root / name).mkdir(exist_ok=True)
cache = (args.cache or (root / "atlas-cache")).resolve()
cache.mkdir(exist_ok=True, parents=True)


def fetch(command, x, y, *flags):
    if not args.atlas:
        raise ValueError(
            f"Input missing for {command} {x} {y}. Supply --atlas and optionally --cache."
        )
    return json.loads(
        subprocess.check_output(
            [args.atlas.resolve(), command, cache, str(x), str(y), *flags], timeout=90
        )
    )


def hash_file(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


expected = json.loads(args.audit.read_text()) if args.audit else {}
inputs = {}


def pin(path):
    rel = str(path.relative_to(root))
    if path.suffix == ".json":
        record = json.loads(path.read_text())
        fields = (
            ("id", "roads", "buildings", "land")
            if rel.startswith("vectors/")
            else ("tile", "grid", "bits")
        )
        encoded = json.dumps(
            [record.get(k) for k in fields], separators=(",", ":")
        ).encode()
        inputs[rel] = hashlib.sha256(encoded).hexdigest()
    else:
        inputs[rel] = hash_file(path)
    if args.audit and expected.get(rel) != inputs[rel]:
        raise ValueError(f"Historical input changed: {rel}")


left, bottom, right, top = swath
extent = [cx + left / side, cy - top / side, cx + right / side, cy - bottom / side]
cells = [
    (x, y)
    for x in range(math.floor(extent[0]), math.floor(extent[2]) + 1)
    for y in range(math.floor(extent[1]), math.floor(extent[3]) + 1)
]
if len(cells) > 24:
    raise ValueError("Survey exceeds the 24 road-tile optical input budget")
for x, y in cells:
    canopy = root / "canopy" / f"c2-{x}-{y}"
    if not canopy.exists():
        block = cache / canopy.name
        if not block.exists():
            fetch("canopy", x, y)
        shutil.copyfile(block, canopy)
    pin(canopy)
    vector = root / "vectors" / f"{x}-{y}.json"
    if not vector.exists():
        vector.write_text(
            json.dumps(
                fetch("tile", x, y, "--without-road-banks"), separators=(",", ":")
            )
        )
    pin(vector)
for x, y in sorted({(x // 4, y // 4) for x, y in cells}):
    mask = root / "canopy" / f"forest-form-{x}-{y}.json"
    if not mask.exists():
        result = fetch("forest-form", x, y)
        if result.get("status") not in ("available", "no_coverage", "unsupported"):
            raise ValueError(f"Forest-type source unavailable at {x},{y}")
        mask.write_text(json.dumps(result, separators=(",", ":")))
    pin(mask)
    image = root / "aerials" / f"a-{x}-{y}.jpg"
    if not image.exists():
        previous = cache / image.name
        if not previous.exists():
            result = fetch("aerial", x, y)
            if result.get("aerial") != image.name:
                raise ValueError(f"Unsupported or missing USGS imagery at {x},{y}")
        shutil.copyfile(previous, image)
    pin(image)
for script in ("collect", "grid", "peaks", "bake", "land", "ground", "hold_lidar"):
    subprocess.run([sys.executable, here / (script + ".py")], check=True)
catalog = {
    "version": 1,
    "surveys": [{"region": region, "bounds": [round(v, 7) for v in extent]}],
}
(root / "data/als-forest-index.json").write_text(
    json.dumps(catalog, separators=(",", ":"))
)
(root / "optical-inputs.json").write_text(json.dumps(inputs, indent=2))
print(
    f"Authored {stem} in {root / 'data'}. Review the held-out evidence before adding the survey to a runtime catalog."
)
