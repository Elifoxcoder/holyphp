#!/usr/bin/env bash
# make-appimage.sh — build HolyPHP-x86_64.AppImage.
#
#   bash packaging/linux/make-appimage.sh
#
# Requires: a Linux x86_64 machine (or WSL), internet access on first run
# (downloads appimagetool, cached in build/), and a repo-root `hphp` binary —
# built automatically if missing (packaging/linux/build-linux.sh).
#
# What ships inside:
#   AppRun                       launcher + `install` wizard mode
#   usr/bin/hphp                 the compiler (runtime + stdlib embedded)
#   hphp.desktop, hphp.png       desktop integration for AppImageLauncher
#
# NOT included: the lib/*.hphp libraries. ui/websocket go through the
# package manager (`hphp pkg install ui`).
set -euo pipefail
cd "$(dirname "$0")/../.."          # repo root

VERSION=1.0.0
ARCH=x86_64
APPDIR=packaging/linux/AppDir
OUT=packaging/linux/out/HolyPHP-$VERSION-$ARCH.AppImage
TOOL=build/appimagetool-$ARCH.AppImage

[ -x ./hphp ] || bash packaging/linux/build-linux.sh
[ -x ./hphp ] || { echo "error: Linux build failed"; exit 1; }

# fetch appimagetool once
if [ ! -x "$TOOL" ]; then
    mkdir -p build
    echo "downloading appimagetool (one-time, cached)..."
    curl -fsSL -o "$TOOL" \
      "https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-$ARCH.AppImage"
    chmod +x "$TOOL"
fi

# stage the AppDir
mkdir -p "$APPDIR/usr/bin"
cp hphp "$APPDIR/usr/bin/hphp"
chmod 755 "$APPDIR/AppRun" "$APPDIR/usr/bin/hphp"

# icon (any 256x256 png works; skip gracefully if absent)
if [ -f packaging/linux/hphp.png ]; then
    cp packaging/linux/hphp.png "$APPDIR/hphp.png"
    cp packaging/linux/hphp.png "$APPDIR/.DirIcon"
else
    echo "note: packaging/linux/hphp.png missing — AppImage will have no icon"
fi

mkdir -p "$(dirname "$OUT")"
# --appimage-extract-and-run so the tool works even on distros without FUSE
APPIMAGE_EXTRACT_AND_RUN=1 "$TOOL" --no-appstream "$APPDIR" "$OUT"

echo ""
echo "built: $OUT"
echo ""
echo "use it two ways:"
echo "  zero-install:   chmod +x $(basename "$OUT") && ./$(basename "$OUT") run hello.hphp"
echo "  install wizard: ./$(basename "$OUT") install      # adds to PATH, checks gcc,"
echo "                                                    # explains pkg-manager libs"
echo "  undo:           ./$(basename "$OUT") uninstall"
