#!/bin/sh
# Build the Apple Silicon macOS release on Windows (Git Bash) or Linux, without a Mac:
#
#   sh scripts/cross-build-macos.sh            # version from config/version
#   sh scripts/cross-build-macos.sh 1.1.0      # explicit version
#
# Produces in build/:
#   SimCruise-VERSION-macos-arm64.zip   SimCruise.app, signed ad hoc
#   SHA256SUMS-macos.txt
#
# Steps: Bun cross-compiles the map helper for darwin-arm64; Godot exports the
# "macOS" preset (the official template is universal, so the engine is then
# thinned to arm64, as build-macos.sh does with lipo); the helper and the
# licences are added to the bundle; rcodesign (github.com/indygreg/apple-platform-rs)
# signs the finished bundle ad hoc, the equivalent of the Mac build's
# `codesign --force --deep --sign -`, because Apple Silicon refuses to run
# unsigned arm64 code; scripts/macos-bundle.mjs
# checks every Mach-O file's architecture and signature hashes and the bundle
# seal, then writes the zip with Unix permissions so the executables stay
# executable when a Mac unpacks it.
#
# Needs Godot 4.7.2 with its macOS export template (GODOT_BIN;
# `sh scripts/install-godot.sh DIR macos` fetches both) and Bun (BUN_BIN).
# rcodesign 0.29.0 is taken from RCODESIGN, else PATH, else downloaded once
# into tools/ from its official GitHub release and checked against a pinned
# SHA-256. A disk image needs macOS (scripts/make-dmg.sh); this route ships the
# zip. Whether the result launches can only be confirmed on a Mac.
set -eu
root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"
godot=${GODOT_BIN:-godot}
bun=${BUN_BIN:-bun}
if ! command -v "$bun" >/dev/null 2>&1 && [ "$bun" = bun ] && [ -x "$HOME/.bun/bin/bun" ]; then bun=$HOME/.bun/bin/bun; fi
for tool in "$godot" "$bun"; do
  command -v "$tool" >/dev/null 2>&1 || { echo "Missing $tool (see the header of this script)." >&2; exit 1; }
done
case "$("$godot" --version)" in 4.7.2.stable*) ;; *) echo "Use Godot 4.7.2 stable and its export templates." >&2; exit 1;; esac

version=${1:-$(sed -n 's/^config\/version="\(.*\)"/\1/p' game/project.godot)}
case "$version" in [0-9]*.[0-9]*.[0-9]*) ;; *) echo "Version '$version' is not X.Y.Z" >&2; exit 1;; esac

# rcodesign: pinned release and checksums (from the release's SHA256SUMS).
rc_version=0.29.0
rcodesign=${RCODESIGN:-}
if [ -z "$rcodesign" ] && command -v rcodesign >/dev/null 2>&1; then rcodesign=rcodesign; fi
if [ -z "$rcodesign" ]; then
  case "$(uname -s):$(uname -m)" in
    MINGW*:x86_64|MSYS*:x86_64|CYGWIN*:x86_64)
      rc_asset=apple-codesign-$rc_version-x86_64-pc-windows-msvc.zip
      rc_sum=54bb500e2da7a8de02fcae0f331d1cac6e6d7173b4281042ff9c528ba3159aaa; rc_exe=rcodesign.exe;;
    Linux:x86_64)
      rc_asset=apple-codesign-$rc_version-x86_64-unknown-linux-musl.tar.gz
      rc_sum=dbe85cedd8ee4217b64e9a0e4c2aef92ab8bcaaa41f20bde99781ff02e600002; rc_exe=rcodesign;;
    *) echo "No pinned rcodesign for $(uname -s) $(uname -m); install rcodesign $rc_version and set RCODESIGN." >&2; exit 1;;
  esac
  rc_dir=tools/rcodesign-$rc_version
  rcodesign=$(find "$rc_dir" -name "$rc_exe" -type f 2>/dev/null | head -n 1)
  if [ -z "$rcodesign" ]; then
    mkdir -p "$rc_dir/download"
    echo "Downloading $rc_asset ..." >&2
    curl -fL --retry 3 --progress-bar -o "$rc_dir/download/$rc_asset" \
      "https://github.com/indygreg/apple-platform-rs/releases/download/apple-codesign%2F$rc_version/$rc_asset"
    got=$(sha256sum "$rc_dir/download/$rc_asset" | cut -d' ' -f1)
    [ "$got" = "$rc_sum" ] || { echo "SHA-256 of $rc_asset does not match the release." >&2; rm -rf "$rc_dir"; exit 1; }
    case "$rc_asset" in
      *.zip) unzip -q -o "$rc_dir/download/$rc_asset" -d "$rc_dir";;
      *) tar -xzf "$rc_dir/download/$rc_asset" -C "$rc_dir";;
    esac
    rm -rf "$rc_dir/download"
    rcodesign=$(find "$rc_dir" -name "$rc_exe" -type f | head -n 1)
    chmod +x "$rcodesign"
  fi
