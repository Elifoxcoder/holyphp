#!/usr/bin/env bash
# make-deb.sh — build holyphp_<version>_amd64.deb.
#
#   bash packaging/linux/make-deb.sh
#
# Requires: a Linux machine (or WSL) with dpkg-deb, and a repo-root `hphp`
# binary — built automatically if missing (packaging/linux/build-linux.sh;
# `scripts/build.sh` is the Windows/MinGW path and cannot run on Linux).
#
# What the package installs:
#   /usr/bin/hphp                        the compiler (on PATH by default)
#   /usr/lib/holyphp/common-install-wizard.sh   helper for postinst
#   /usr/share/doc/holyphp/README
#
# It does NOT install the lib/*.hphp libraries — ui and websocket are
# distributed through the package manager (`hphp pkg install ui`).
set -euo pipefail
cd "$(dirname "$0")/../.."          # repo root

VERSION=1.0.0
STAGE=packaging/linux/deb-stage
OUT=packaging/linux/out/holyphp_${VERSION}_amd64.deb

[ -x ./hphp ] || bash packaging/linux/build-linux.sh
[ -x ./hphp ] || { echo "error: Linux build failed"; exit 1; }
command -v dpkg-deb >/dev/null || { echo "error: dpkg-deb not found (need Debian/Ubuntu/WSL)"; exit 1; }

rm -rf "$STAGE"
mkdir -p "$STAGE/DEBIAN" \
         "$STAGE/usr/bin" \
         "$STAGE/usr/lib/holyphp" \
         "$STAGE/usr/share/doc/holyphp"

cp hphp "$STAGE/usr/bin/hphp"
cp packaging/linux/common-install-wizard.sh "$STAGE/usr/lib/holyphp/"
cp README.md "$STAGE/usr/share/doc/holyphp/README"

# maintainer scripts must be executable
chmod 755 "$STAGE/DEBIAN/postinst" "$STAGE/DEBIAN/prerm"
chmod 755 "$STAGE/usr/bin/hphp" "$STAGE/usr/lib/holyphp/common-install-wizard.sh"

# keep Installed-Size in control roughly honest
size_kb=$(du -sk "$STAGE/usr" | cut -f1)
sed -i "s/^Installed-Size: .*/Installed-Size: $size_kb/" "$STAGE/DEBIAN/control"

mkdir -p "$(dirname "$OUT")"
dpkg-deb --build --root-owner-group "$STAGE" "$OUT"

echo ""
echo "built: $OUT"
echo "install it with:   sudo apt install ./$(basename "$OUT")"
echo "  (apt runs the postinst wizard: binary check, gcc check, pkg-manager hints)"
echo "then:              hphp run hello.hphp"
