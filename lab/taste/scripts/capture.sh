#!/bin/sh
# Re-run the taste test's game captures (lab/taste/README.md).
#
#   GODOT_BIN=/path/to/Godot_v4.7.2_console sh lab/taste/scripts/capture.sh /abs/out [renderer]
#
# renderer: mobile (default, the shipping renderer) or forward_plus.
# Needs a window (captures read the viewport) and tools/atlas.exe (or tools/atlas)
# built from helper/atlas.mjs; see the README. Writes:
#   OUT/car/nb1999_*.jpg        CarTurntable, Ultra, with --lamps
#   OUT/place/*.jpg + *.log     ReferencePose at fixed poses, Ultra
set -u
root=$(CDPATH='' cd -- "$(dirname -- "$0")/../../.." && pwd)
out=${1:?output directory}
rend=${2:-mobile}
godot=${GODOT_BIN:-godot}
mkdir -p "$out/car" "$out/place"
win() { if command -v cygpath >/dev/null 2>&1; then cygpath -m "$1"; else printf '%s' "$1"; fi; }
helper="$root/tools/atlas.exe"; [ -x "$helper" ] || helper="$root/tools/atlas"
SIMCRUISE_MAP_HELPER=$(win "$helper"); export SIMCRUISE_MAP_HELPER
extra=""; [ "$rend" = forward_plus ] && extra="--rendering-method forward_plus"
cd "$root"

# (A) The car: 13 studio views, driver view and 4 dusk shots.
timeout 400 "$godot" --path game --windowed --resolution 1600x1000 $extra -- \
  --car-turntable "$(win "$out/car")" --quality=ultra --lamps > "$out/car/turntable.log" 2>&1
echo "car rc=$? $(grep -c '^TURNTABLE' "$out/car/turntable.log") views"

# (B) The place. NAME RESOLUTION LAT LON HEADING HFOV [user args...]
shot() {
  name=$1; res=$2; lat=$3; lon=$4; hd=$5; fov=$6; shift 6
  timeout 300 "$godot" --path game --windowed --resolution "$res" $extra -- --quality=ultra \
    --reference "$lat" "$lon" "$hd" "$fov" "$(win "$out/place/$name.jpg")" 0 0 "$@" --complete-reference \
    > "$out/place/$name.log" 2>&1
  echo "$name rc=$? $(grep -o 'result=[0-9]*' "$out/place/$name.log")"
}
# Panoramax 4b6d216b-ae1c-48a8-b2ae-5d9558334f18 (Lummus Park beachwalk, 2025-04-24 14:34:52 UTC):
# its position, its compass heading, the Pixel 8 main camera's 70.8 deg horizontal field, its capture time.
shot lummus 1600x720 25.7770425 -80.1309641 7 70.8 --reference-epoch 1745505292
# Ocean Drive at 8th Street, driver height, same sun (no licensed street-level frame on the road itself).
shot oceandrive-north 1600x900 25.77800 -80.13118 12 80 --reference-epoch 1745505292
# Two start-pool places picked with the LCG seed 20261110 (README), midday 15 July 2025,
# waiting for the regional terrain so the horizon is in.
shot telluride 1600x900 37.9374275 -107.8118513 108 80 --reference-epoch 1752606000 --landscape-reference
shot oatman 1600x900 35.0441967 -114.3678383 250 80 --reference-epoch 1752606000 --landscape-reference
