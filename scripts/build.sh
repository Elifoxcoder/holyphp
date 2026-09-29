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
# (name differs per platform: gcc on MinGW auto-appends .exe, Linux does not;
#  failure is fatal — a stale embed silently breaks the produced binary)
GE=build/genembed
[[ "$(uname -s)" == MINGW* || "$(uname -s)" == *NT* ]] && GE=build/genembed.exe
mkdir -p build
rm -f build/genembed build/genembed.exe
$CC -O2 -std=c11 scripts/genembed.c -o "$GE" || { echo "genembed build failed"; exit 1; }
mkdir -p src/hphpc
# bundled libraries: everything in lib/ ships inside the compiler, so an
# installed hphp has its stdlib without any files on disk
libs=(lib/*.hphp)
[ -e "${libs[0]}" ] || libs=()
"$GE" src/hphpc/runtime_embed.h src/hphpc/runtime "${libs[@]}" || { echo "embed failed"; exit 1; }

if [[ "$(uname -s)" == MINGW* || "$(uname -s)" == *NT* || -n "${OS:-}" && "${OS:-}" == Windows_NT ]]; then
  # ---- MinGW/Windows: link the PE image directly with ld ----
  OUT=hphp.exe
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

  sig=$(head -c 2 "$OUT" 2>/dev/null | od -An -tx1 | tr -d ' \n')
  [ "$sig" = "4d5a" ] || { echo "output invalid"; exit 1; }
  echo "build ok: $OUT ($(stat -c%s "$OUT") bytes, runtime embedded)"
else
  # ---- POSIX (Linux/macOS): plain gcc link, ELF out ----
  OUT=hphp
  mkdir -p build/obj
  objs=()
  rc=0
  for src in src/hphpc/*.c; do
    obj="build/obj/$(basename "${src%.c}").o"
    if ! $CC $CFLAGS -Isrc/hphpc -c "$src" -o "$obj"; then rc=1; fi
    objs+=("$obj")
  done
  [ $rc -eq 0 ] || { echo "compile failed"; exit 1; }

  rm -f "$OUT"
  $CC -O2 "${objs[@]}" -o "$OUT" -lm -lpthread || { echo "link failed"; exit 1; }

  sig=$(head -c 2 "$OUT" 2>/dev/null | od -An -tx1 | tr -d ' \n')
  [ "$sig" = "7f45" ] || { echo "output invalid (not ELF)"; exit 1; }
  echo "build ok: $OUT ($(stat -c%s "$OUT") bytes, runtime embedded)"
fi
