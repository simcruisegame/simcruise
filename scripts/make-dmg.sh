#!/bin/sh
# Package the exported Mac app into a release disk image using only the tools
# that ship with macOS (hdiutil, ditto, codesign).
#
#   sh scripts/make-dmg.sh [VERSION] [APP_PATH] [OUT_DIR]
#
# VERSION   Release version for the file name. Resolved in this order: the first
#           argument, $SIMCRUISE_VERSION, application/short_version or
#           application/version in game/export_presets.cfg, config/version in
#           game/project.godot, then the first "## SimCruise X.Y.Z" heading of
#           CHANGELOG.md (with a warning, since that entry may be unreleased).
# APP_PATH  The app bundle to package. Default: build/SimCruise.app, which is what
#           `sh scripts/build-macos.sh app` produces.
# OUT_DIR   Where the image is written. Default: build/.
#
# The image contains the app, an Applications symlink for drag-to-install and
# README-first-launch.txt, which explains the Gatekeeper prompt an ad-hoc-signed,
# un-notarized app shows the first time it opens. Output: OUT_DIR/SimCruise-VERSION.dmg
#
# Environment: SIMCRUISE_ALLOW_UNSIGNED=1 packages an app that fails codesign
# verification (for testing the script; release builds are always signed ad hoc).
set -eu
root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"

usage() { echo "Usage: $0 [VERSION] [APP_PATH] [OUT_DIR]" >&2; exit 2; }
case "${1:-}" in -h|--help) usage;; esac

if [ "$(uname -s)" != Darwin ]; then
  echo "make-dmg.sh needs macOS: it uses hdiutil, ditto and codesign." >&2
  exit 1
fi
for tool in /usr/bin/hdiutil /usr/bin/ditto /usr/bin/codesign /usr/bin/shasum; do
  if [ ! -x "$tool" ]; then echo "Missing $tool." >&2; exit 1; fi
done

# config_value FILE KEY prints the value of a KEY="value" line (first match, quotes stripped).
config_value() {
  [ -f "$1" ] || return 0
  sed -n "s|^$2=\"\{0,1\}\([^\"]*\)\"\{0,1\}.*|\1|p" "$1" | head -n 1
}

version=${1:-${SIMCRUISE_VERSION:-}}
if [ -z "$version" ]; then version=$(config_value game/export_presets.cfg application/short_version); fi
if [ -z "$version" ]; then version=$(config_value game/export_presets.cfg application/version); fi
if [ -z "$version" ]; then version=$(config_value game/project.godot config/version); fi
if [ -z "$version" ] && [ -f CHANGELOG.md ]; then
  version=$(sed -n 's/^## SimCruise \([0-9][0-9A-Za-z._-]*\).*/\1/p' CHANGELOG.md | head -n 1)
  if [ -n "$version" ]; then
    echo "Note: no version in game/export_presets.cfg or game/project.godot; using $version from the top of CHANGELOG.md. Pass the version explicitly for a release." >&2
  fi
fi
if [ -z "$version" ]; then
  echo "No version found. Pass one: sh scripts/make-dmg.sh 1.1.0" >&2
  usage
fi
case "$version" in
  *[!0-9A-Za-z._-]*) echo "Version '$version' may only contain letters, digits, '.', '_' and '-'." >&2; exit 2;;
esac

app=${2:-$root/build/SimCruise.app}
out_dir=${3:-$root/build}
if [ ! -d "$app/Contents" ]; then
  echo "No app bundle at $app. Build one first: sh scripts/build-macos.sh app" >&2
  exit 1
fi
app_name=$(basename "$app" .app)

if ! /usr/bin/codesign --verify --deep --strict "$app" >/dev/null 2>&1; then
  if [ "${SIMCRUISE_ALLOW_UNSIGNED:-0}" = 1 ]; then
    echo "Warning: $app does not pass codesign verification; packaging anyway (SIMCRUISE_ALLOW_UNSIGNED=1)." >&2
  else
    echo "$app does not pass codesign verification. scripts/build-macos.sh signs the export ad hoc; rebuild, or set SIMCRUISE_ALLOW_UNSIGNED=1 to package it anyway." >&2
    exit 1
  fi
fi

mkdir -p "$out_dir"
stage=$(mktemp -d "${TMPDIR:-/tmp}/simcruise-dmg.XXXXXX")
trap 'rm -rf "$stage"' EXIT

# ditto keeps the bundle's symlinks, permissions and extended attributes intact.
/usr/bin/ditto "$app" "$stage/$app_name.app"
/bin/ln -s /Applications "$stage/Applications"

cat > "$stage/README-first-launch.txt" <<EOF
SimCruise $version

INSTALL
  Drag SimCruise onto the Applications shortcut in this window, then open it
  from Applications. Requires a Mac with Apple Silicon (M1 or later) running
  macOS 13 Ventura or newer.

FIRST LAUNCH
  SimCruise is free, open-source software signed ad hoc by its build script.
  It is not notarized by Apple (that needs a paid developer account, which this
  project deliberately avoids), so macOS warns once that it cannot verify the
  developer. The app is exactly what the project published; allow it once:

  macOS 13 Ventura and 14 Sonoma
    Control-click (right-click) SimCruise in Applications, choose Open, then
    click Open in the dialog. macOS remembers the choice.

  macOS 15 Sequoia and later
    Double-click the app once and dismiss the warning. Open System Settings >
    Privacy & Security, scroll to Security, click "Open Anyway" next to
    SimCruise, authenticate, then open the app again.

  Terminal alternative
    xattr -dr com.apple.quarantine /Applications/SimCruise.app

WHAT IT NEEDS
  No account, API key or subscription. An internet connection is needed to
  drive anywhere outside the bundled starter area; the map helper fetches public
  data (OpenStreetMap, USGS, USDA, NOAA, NASA, Copernicus, Overture and others)
  and keeps a cache of about 96 MB in
  ~/Library/Application Support/Godot/app_userdata/SimCruise/
  Delete that folder and the app to uninstall completely.

LICENSES
  Game code is MIT. Map data, surveys, photographs, models and fonts keep their
  own licenses; see Contents/Resources/Credits inside the app and the project
  README. Map data (c) OpenStreetMap contributors, ODbL.
EOF

dmg="$out_dir/SimCruise-$version.dmg"
rm -f "$dmg"
# UDZO: read-only, zlib-compressed, with an embedded checksum that hdiutil verify checks.
/usr/bin/hdiutil create -quiet -volname "SimCruise $version" -srcfolder "$stage" \
  -fs HFS+ -format UDZO -imagekey zlib-level=9 -ov "$dmg"
/usr/bin/hdiutil verify -quiet "$dmg"
echo "Built $dmg ($(du -h "$dmg" | cut -f1))"
/usr/bin/shasum -a 256 "$dmg"
