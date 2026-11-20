#!/usr/bin/env bash
# build-asan.sh — build the compiler with AddressSanitizer + UndefinedBehavior
# Sanitizer + LeakSanitizer.
#
#   bash scripts/build-asan.sh          -> build/hphp_asan
#   HPHP_ASAN=1 bash tests/run.sh --asan
#
# Sanitizers need a Linux host (WSL is fine); MinGW has none. The program a
# test compiles is built by the compiler itself, so the sanitizers must also be
# applied to that backend call. tests/run.sh --asan puts a `gcc` wrapper in
# front of PATH for exactly that; this script only builds the compiler.
set -u
cd "$(dirname "$0")/.." || exit 1

if [[ "$(uname -s)" != Linux ]]; then
    echo "build-asan: sanitizers need Linux (run this in WSL: wsl -d Ubuntu-24.04 -u root)"
    exit 1
fi

command -v gcc >/dev/null 2>&1 || { echo "build-asan: no gcc"; exit 1; }
if ! echo 'int main(void){return 0;}' | gcc -x c -fsanitize=address,undefined -fsanitize=leak -o /tmp/hphp_asan_probe - >/dev/null 2>&1; then
    echo "build-asan: gcc has no ASan/UBSan runtime (install libasan8 / libubsan1)"
    exit 1
fi
rm -f /tmp/hphp_asan_probe

SAN="-O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fsanitize=leak"
HPHP_CFLAGS="$SAN -std=c11 -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable" \
HPHP_OUT=build/hphp_asan \
    bash scripts/build.sh
