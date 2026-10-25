#!/usr/bin/env bash
# Package the Windows build: run windeployqt, collect runtime deps, produce zip.
# Usage: package_windows.sh <build-dir> <version> <arch> [qt-bin-dir]
set -euo pipefail

BUILD_DIR="$1"
VERSION="$2"
ARCH="$3"

BIN="$BUILD_DIR/bin"
STAGE="$BUILD_DIR/package/camera"
OUT="$BUILD_DIR/package/camera-${VERSION}-windows-${ARCH}.zip"

rm -rf "$STAGE"
mkdir -p "$STAGE"

cp "$BIN/camera.exe" "$STAGE/"

windeployqt --release --compiler-runtime --no-translations "$STAGE/camera.exe"

# onnxruntime dll lives in the build deps tree
ORT_DLL=$(find "$BUILD_DIR/deps/onnxruntime" -name 'onnxruntime.dll' | head -1)
cp "$ORT_DLL" "$STAGE/"

# bundled model
if [ -d models ]; then
    mkdir -p "$STAGE/models"
    cp models/*.onnx "$STAGE/models/" 2>/dev/null || true
fi

(cd "$BUILD_DIR/package" && 7z a -tzip "camera-${VERSION}-windows-${ARCH}.zip" camera > /dev/null)
echo "ARTIFACT=$OUT"
