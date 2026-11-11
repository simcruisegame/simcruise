#!/bin/sh
# Build every release file on one Windows PC (Git Bash) and print the commands
# that put them in a draft GitHub release. Nothing is uploaded or tagged.
#
#   sh scripts/release-local.sh            # version from config/version
#   sh scripts/release-local.sh 1.2.0      # explicit version (must match config/version)
#
# 1. checks the version against game/project.godot and that CHANGELOG.md has a
#    "## SimCruise X.Y.Z (date)" entry (scripts/release-notes.sh);
# 2. Windows: scripts/build-windows.sh (installer, portable zip), then
#    scripts/smoke-windows.sh starts the portable build headless;
# 3. macOS: scripts/cross-build-macos.sh (SimCruise.app for Apple Silicon,
#    signed ad hoc, checked statically, zipped);
# 4. writes build/SHA256SUMS.txt for the three files and
#    build/release-notes-X.Y.Z.md (the changelog entry with its links pointed
#    at the tag, a download list, first-launch notes and the checksums);
# 5. prints the git tag and `gh release create --draft` commands to run.
#
# Needs what those scripts need: Godot 4.7.2 with the Windows and macOS export
# templates (GODOT_BIN; `sh scripts/install-godot.sh DIR windows macos`), Bun
# and NSIS 3. GH_REPO overrides the repository used in links (default
# simcruisegame/simcruise).
set -eu
root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"
project_version=$(sed -n 's/^config\/version="\(.*\)"/\1/p' game/project.godot)
version=${1:-$project_version}
case "$version" in [0-9]*.[0-9]*.[0-9]*) ;; *) echo "Version '$version' is not X.Y.Z" >&2; exit 1;; esac
[ "$version" = "$project_version" ] || {
  echo "config/version in game/project.godot is $project_version, not $version; set it first." >&2; exit 1; }
tag=v$version
repo=${GH_REPO:-simcruisegame/simcruise}
changelog=$(sh scripts/release-notes.sh "$version")
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) windows=1;; *) windows=0;; esac

sh scripts/build-windows.sh "$version"
if [ "$windows" = 1 ]; then
  sh scripts/smoke-windows.sh "build/SimCruise-$version-windows-x64.zip"
else
  echo "Not on Windows: skipping scripts/smoke-windows.sh; start the build on Windows before publishing." >&2
fi
sh scripts/cross-build-macos.sh "$version"

mac=SimCruise-$version-macos-arm64.zip
setup=SimCruise-$version-windows-x64-setup.exe
portable=SimCruise-$version-windows-x64.zip
(
  cd build
  sha256sum -c --quiet SHA256SUMS-windows.txt SHA256SUMS-macos.txt
  # Git Bash's sha256sum marks files "*" (binary); the plain form reads the same everywhere.
  sha256sum "$mac" "$setup" "$portable" | sed 's/ \*/  /' > SHA256SUMS.txt
)

notes=build/release-notes-$version.md
{
  # Links in CHANGELOG.md are relative to the repository root; on the release
  # page they would resolve under /releases/tag/, so point them at the tag.
  printf '%s\n' "$changelog" | sed -E "s#\]\(([^)#:/][^):]*)\)#](https://github.com/$repo/blob/$tag/\1)#g"
  printf '\n## Downloads\n\n'
  printf -- '- **macOS** (Apple Silicon, macOS 13 or newer): `%s`. Double-click to unzip, drag **SimCruise** to Applications, then Control-click it and choose **Open** the first time.\n' "$mac"
  printf -- '- **Windows** 10/11 64-bit: `%s` (installer) or `%s` (portable: unzip and run `SimCruise.exe`).\n' "$setup" "$portable"
  printf '\nThe Mac app is signed ad hoc but not notarized, and the Windows build is not code-signed, so macOS Gatekeeper and Windows SmartScreen ask once before the first launch; the steps are in [docs/install.md](https://github.com/%s/blob/%s/docs/install.md).\n' "$repo" "$tag"
  printf '\n### SHA-256\n\n```\n'
  cat build/SHA256SUMS.txt
  printf '```\n'
} > "$notes"

commit=$(git rev-parse HEAD)
echo
echo "Release files in build/:"
for f in "$mac" "$setup" "$portable" SHA256SUMS.txt; do
  printf '  %-44s %s bytes\n' "$f" "$(wc -c < "build/$f" | tr -d ' ')"
done
echo "Notes: $notes"
# (git diff compares content, so the Godot import's line-ending rewrites of .import files do not count.)
if ! git diff --quiet HEAD --; then
  echo "Warning: the working tree has uncommitted changes; the tag below would not match what was built." >&2
fi
cat <<EOF

Nothing has been uploaded. To make a draft release of commit $commit:

  git tag $tag $commit && git push origin $tag
  gh release create $tag --repo $repo --draft --verify-tag --title "SimCruise $version" \\
    --notes-file $notes \\
    build/$mac build/$setup build/$portable build/SHA256SUMS.txt

Then open the draft on https://github.com/$repo/releases, try the downloads, and press Publish release.
EOF
