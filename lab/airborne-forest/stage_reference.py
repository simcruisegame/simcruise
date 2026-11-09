"""Generate the A/B camera check inside a disposable copy of game/."""

import argparse
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("scratch_game", type=Path)
args = parser.parse_args()
here = Path(__file__).resolve().parent
game = args.scratch_game.resolve()
if game == (here.parents[1] / "game").resolve():
    raise SystemExit("Use a separate scratch game copy for the A/B harness.")
path = game / "checks/ReferencePose.gd"
text = path.read_text()
if "func forest_cost(" in text:
    raise SystemExit("Camera check already staged.")
trigger = ' if "--landscape-cost" in OS.get_cmdline_user_args():'
if text.count(trigger) != 1 or text.count(" if frame<60:return") != 1:
    raise SystemExit("Camera check entry changed. Review before staging.")
CAPTURE = ' if "--forest-depth-ab" in OS.get_cmdline_user_args():\n  await capture_forest()\n  return\n'
COST = (here / "reference_pair.txt").read_text()
text = text.replace(
    " if frame<60:return",
    ' if frame<60:return\n if "--forest-depth-ab" in OS.get_cmdline_user_args() and game.grove.observed_forest.tiles.is_empty():return',
)
path.write_text(text.replace(trigger, CAPTURE + trigger) + "\n" + COST)
