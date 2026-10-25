#!/usr/bin/env bash
# local test harness for tools/bump_version.sh - exercises the version-decision
# logic in a scratch git repo. Run from anywhere; cleans up after itself.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

mkdir -p "$TMP/tools"
cp "$ROOT/tools/bump_version.sh" "$TMP/tools/"
cp "$ROOT/CMakeLists.txt" "$TMP/"

cd "$TMP" || exit 1
git init -q -b main .
git add -A
git -c user.name=t -c user.email=t@t commit -qm base

fail=0
check() { # name expected got
  if [ "$2" = "$3" ]; then echo "  PASS $1"; else echo "  FAIL $1: expected [$2] got [$3]"; fail=1; fi
}

echo "--- case1: no tags yet"
out=$(bash tools/bump_version.sh)
USE_VERSION= NEED_COMMIT=
eval "$out"
check "USE_VERSION" "1.0.0" "${USE_VERSION:-}"
check "NEED_COMMIT" "0" "${NEED_COMMIT:-}"

git tag v1.0.0
echo "--- case2: tag == cmake version"
out=$(bash tools/bump_version.sh)
USE_VERSION= NEED_COMMIT=
eval "$out"
check "USE_VERSION" "1.0.1" "${USE_VERSION:-}"
check "NEED_COMMIT" "1" "${NEED_COMMIT:-}"
check "cmakelists rewritten" "1" "$(grep -c 'VERSION 1.0.1' CMakeLists.txt)"

git -c user.name=t -c user.email=t@t commit -qam bump
git tag v1.0.1
echo "--- case3: cmk == latest tag again"
out=$(bash tools/bump_version.sh)
USE_VERSION= NEED_COMMIT=
eval "$out"
check "USE_VERSION" "1.0.2" "${USE_VERSION:-}"
check "NEED_COMMIT" "1" "${NEED_COMMIT:-}"

# restore an ahead-of-tags version in the file -> manual-bump path
sed -i.bak -E "s/^(project\([^)]*VERSION[[:space:]]+)[0-9]+(\.[0-9]+)+(.*)/\1VERSION 2.0.0\3/" CMakeLists.txt
rm -f CMakeLists.txt.bak
echo "--- case4: cmake version ahead of tags"
out=$(bash tools/bump_version.sh)
USE_VERSION= NEED_COMMIT=
eval "$out"
check "USE_VERSION" "2.0.0" "${USE_VERSION:-}"
check "NEED_COMMIT" "0" "${NEED_COMMIT:-}"

exit $fail
