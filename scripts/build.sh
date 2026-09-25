#!/usr/bin/env bash
# build.sh — build the hphp toolchain binary.
#
# 1. regenerates the embedded runtime header (scripts/genembed.c)
# 2. compiles compiler objects with gcc -c (rock solid)
# 3. links with ld directly — the gcc driver's collect2 intermittently fails
#    on this machine (on-access AV scanners); manual ld always succeeds.
set -u
cd "$(dirname "$0")/.."

CC=${CC:-gcc}
CFLAGS="-O2 -std=c11 -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable"
OUT=hphp.exe

# 1. embed the runtime sources into the compiler binary
$CC -O2 -std=c11 scripts/genembed.c -o build/genembed.exe 2>/dev/null || \
  $CC -O2 -std=c11 scripts/genembed.c -o build/genembed
mkdir -p src/hphpc
# bundled libraries: everything in lib/ ships inside the compiler, so an
# installed hphp has its stdlib without any files on disk
libs=(lib/*.hphp)
[ -e "${libs[0]}" ] || libs=()
./build/genembed src/hphpc/runtime_embed.h src/hphpc/runtime "${libs[@]}"

GCCLIB=$(gcc -print-file-name=libgcc.a | xargs dirname)
GCCDIR=$(gcc -print-file-name=crtbegin.o | xargs dirname)
CRTDIR=$(dirname "$(gcc -print-file-name=libmingwex.a)")

# 2. compile objects
mkdir -p build/obj
objs=()
rc=0
for src in src/hphpc/*.c; do
  obj="build/obj/$(basename "${src%.c}").o"
  if ! $CC $CFLAGS -Isrc/hphpc -c "$src" -o "$obj"; then rc=1; fi
  objs+=("$obj")
done
[ $rc -eq 0 ] || { echo "compile failed"; exit 1; }

# 3. link
rm -f "$OUT"
ld -m i386pep -Bdynamic -o "$OUT" \
  "$CRTDIR/crt2.o" "$GCCDIR/crtbegin.o" \
  -L"$GCCDIR" -L"$CRTDIR" \
  "${objs[@]}" \
  -lmingw32 -lgcc -lgcc_eh -lmingwex -lmsvcrt -lkernel32 -lpthread -ladvapi32 -lshell32 -luser32 -lws2_32 \
  "$CRTDIR/default-manifest.o" "$GCCDIR/crtend.o" || { echo "link failed"; exit 1; }

sig=$(head -c 2 "$OUT" 2>/dev/null | xxd -p)
[ "$sig" = "4d5a" ] || { echo "output invalid"; exit 1; }
echo "build ok: $OUT ($(stat -c%s "$OUT") bytes, runtime embedded)"
