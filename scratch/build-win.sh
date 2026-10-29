#!/usr/bin/env bash
# Build hphp.exe with the native MinGW toolchain (Windows target).
set -e
cd "$(dirname "$0")/.."
CC=C:/msys64/ucrt64/bin/gcc.exe
COMMON="-O2 -std=c11 -Isrc/hphpc"
mkdir -p build/obj
for src in src/hphpc/*.c; do
  base=$(basename "$src" .c)
  "$CC" $COMMON -c "$src" -o "build/obj/$base.o"
done
"$CC" -O2 build/obj/*.o -o hphp.exe \
  -lm -lws2_32 -lgdi32 -lgdiplus -lcomctl32 -lcomdlg32 -lole32 -loleaut32 \
  -luuid -lversion -lshlwapi -lwinmm -lkernel32 -lpthread -ladvapi32 \
  -lshell32 -luser32 -lmingw32
echo "build ok: hphp.exe"