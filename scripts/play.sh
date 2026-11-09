#!/bin/sh
# Update this checkout, prepare it and play SimCruise (Apple Silicon Mac).
#
#   sh scripts/play.sh            # git pull, compile the map helper, import, play
#   sh scripts/play.sh --no-pull  # play the code as it is
#
# Needs Godot 4.7.2 (in /Applications or GODOT_BIN) and Bun (on PATH, in
# ~/.bun/bin or BUN_BIN). First time from nothing, see docs/building.md.
set -eu
root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"
godot=${GODOT_BIN:-}
if [ -z "$godot" ]; then
  if [ -x /Applications/Godot.app/Contents/MacOS/Godot ]; then godot=/Applications/Godot.app/Contents/MacOS/Godot
  else godot=$(command -v godot || true); fi
fi
if [ -z "$godot" ] || [ ! -x "$godot" ]; then
  echo "Godot 4.7.2 not found. Put Godot.app in /Applications or set GODOT_BIN." >&2
  exit 1
fi
if [ "${1:-}" != --no-pull ]; then git pull --ff-only; fi
GODOT_BIN="$godot" sh scripts/build-macos.sh editor
exec "$godot" --path game
