#!/bin/sh
# Download Godot 4.7.2 stable, and optionally its export templates, from the
# official release at github.com/godotengine/godot and install them where Godot
# looks. Every download is checked against the release's SHA512-SUMS.txt.
#
#   sh scripts/install-godot.sh DIR                  # editor only (enough for scripts/check.sh)
#   sh scripts/install-godot.sh DIR windows          # + the Windows x86_64 release templates
#   sh scripts/install-godot.sh DIR macos            # + the macOS template (macos.zip)
#   sh scripts/install-godot.sh DIR all              # + every template in the archive
#
# DIR receives the editor (and a downloads/ folder that can be deleted). The
# last line printed is the editor executable to use as GODOT_BIN. Templates go
# to the user's export_templates/4.7.2.stable folder:
#   Windows  %APPDATA%\Godot\export_templates\4.7.2.stable
#   macOS    ~/Library/Application Support/Godot/export_templates/4.7.2.stable
#   Linux    ${XDG_DATA_HOME:-~/.local/share}/godot/export_templates/4.7.2.stable
# The template archive is 1.3 GB; only the files the named platforms need are
# unpacked from it. Files already in place are kept.
#
# Environment: GODOT_RELEASE_URL overrides the download base (for a mirror or a
# test); GODOT_TEMPLATES_DIR overrides the template folder.
set -eu
godot_version=4.7.2
tag=$godot_version-stable
base=${GODOT_RELEASE_URL:-https://github.com/godotengine/godot/releases/download/$tag}
[ $# -ge 1 ] || { echo "Usage: $0 DIR [windows|macos|linux|all]..." >&2; exit 2; }
dest=$1; shift
mkdir -p "$dest/downloads"
dest=$(CDPATH='' cd -- "$dest" && pwd)
dl=$dest/downloads

case "$(uname -s)" in
  Linux)
    editor_zip=Godot_v${tag}_linux.x86_64.zip
    editor=$dest/Godot_v${tag}_linux.x86_64
    templates_default=${XDG_DATA_HOME:-$HOME/.local/share}/godot/export_templates/$godot_version.stable;;
  Darwin)
    editor_zip=Godot_v${tag}_macos.universal.zip
    editor=$dest/Godot.app/Contents/MacOS/Godot
    templates_default="$HOME/Library/Application Support/Godot/export_templates/$godot_version.stable";;
  MINGW*|MSYS*|CYGWIN*)
    editor_zip=Godot_v${tag}_win64.exe.zip
    editor=$dest/Godot_v${tag}_win64_console.exe
    appdata=${APPDATA:?APPDATA is not set}
    if command -v cygpath >/dev/null 2>&1; then appdata=$(cygpath -u "$appdata"); fi
    templates_default=$appdata/Godot/export_templates/$godot_version.stable;;
  *) echo "Unsupported system $(uname -s)." >&2; exit 1;;
esac
templates=${GODOT_TEMPLATES_DIR:-$templates_default}

if command -v sha512sum >/dev/null 2>&1; then sha512() { sha512sum "$1" | cut -d' ' -f1; }
else sha512() { shasum -a 512 "$1" | cut -d' ' -f1; }; fi

sums=$dl/SHA512-SUMS.txt
fetch() { # NAME: download into $dl and verify against SHA512-SUMS.txt
  if [ ! -s "$sums" ]; then
    curl -fsSL --retry 3 --retry-delay 5 -o "$sums.part" "$base/SHA512-SUMS.txt" && mv "$sums.part" "$sums"
  fi
  want=$(awk -v n="$1" '{ f=$NF; sub(/^\*/, "", f) } f==n { print $1; exit }' "$sums")
  [ -n "$want" ] || { echo "$1 is not listed in SHA512-SUMS.txt of $tag." >&2; exit 1; }
  if [ ! -f "$dl/$1" ] || [ "$(sha512 "$dl/$1")" != "$want" ]; then
    echo "Downloading $1 ..." >&2
    curl -fL --retry 3 --retry-delay 5 --progress-bar -o "$dl/$1.part" "$base/$1"
    mv "$dl/$1.part" "$dl/$1"
  fi
  [ "$(sha512 "$dl/$1")" = "$want" ] || { echo "SHA-512 of $1 does not match the release." >&2; rm -f "$dl/$1"; exit 1; }
}

if [ ! -x "$editor" ]; then
  fetch "$editor_zip"
  unzip -q -o "$dl/$editor_zip" -d "$dest"
  chmod +x "$editor" 2>/dev/null || true
fi
[ -f "$editor" ] || { echo "The editor archive did not contain $editor." >&2; exit 1; }

# Per platform: the files an export needs (Godot refuses a release export
# unless the debug template is there too) and the archive members to unpack
# (the same plus the Windows console wrappers).
required=; patterns=
for platform in "$@"; do
  case "$platform" in
    windows) required="$required windows_release_x86_64.exe windows_debug_x86_64.exe"
             pattern='templates/windows_*_x86_64*.exe';;
    macos) required="$required macos.zip"; pattern=templates/macos.zip;;
    linux) required="$required linux_release.x86_64 linux_debug.x86_64"; pattern='templates/linux_*.x86_64';;
    all) required="$required version.txt"; pattern='templates/*';;
    *) echo "Unknown template platform '$platform' (windows, macos, linux or all)." >&2; exit 2;;
  esac
  # unzip fails (exit 11) when one member matches two patterns, so name each once.
  case "$patterns" in *"templates/*"*) ;; *"$pattern"*) ;; *) patterns="$patterns $pattern";; esac
  if [ "$pattern" = 'templates/*' ]; then patterns=$pattern; fi
done
if [ -n "$required" ]; then
  missing=
  for m in $required version.txt; do [ -s "$templates/$m" ] || missing="$missing $m"; done
  case " $* " in *" all "*) missing=all;; esac
  [ "$patterns" = 'templates/*' ] || patterns="$patterns templates/version.txt"
  if [ -n "$missing" ]; then
    tpz=Godot_v${tag}_export_templates.tpz
    fetch "$tpz"
    mkdir -p "$templates"
    # set -f keeps the shell from expanding the patterns; unzip matches them.
    set -f
    # shellcheck disable=SC2086 # one pattern per word
    unzip -q -o -j "$dl/$tpz" $patterns -d "$templates"
    set +f
    for m in $required; do
      [ -s "$templates/$m" ] || { echo "The template archive has no $m." >&2; exit 1; }
    done
  fi
  case "$(cat "$templates/version.txt" 2>/dev/null)" in
    "$godot_version.stable"*) ;;
    *) echo "$templates/version.txt does not say $godot_version.stable." >&2; exit 1;;
  esac
  echo "Export templates: $templates" >&2
fi
echo "$editor"
