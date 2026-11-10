#!/bin/sh
# Run SimCruise's standalone headless checks and print a pass/fail table.
#
#   sh scripts/check.sh                 # the default suite
#   sh scripts/check.sh Track Wall      # only the named checks (game/checks/<Name>.gd)
#
# Environment:
#   GODOT_BIN            Godot 4.7.2 executable (default: `godot` on PATH, then
#                        /Applications/Godot.app/Contents/MacOS/Godot)
#   CHECK_LOG_DIR        where per-check logs go (default: build/check-logs)
#   SIMCRUISE_GODOT_RUN  optional wrapper invoked as `sh WRAPPER SECONDS LOG args...`
#                        (lets automated runs serialise Godot); default: built-in cap
#
# Safety: every Godot run is capped at 600 s, all of its output goes to a log
# file, and no file it writes may exceed 200 MiB. Nothing is printed per frame.
# Checks that need the network or extra arguments are not in the default suite.
# Exit status: the number of failed checks (0 = everything passed).
set -u
root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"
godot=${GODOT_BIN:-godot}
if ! command -v "$godot" >/dev/null 2>&1 && [ "$godot" = godot ]; then
  godot=/Applications/Godot.app/Contents/MacOS/Godot
fi
logs=${CHECK_LOG_DIR:-$root/build/check-logs}
mkdir -p "$logs"
# Checks that touch preferences write them here, never into the player's own.
SIMCRUISE_QA_PREFS_DIR="$logs/prefs"
rm -rf "$SIMCRUISE_QA_PREFS_DIR"; mkdir -p "$SIMCRUISE_QA_PREFS_DIR"
export SIMCRUISE_QA_PREFS_DIR

run_godot() { # SECONDS LOG args...
  secs=$1; log=$2; shift 2
  if [ -n "${SIMCRUISE_GODOT_RUN:-}" ]; then
    sh "$SIMCRUISE_GODOT_RUN" "$secs" "$log" "$@" 2>/dev/null
    return $?
  fi
  # The log is capped through a pipe (100 MiB). Other files get a high cap with
  # SIGXFSZ ignored, because Apple's Metal shader cache for Godot can pass
  # 200 MiB and a low `ulimit -f` would kill every run that compiles shaders.
  (
    trap '' XFSZ
    ulimit -f 2097152
    # GNU timeout where it exists (Linux, Git Bash on Windows); perl's alarm
    # elsewhere (stock macOS). MSYS perl cannot exec a Windows .exe, so on
    # Windows the perl path ran nothing and left an empty log.
    if command -v timeout >/dev/null 2>&1; then
      { timeout "$secs" "$godot" "$@" 2>&1; echo $? > "$log.code"; } | head -c 104857600 > "$log"
    else
      { perl -e 'alarm shift @ARGV; exec @ARGV' "$secs" "$godot" "$@" 2>&1; echo $? > "$log.code"; } | head -c 104857600 > "$log"
    fi
  )
  code=$(cat "$log.code" 2>/dev/null); [ -n "$code" ] || code=1
  return "$code"
}

# name:fixed-fps (0 = no fixed fps)
default_suite="Track:60 RideCheck:60 TurningCircle:60 Wall:30 KeyboardTap:60 SteerTransient:60
DifferentialCheck:60 RearDamperCheck:60 TireStep:60 SplitGrip:60 LockedBrakeTurn:60
LightingCheck:0 DaylightWeatherCheck:0 HeadCameraCheck:0 HudLevelCheck:0 RoadNoiseCheck:0
VegetationCheck:0 RoadProfile:0 RoadBankMeshCheck:0 CrownArrivalOrder:0 SignalControlCheck:0
StorefrontCheck:0 ObservedPaintCheck:0 PhotoFacadeCheck:0 LandmarkRoof:0
HistoricAnchorageCheck:0 SustainedSurfaceCheck:0 DriftHold:60 BumpDrive:60 CarTrack:60
AmbientCheck:0 TrafficCheck:0 SBend:60 LiftOff:60 TractionControlCheck:60 CombinedSlip:60 AbsReverse:60
ParkedStart:60"

