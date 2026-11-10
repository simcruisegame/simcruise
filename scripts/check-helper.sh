#!/bin/sh
# Run the map helper's offline tests and print a pass/fail table.
#
#   sh scripts/check-helper.sh
#
# These tests use recorded or synthetic responses and need no network. Tests
# that fetch live data (check-forest-form, check-regional-terrain,
# verify-streamed-grade, overture_sample, prepare_world) are not run here.
# Environment: BUN_BIN (default: bun on PATH).
# Exit status: the number of failed tests (0 = everything passed).
set -u
root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"
bun=${BUN_BIN:-bun}
logs=${CHECK_LOG_DIR:-$root/build/check-logs}/helper
mkdir -p "$logs"
[ -d node_modules ] || "$bun" install --frozen-lockfile >/dev/null

offline="cache-check check-cache-eviction check-ground-reflectance check-guard-rails check-lane-counts check-panoramax
check-pedestrian check-rangeland check-regional-height-outliers check_civic_trees
check_overture_heights check_regional_aerial check_signal_controls
check_verified_footprints nsi-check optional-street-objects street-appearance-check
street_sample_check verify-road-banks"
# node:test suites run under Bun's test runner.
suites="test-spectral-ground"

failed=0; passed=0
printf '%-34s %-6s\n' TEST RESULT
run() { # name command...
 name=$1; shift
 if "$@" > "$logs/$name.log" 2>&1; then r=pass; passed=$((passed+1)); else r=FAIL; failed=$((failed+1)); fi
 printf '%-34s %-6s\n' "$name" "$r"
}
for t in $offline; do run "$t" "$bun" "lab/$t.mjs"; done
for t in $suites; do run "$t" "$bun" test "./lab/$t.mjs"; done
"$bun" build --target=bun helper/atlas.mjs --outdir "$logs/build" > "$logs/build.log" 2>&1 \
 && { printf '%-34s %-6s\n' "helper bundles" pass; passed=$((passed+1)); } \
 || { printf '%-34s %-6s\n' "helper bundles" FAIL; failed=$((failed+1)); }
echo "passed $passed, failed $failed (logs in $logs)"
exit "$failed"
