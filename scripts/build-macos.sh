#!/bin/sh
# Run from a checkout of the complete source distribution on an Apple Silicon Mac.
set -eu
root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"
mode=${1:-app}
case "$mode" in editor|app|dmg) ;; *) echo "Usage: $0 [editor|app|dmg [version]]" >&2; exit 2;; esac
if [ "$(uname -s)" != Darwin ] || [ "$(uname -m)" != arm64 ]; then
  echo "This build targets Apple Silicon macOS. Use a native arm64 terminal and Bun." >&2
  exit 1
fi
godot=${GODOT_BIN:-godot}
bun_runtime=${BUN_BIN:-bun}
if ! command -v "$godot" >/dev/null 2>&1 && [ "$godot" = godot ]; then
  godot=/Applications/Godot.app/Contents/MacOS/Godot
fi
if ! command -v "$godot" >/dev/null 2>&1 || ! command -v "$bun_runtime" >/dev/null 2>&1; then
  echo "Set GODOT_BIN and BUN_BIN to the installed Godot and Bun executables." >&2
  exit 1
fi
case "$("$godot" --version)" in 4.7.2.stable*) ;; *) echo "Use Godot 4.7.2 stable and its matching export templates." >&2; exit 1;; esac
if [ "$("$bun_runtime" -p 'process.arch')" != arm64 ]; then
  echo "Install the Apple Silicon version of Bun." >&2
  exit 1
fi

"$bun_runtime" install --frozen-lockfile
mkdir -p tools
"$bun_runtime" build --compile --minify --no-compile-autoload-dotenv --no-compile-autoload-bunfig \
  --no-compile-autoload-tsconfig --no-compile-autoload-package-json helper/atlas.mjs --outfile tools/atlas
"$godot" --headless --editor --path game --import
if [ "$mode" = editor ]; then
  echo "Editor project ready: $root/game/project.godot"
  exit 0
fi

mkdir -p build
stage=$(mktemp -d "$root/build/.package.XXXXXX")
target="$root/build/SimCruise.app"
trap 'rm -rf "$stage"' EXIT
app="$stage/SimCruise.app"
"$godot" --headless --path game --export-release macOS "$app"
engine="$app/Contents/MacOS/SimCruise"
/usr/bin/lipo "$engine" -thin arm64 -output "$stage/engine-arm64"
/bin/mv "$stage/engine-arm64" "$engine"
/bin/chmod +x "$engine"
/usr/bin/install -m 755 tools/atlas "$app/Contents/MacOS/atlas"
/usr/bin/ditto --norsrc --noextattr licenses "$app/Contents/Resources/Credits"
/usr/bin/codesign --force --deep --sign - "$app"
/usr/bin/codesign --verify --deep --strict "$app"
if [ -e "$target" ]; then /bin/mv "$target" "$stage/previous.app"; fi
if ! /bin/mv "$app" "$target"; then
  if [ -d "$stage/previous.app" ]; then /bin/mv "$stage/previous.app" "$target"; fi
  exit 1
fi
echo "Built $target"

# Optional release disk image. Only the "dmg" mode reaches this step; "editor"
# exits above and "app" stops at the bundle. An optional second argument names
# the version, otherwise make-dmg.sh reads it from the project files.
if [ "$mode" = dmg ]; then
  sh "$root/scripts/make-dmg.sh" "${2:-}" "$target"
fi