fps_for() {
  for entry in $default_suite; do
    [ "${entry%%:*}" = "$1" ] && { echo "${entry##*:}"; return; }
  done
  echo 60
}

if [ $# -gt 0 ]; then names="$*"; else names=$(for e in $default_suite; do echo "${e%%:*}"; done); fi

# Always refresh the import cache first: it is incremental (fast when nothing
# changed) and new textures or models from a merge are otherwise missing, which
# makes every check that loads the game fail to compile.
[ -d game/.godot ] || echo "Importing the project (first run: 1-3 minutes)..."
# The editor import re-saves some scripts with "convert indent on save", which
# turns 4-space indentation into tabs and breaks scripts that indent with spaces.
# Keep a copy of every script and put back any the import rewrote.
scripts_backup="$logs/scripts-before-import"
rm -rf "$scripts_backup"; mkdir -p "$scripts_backup"
(cd game && find . -name '*.gd' -not -path './.godot/*' | while read -r f; do mkdir -p "$scripts_backup/$(dirname "$f")"; cp -p "$f" "$scripts_backup/$f"; done)
run_godot 900 "$logs/import.log" --headless --editor --path game --import
(cd "$scripts_backup" && find . -name '*.gd') | while read -r f; do
  if ! cmp -s "$scripts_backup/$f" "game/$f"; then cp -p "$scripts_backup/$f" "game/$f"; echo "restored game/${f#./} after the editor import rewrote it"; fi
done
if grep -q "SCRIPT ERROR\|Parse Error" "$logs/import.log" 2>/dev/null; then
  echo "import reported script errors; see $logs/import.log"
fi

failed=0; passed=0; skipped=0
printf '%-24s %-6s %6s  %s\n' CHECK RESULT SECS DETAIL
for name in $names; do
  if [ ! -f "game/checks/$name.gd" ]; then
    printf '%-24s %-6s %6s  %s\n' "$name" skip - "not in this tree"; skipped=$((skipped+1)); continue
  fi
  fps=$(fps_for "$name")
  log="$logs/$name.log"
  start=$(date +%s)
  if [ "$fps" != 0 ]; then
    run_godot 600 "$log" --headless --path game --fixed-fps "$fps" --script "res://checks/$name.gd"
  else
    run_godot 600 "$log" --headless --path game --script "res://checks/$name.gd"
  fi
  code=$?
  # An empty log means Godot never ran (e.g. the time-limit wrapper failed); never count it as a pass.
  [ -s "$log" ] || code=127
  secs=$(( $(date +%s) - start ))
  detail=$(grep -E "RESULT|PASS|FAIL" "$log" 2>/dev/null | tail -1 | cut -c1-110)
  result=pass
  [ "$code" -ne 0 ] && result=FAIL
  grep -q "SCRIPT ERROR\|SHADER ERROR\|Parse Error" "$log" 2>/dev/null && result=FAIL
  if [ "$name" = Track ] && [ "$result" = pass ]; then
    # Calibration bands from CONTRIBUTING.md: 0-60 7.95 s, 70-0 173.0 ft, skidpad 0.885 g.
    awk '
      /RESULT flat:/ { for(i=1;i<=NF;i++){ if($i=="="&&$(i+2)=="s;") t=$(i+1); if($i=="="&&$(i+2)=="ft") b=$(i+1) } }
      /RESULT skidpad/ { for(i=1;i<=NF;i++) if($i ~ /^g:$/) g=$(i+1) }
      END {
        bad = (t=="" || t<7.85 || t>8.05 || b=="" || b<170 || b>176 || g=="" || g<0.875 || g>0.895)
        printf "%s %s %s\n", t, b, g > "/dev/stderr"
        exit bad
      }' "$log" 2>/dev/null || result=FAIL
  fi
  printf '%-24s %-6s %6s  %s\n' "$name" "$result" "$secs" "${detail:-exit $code}"
  if [ "$result" = pass ]; then passed=$((passed+1)); else failed=$((failed+1)); fi
done
echo "passed $passed, failed $failed, skipped $skipped (logs in $logs)"
exit "$failed"
