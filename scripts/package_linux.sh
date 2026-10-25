#!/usr/bin/env bash
# Package the Linux build: collect shared-library deps recursively, copy Qt
# plugins, emit a launcher script and a tar.gz.
# Usage: package_linux.sh <build-dir> <version> <arch>
set -euo pipefail

BUILD_DIR="$1"
VERSION="$2"
ARCH="$3"

STAGE="$BUILD_DIR/package/camera"
OUT="$BUILD_DIR/package/camera-${VERSION}-linux-${ARCH}.tar.gz"

rm -rf "$STAGE" "$OUT"
mkdir -p "$STAGE/bin" "$STAGE/lib" "$STAGE/models"

cp "$BUILD_DIR/bin/camera" "$STAGE/bin/camera"
if compgen -G 'models/*.onnx' > /dev/null; then
    cp models/*.onnx "$STAGE/models/"
fi

QT_PLUGINS=$(qmake6 -query QT_INSTALL_PLUGINS 2>/dev/null || qmake -query QT_INSTALL_PLUGINS)
echo "Qt plugins at: $QT_PLUGINS"

# --- copy needed Qt plugin sets (keeping their subdirectory layout)
for sub in platforms imageformats iconengines multimedia networkinformation xcbglintegrations; do
    if [ -d "$QT_PLUGINS/$sub" ]; then
        mkdir -p "$STAGE/plugins/$sub"
        cp -L "$QT_PLUGINS/$sub/"*.so "$STAGE/plugins/$sub/" 2>/dev/null || true
    fi
done

# --- recursively collect NEEDED libraries into lib/
SEEN="$BUILD_DIR/package/.deps-seen"
: > "$SEEN"

collect_deps() {
    local file="$1"
    ldd "$file" 2>/dev/null | awk '/=> \// {print $3} /^\// {print $1}' | sort -u | while read -r dep; do
        [ -f "$dep" ] || continue
        local bn
        bn=$(basename "$dep")
        # skip glibc core - use the system runtime
        case "$bn" in
            ld-linux*|libc.so*|libm.so*|libpthread*|libdl*|librt*|libresolv*|libnss*) continue ;;
        esac
        if grep -Fxq "$bn" "$SEEN"; then continue; fi
        echo "$bn" >> "$SEEN"
        cp -L "$dep" "$STAGE/lib/"
        collect_deps "$dep"
    done
}

collect_deps "$STAGE/bin/camera"
for so in "$STAGE"/plugins/*/*.so; do
    [ -f "$so" ] && collect_deps "$so"
done
echo "collected $(ls -1 "$STAGE/lib" | wc -l) libraries"

# --- launcher
cat > "$STAGE/run.sh" <<'EOS'
#!/usr/bin/env bash
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export LD_LIBRARY_PATH="$DIR/lib:$LD_LIBRARY_PATH"
export QT_PLUGIN_PATH="$DIR/plugins"
export QT_QPA_PLATFORM_PLUGIN_PATH="$DIR/plugins/platforms"
exec "$DIR/bin/camera" "$@"
EOS
chmod +x "$STAGE/run.sh" "$STAGE/bin/camera"

tar -czf "$OUT" -C "$BUILD_DIR/package" camera
echo "ARTIFACT=$OUT"
