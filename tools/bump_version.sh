#!/usr/bin/env bash
# Decide which version this release pipeline should use.
#
# Reads the version from CMakeLists.txt (project(camera VERSION x.y.z)) and
# compares it with the latest v* tag.
#   - CMake version differs from the latest tag (or no tag yet): use the CMake
#     version as-is (maintainer bumped it manually) -> NEED_COMMIT=0
#   - CMake version equals the latest tag: bump patch by 1 and write it back
#     to CMakeLists.txt -> NEED_COMMIT=1 (caller commits with [skip ci])
#
# Prints machine readable lines:
#   USE_VERSION=1.2.3
#   NEED_COMMIT=0|1
set -euo pipefail

CMAKE_FILE="${1:-CMakeLists.txt}"

cur=$(sed -nE 's/^project\([A-Za-z0-9_-+[[:space:]]+VERSION[[:space:]]+([0-9]+(\.[0-9]+)+).*/\1/p' "$CMAKE_FILE" | head -1)
if [ -z "$cur" ]; then
    echo "cannot parse project VERSION from $CMAKE_FILE" >&2
    exit 1
fi

latest=$(git tag --list 'v*' --sort=-v:refname | head -1 || true)
latest="${latest#v}"

if [ -z "$latest" ] || [ "$cur" != "$latest" ]; then
    echo "USE_VERSION=$cur"
    echo "NEED_COMMIT=0"
    exit 0
fi

IFS='.' read -r a b c <<<"$cur"
next="$a.$b.$((c + 1))"
sed -i.bak -E "s/^(project\\([A-Za-z0-9_-+[[:space:]]+VERSION[[:space:]]+)[0-9]+(\\.[0-9]+)+(.*)/\\1${next}\\3/" "$CMAKE_FILE"
rm -f "$CMAKE_FILE.bak"

grep -q "VERSION $next" "$CMAKE_FILE" || { echo "failed to write back version" >&2; exit 1; }

echo "USE_VERSION=$next"
echo "NEED_COMMIT=1"
