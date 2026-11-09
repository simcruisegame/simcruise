"""Rebuild the Cheyenne verified texture atlas from local, checksum-checked photographs."""
import argparse
import json
from pathlib import Path
import sys

here = Path(__file__).resolve().parent
sys.path.insert(0, str(here.parent))
from landmark_projection import build

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('photos', type=Path)
parser.add_argument('output', type=Path)
parser.add_argument('--geometry', type=Path, required=True)
args = parser.parse_args()
print(json.dumps(build(here/'registration.json', args.geometry, args.photos, args.output), indent=2))
