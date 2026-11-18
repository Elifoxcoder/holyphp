#!/usr/bin/env bash
# Build HolyBrowser end to end: the compiler, the Chromium shim, the app.
#
#   bash holybrowser/build.sh            # everything
#   bash holybrowser/build.sh --app      # just rebuild the app
#
# Order matters. scripts/build.sh regenerates src/hphpc/runtime_embed.h from
# src/hphpc/runtime/*.c and links it into hphp.exe, so the app must be built
# with the hphp.exe that was just produced - an app linked against a stale
# runtime behaves like a bug that was already fixed.
#
# Kill any running holybrowser.exe first: the executable is locked while it is
# open, and hphp build will fail with "cannot place executable at ...".
set -euo pipefail
cd "$(dirname "$0")/.."

APP_ONLY=0
[ "${1:-}" = "--app" ] && APP_ONLY=1

if ! tasklist //FI "IMAGENAME eq holybrowser.exe" 2>/dev/null | grep -q holybrowser; then
  :
else
  echo "note: holybrowser.exe is running; kill it before building"
fi

if [ "$APP_ONLY" = "0" ]; then
  echo "== 1/3 compiler + embedded runtime =="
  bash scripts/build.sh

  echo "== 2/3 Chromium shim (MSVC) + CEF runtime =="
  # Driven through a batch file on purpose: MSYS turns /flags into paths.
  cmd //c "holybrowser\\build_cefshim.bat"
  grep -q "COMPILE_EXIT=0" <(cmd //c "holybrowser\\build_cefshim.bat") || {
    echo "cefshim.dll did not build"; exit 1; }
fi

echo "== 3/3 HolyBrowser =="
# --gui links the Windows GUI subsystem, so launching the exe does not leave a
# console window behind it.
./hphp.exe build holybrowser/holybrowser.hphp -o build/holybrowser.exe --gui

echo
echo "run it with:  ./build/holybrowser.exe"
echo "it needs build/cef_runtime/ beside it (or HOLYCEF_SHIM pointing there)."