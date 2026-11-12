#!/bin/sh
# Start a built Windows release briefly, without a window, to confirm it runs.
# Run on Windows (Git Bash) after scripts/build-windows.sh:
#
#   sh scripts/smoke-windows.sh                                   # build/SimCruise-<config/version>-windows-x64.zip
#   sh scripts/smoke-windows.sh build/SimCruise-1.1.0-windows-x64.zip
#
# Unpacks the portable zip into a temporary folder and checks that
#   1. SimCruise.exe --headless --version exits 0 and reports Godot 4.7.2;
#   2. atlas.exe starts (an unknown request must answer "Unknown map request");
#   3. SimCruise.exe --headless --quit-after 600 loads the game offline (map
#      helper switched off, so it starts on the bundled Miami Beach tiles),
#      exits 0 and logs no script or shader error.
# APPDATA points into the temporary folder for these runs, so the player's own
# cache, settings and logs in %APPDATA%\Godot\app_userdata\SimCruise are not touched.
set -eu
root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
version=$(sed -n 's/^config\/version="\(.*\)"/\1/p' "$root/game/project.godot")
zip=${1:-$root/build/SimCruise-$version-windows-x64.zip}
[ -f "$zip" ] || { echo "No portable zip at $zip; run sh scripts/build-windows.sh first." >&2; exit 1; }
work=$(mktemp -d "${TMPDIR:-/tmp}/simcruise-smoke.XXXXXX")
trap 'rm -rf "$work"' EXIT
unzip -q "$zip" -d "$work/unpacked"
app=$(dirname "$(find "$work/unpacked" -name SimCruise.exe | head -n 1)")
[ -f "$app/SimCruise.exe" ] && [ -f "$app/atlas.exe" ] || { echo "The zip lacks SimCruise.exe or atlas.exe." >&2; exit 1; }
mkdir -p "$work/appdata" "$work/prefs"
appdata=$work/appdata
if command -v cygpath >/dev/null 2>&1; then appdata=$(cygpath -w "$appdata"); fi
fail=0

code=0
out=$(APPDATA=$appdata timeout 120 "$app/SimCruise.exe" --headless --version 2>&1) || code=$?
case "$code:$out" in
  0:*4.7.2.stable*) echo "ok   SimCruise.exe --version: $out";;
  0:) echo "ok   SimCruise.exe --version exited 0 (no console output captured)";;
  *) echo "FAIL --version exited $code and printed: $out"; fail=1;;
esac

atlas_out=$(timeout 60 "$app/atlas.exe" smoke-test "$work/atlas-cache" 2>&1) || true
case "$atlas_out" in
  *"Unknown map request"*) echo "ok   atlas.exe starts";;
  *) echo "FAIL atlas.exe: $atlas_out"; fail=1;;
esac

log=$work/game.log
code=0
APPDATA=$appdata SIMCRUISE_QA_PREFS_DIR=$work/prefs SIMCRUISE_MAP_HELPER=$work/no-helper.exe \
  timeout 300 "$app/SimCruise.exe" --headless --quit-after 600 --log-file "$log" > "$work/stdout.log" 2>&1 || code=$?
cat "$work/stdout.log" >> "$log" 2>/dev/null || true
if [ "$code" -ne 0 ]; then echo "FAIL headless run exited $code"; fail=1
elif grep -q "SCRIPT ERROR\|SHADER ERROR\|Parse Error" "$log" 2>/dev/null; then echo "FAIL headless run logged errors:"; grep -m 5 "SCRIPT ERROR\|SHADER ERROR\|Parse Error" "$log"; fail=1
else echo "ok   headless run: 600 frames, exit 0, no script errors ($(wc -l < "$log" | tr -d ' ') log lines)"
fi
[ "$fail" -eq 0 ] || { echo "--- last log lines"; tail -n 40 "$log" 2>/dev/null || true; exit 1; }
echo "Smoke test passed: $zip"
