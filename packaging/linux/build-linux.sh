#!/usr/bin/env bash
# build-linux.sh — build the `hphp` toolchain binary on Linux.
#
#   bash packaging/linux/build-linux.sh
#
# scripts/build.sh targets Windows/MinGW (gcc -c + direct `ld -m i386pep`),
# which cannot run on a Linux build host. This variant compiles the exact
# same sources but links with the gcc driver, producing a native ELF `hphp`.
# Output: ./hphp at the repo root (used by make-deb.sh / make-appimage.sh).
set -euo pipefail
cd "$(dirname "$0")/../.."          # repo root

CC=${CC:-gcc}
CFLAGS="-O2 -std=c11 -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable"
OUT=hphp

# 1. embed the runtime + bundled libraries into a header, like build.sh does
$CC -O2 -std=c11 scripts/genembed.c -o build/genembed
mkdir -p src/hphpc
libs=(lib/*.hphp)
[ -e "${libs[0]}" ] || libs=()
./build/genembed src/hphpc/runtime_embed.h src/hphpc/runtime "${libs[@]}"

# 2. compile compiler + runtime objects
mkdir -p build/obj
objs=()
for src in src/hphpc/*.c src/hphpc/runtime/*.c; do
  obj="build/obj/$(basename "${src%.c}").o"
  $CC $CFLAGS -Isrc/hphpc -c "$src" -o "$obj"
  objs+=("$obj")
done

# 3. link with the gcc driver (native ELF)
$CC -O2 -o "$OUT" "${objs[@]}" -lpthread -lm

echo "build ok: $OUT ($(stat -c%s "$OUT") bytes, runtime embedded)"
