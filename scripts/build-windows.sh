#!/bin/sh
# Build the Windows release on Linux, macOS or Windows (Git Bash):
#
#   sh scripts/build-windows.sh            # version from config/version
#   sh scripts/build-windows.sh 1.1.0      # explicit version
#
# Produces in build/:
#   SimCruise-VERSION-windows-x64-setup.exe   installer (NSIS)
#   SimCruise-VERSION-windows-x64.zip         portable folder
#   SHA256SUMS-windows.txt
# Needs Godot 4.7.2 with its export templates (GODOT_BIN), Bun (BUN_BIN; Bun
# cross-compiles atlas.exe) and NSIS 3 (MAKENSIS, else makensis on PATH, else
# the NSIS installer's default folder on Windows; `apt install nsis`, `brew
# install makensis` or the Windows installer from nsis.sourceforge.io). The
# portable zip is written with zip, else Python 3, else Windows' own tar.exe.
# `sh scripts/install-godot.sh DIR windows` fetches Godot and the templates.
set -eu
root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"
godot=${GODOT_BIN:-godot}
bun=${BUN_BIN:-bun}
if ! command -v "$bun" >/dev/null 2>&1 && [ "$bun" = bun ] && [ -x "$HOME/.bun/bin/bun" ]; then bun=$HOME/.bun/bin/bun; fi
makensis=${MAKENSIS:-makensis}
# The NSIS installer for Windows does not add itself to PATH.
for dir in "/c/Program Files (x86)/NSIS" "/c/Program Files/NSIS"; do
  if ! command -v "$makensis" >/dev/null 2>&1 && [ "$makensis" = makensis ] && [ -x "$dir/makensis.exe" ]; then
    makensis=$dir/makensis.exe
  fi
done
for tool in "$godot" "$bun" "$makensis"; do
  command -v "$tool" >/dev/null 2>&1 || { echo "Missing $tool (see the header of this script)." >&2; exit 1; }
done
case "$("$godot" --version)" in 4.7.2.stable*) ;; *) echo "Use Godot 4.7.2 stable and its export templates." >&2; exit 1;; esac

version=${1:-$(sed -n 's/^config\/version="\(.*\)"/\1/p' game/project.godot)}
case "$version" in [0-9]*.[0-9]*.[0-9]*) ;; *) echo "Version '$version' is not X.Y.Z" >&2; exit 1;; esac
stage=build/windows/stage
rm -rf "$stage" && mkdir -p "$stage/Credits"

"$bun" install --frozen-lockfile
"$bun" build --compile --minify --target=bun-windows-x64 --no-compile-autoload-dotenv --no-compile-autoload-bunfig \
  --no-compile-autoload-tsconfig --no-compile-autoload-package-json helper/atlas.mjs --outfile "$stage/atlas.exe"
"$godot" --headless --editor --path game --import
"$godot" --headless --path game --export-release "Windows Desktop" "$root/$stage/SimCruise.exe"
[ -s "$stage/SimCruise.exe" ] || { echo "Godot export failed" >&2; exit 1; }

cp -R licenses/. "$stage/Credits/"
cp Credits.txt "$stage/Credits/Credits.txt"
# Notepad-friendly copies with CRLF line endings.
sed 's/$/\r/' LICENSE > "$stage/LICENSE.txt"
sed 's/$/\r/' > "$stage/README.txt" <<EOF
SimCruise $version for Windows

Installed: start SimCruise from the Start menu or the desktop shortcut.
Portable zip: run SimCruise.exe in this folder (keep atlas.exe beside it).
Press / to search for a place, Tab for the road atlas, C to change camera.
Controls and help: https://github.com/simcruisegame/simcruise#how-to-play

Windows may show "Windows protected your PC" the first time, because this free
open-source build is not code-signed. Click "More info", then "Run anyway".

The map cache and settings are in %APPDATA%\\Godot\\app_userdata\\SimCruise.
Licences for the code, map data, imagery and assets are in the Credits folder.
EOF

out="$root/build/SimCruise-$version-windows-x64-setup.exe"
"$makensis" -V2 -DVERSION="$version" -DSTAGE="$root/$stage" -DOUTFILE="$out" packaging/windows/simcruise.nsi
make_zip() { # ZIP DIR
  if command -v zip >/dev/null 2>&1; then zip -qr "$1" "$2"; return; fi
  for py in python3 python; do
    # Windows' "python3" may be a Store alias that runs nothing; the import proves a real one.
    if "$py" -c 'import zipfile' >/dev/null 2>&1; then "$py" -m zipfile -c "$1" "$2"; return; fi
  done
  # Windows 10 and later ship libarchive's bsdtar, which writes zip archives
  # (Git Bash's own tar is GNU tar, which cannot).
  for wintar in "${SYSTEMROOT:-C:/Windows}/System32/tar.exe" /c/Windows/System32/tar.exe; do
    if [ -x "$wintar" ]; then "$wintar" --format zip -cf "$1" "$2"; return; fi
  done
  echo "Need zip, Python 3 or Windows tar.exe to write the portable zip." >&2; return 1
}
zip_name="SimCruise-$version-windows-x64.zip"
rm -f "build/$zip_name"
(cd build/windows && rm -rf "SimCruise-$version" && cp -R stage "SimCruise-$version" && \
  make_zip "../$zip_name" "SimCruise-$version" && rm -rf "SimCruise-$version")
(cd build && sha256sum "SimCruise-$version-windows-x64-setup.exe" "$zip_name" > SHA256SUMS-windows.txt 2>/dev/null || \
  shasum -a 256 "SimCruise-$version-windows-x64-setup.exe" "$zip_name" > SHA256SUMS-windows.txt)
echo "Built build/SimCruise-$version-windows-x64-setup.exe and build/$zip_name"