fi
"$rcodesign" --version >/dev/null 2>&1 || { echo "rcodesign at '$rcodesign' does not run." >&2; exit 1; }

work=build/macos
app=$work/SimCruise.app
rm -rf "$work" && mkdir -p "$work"

"$bun" install --frozen-lockfile
"$bun" build --compile --minify --target=bun-darwin-arm64 --no-compile-autoload-dotenv --no-compile-autoload-bunfig \
  --no-compile-autoload-tsconfig --no-compile-autoload-package-json helper/atlas.mjs --outfile "$work/atlas"

# Export with Godot's own signing off (codesign/codesign=0) from a temporary
# copy of the presets; the original is put back however the export ends.
# Godot's "Built-in (ad-hoc only)" signing would be replaced anyway, because
# adding the helper and credits breaks the bundle seal, and it marks the engine
# for the hardened runtime, which the Mac build's signature does not use.
presets=game/export_presets.cfg
cp "$presets" "$work/export_presets.cfg.orig"
trap 'cp "$root/$work/export_presets.cfg.orig" "$root/$presets"' EXIT
awk '
  { sub(/\r$/, "") }
  /^\[preset\./ { section = $0 }
  section == "[preset.0.options]" && /^codesign\/codesign=/ { next }
  { print }
  $0 == "[preset.0.options]" { print "codesign/codesign=0" }
' "$work/export_presets.cfg.orig" > "$presets"
sed -n '/^\[preset.0\]/,/^\[preset.0.options\]/p' "$presets" | grep -q '^name="macOS"' || {
  echo "The macOS preset is not preset.0 in $presets; update this script." >&2; exit 1; }
"$godot" --headless --editor --path game --import
"$godot" --headless --path game --export-release macOS "$root/$app"
cp "$work/export_presets.cfg.orig" "$presets"
[ -f "$app/Contents/MacOS/SimCruise" ] || { echo "Godot export failed: no $app/Contents/MacOS/SimCruise" >&2; exit 1; }
"$bun" scripts/macos-bundle.mjs thin "$app/Contents/MacOS/SimCruise" arm64

# The game starts the helper from beside its own executable (Drive.gd).
cp "$work/atlas" "$app/Contents/MacOS/atlas"
mkdir -p "$app/Contents/Resources/Credits"
cp -R licenses/. "$app/Contents/Resources/Credits/"
cp Credits.txt "$app/Contents/Resources/Credits/Credits.txt"

"$rcodesign" sign "$app"
"$bun" scripts/macos-bundle.mjs verify "$app" arm64

zip_name=SimCruise-$version-macos-arm64.zip
rm -f "build/$zip_name"
"$bun" scripts/macos-bundle.mjs zip "$app" "build/$zip_name"
(cd build && sha256sum "$zip_name" > SHA256SUMS-macos.txt)
echo "Built build/$zip_name (not launched: that needs a Mac)"
