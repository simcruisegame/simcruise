#!/bin/sh
# Print the CHANGELOG.md entry for one release, for the GitHub release notes.
#
#   sh scripts/release-notes.sh 1.1.0 [CHANGELOG]
#
# The entry is everything under the "## <name> 1.1.0 (date)" heading up to the
# next "## " heading, without the heading itself. Exits 1 when there is no such
# heading or the entry is empty, so a tag cannot be released without notes.
set -eu
[ $# -ge 1 ] || { echo "Usage: $0 VERSION [CHANGELOG]" >&2; exit 2; }
version=$1
changelog=${2:-$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)/CHANGELOG.md}
# Command substitution drops the trailing blank lines; sed drops the leading ones.
notes=$(awk -v v="$version" '
  /^## / {
    if (found) exit
    for (i = 2; i <= NF; i++) if ($i == v) { found = 1; next }
    next
  }
  found { print }
' "$changelog" | sed -e '/./,$!d')
if [ -z "$(printf '%s' "$notes" | tr -d '[:space:]')" ]; then
  echo "CHANGELOG has no entry for $version: add a '## SimCruise $version (YYYY-MM-DD)' section." >&2
  exit 1
fi
printf '%s\n' "$notes"
