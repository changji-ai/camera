#!/usr/bin/env bash
# Package the macOS build: macdeployqt, ad-hoc codesign, zip.
# Usage: package_macos.sh <build-dir> <version> <arch>
set -euo pipefail

BUILD_DIR="$1"
VERSION="$2"
ARCH="$3"

APP="$BUILD_DIR/bin/camera.app"
STAGE="$BUILD_DIR/package"
OUT="$STAGE/camera-${VERSION}-macos-${ARCH}.zip"

rm -rf "$STAGE"
mkdir -p "$STAGE"

macdeployqt "$APP" -always-overwrite

# onnxruntime dylib -> Frameworks, always under the canonical name that the
# binary's LC_LOAD_DYLIB entry references (@rpath/libonnxruntime.dylib)
ORT_LIB=$(find "$BUILD_DIR/deps/onnxruntime" -name 'libonnxruntime*.dylib' ! -name '*deven*' | head -1)
cp "$ORT_LIB" "$APP/Contents/Frameworks/libonnxruntime.dylib"

# bundled model -> Resources
if [ -d models ]; then
    mkdir -p "$APP/Contents/Resources/models"
    cp models/*.onnx "$APP/Contents/Resources/models/" 2>/dev/null || true
fi

# ad-hoc signature so the app runs without extra Gatekeeper friction locally
codesign --force --deep -s - "$APP"

ditto -c -k --keepParent "$APP" "$OUT"
echo "ARTIFACT=$OUT"
