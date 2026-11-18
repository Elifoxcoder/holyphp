#!/usr/bin/env bash
# tests/fuzz/run.sh — build and run the libFuzzer targets.
#
#   bash tests/fuzz/run.sh                 both targets, 20 minutes each
#   bash tests/fuzz/run.sh compiler 60     compiler front end, 60 seconds
#   bash tests/fuzz/run.sh runtime         string/array/JSON builtins
#
# Needs Linux, clang and the libFuzzer runtime (Ubuntu:
# `apt-get install clang`). ASan + UBSan are always on. LeakSanitizer is off
# for the compiler process because the front end intentionally keeps its AST
# until exit (STATUS.md F1); the runtime has the same known "no reference
# counting yet" state (F2).
#
# Crash inputs land in tests/fuzz/artifacts/<target>/ and logs in
# tests/fuzz/logs/ (both git-ignored). tests/fuzz/corpus/<target>/ holds the
# curated seeds; the growing corpus is written to build/fuzz/corpus/.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
cd "$ROOT" || exit 1

if [ "$(uname -s)" != Linux ]; then
    echo "run.sh: fuzzing needs Linux (run it in WSL: wsl -d Ubuntu-24.04 -u root)"
    exit 77
fi
if ! command -v clang >/dev/null 2>&1; then
    echo "run.sh: no clang (apt-get install clang)"
    exit 77
fi
if ! echo 'int LLVMFuzzerTestOneInput(const unsigned char *d, unsigned long n){return 0;}' \
        | clang -x c -fsanitize=fuzzer -o /tmp/hp_fuzz_probe - >/dev/null 2>&1; then
    echo "run.sh: clang has no libFuzzer runtime"
    exit 77
fi
rm -f /tmp/hp_fuzz_probe

TARGET=${1:-both}
DURATION=${2:-1200}
OBJ=build/fuzz
FUZZ_FORK=${FUZZ_FORK:-4}
FUZZ_RSS_MB=${FUZZ_RSS_MB:-2048}
mkdir -p "$OBJ" tests/fuzz/artifacts/compiler tests/fuzz/artifacts/runtime

FUZZ_FLAGS="-O1 -g -fno-omit-frame-pointer -fsanitize=fuzzer-no-link,address,undefined"
LINK_FLAGS="-fsanitize=fuzzer,address,undefined -O1 -g"
export ASAN_OPTIONS="detect_leaks=0:abort_on_error=1:symbolize=1"
export UBSAN_OPTIONS="halt_on_error=0:print_stacktrace=1"
export HPHP_DIAG=quiet

build_compiler() {
    echo "== build fuzz/frontend"
    local objs=()
    for src in src/hphpc/*.c; do
        case "$(basename "$src")" in main.c) continue ;; esac
        obj="$OBJ/front_$(basename "${src%.c}").o"
        clang $FUZZ_FLAGS -D_GNU_SOURCE -Isrc/hphpc -c "$src" -o "$obj" || return 1
        objs+=("$obj")
    done
    clang $LINK_FLAGS -Isrc/hphpc tests/fuzz/compiler_fuzz.c "${objs[@]}" -lm -lpthread \
        -o "$OBJ/fuzz_frontend" || return 1
}

build_runtime() {
    echo "== build fuzz/runtime"
    local objs=()
    for src in src/hphpc/runtime/hphp_rt.c src/hphpc/runtime/hphp_std.c \
               src/hphpc/runtime/hphp_thr.c src/hphpc/runtime/hphp_ui.c \
               src/hphpc/runtime/hphp_wv2.c; do
        obj="$OBJ/rt_$(basename "${src%.c}").o"
        clang $FUZZ_FLAGS -Isrc/hphpc/runtime -include src/hphpc/runtime/hphp_ext.h \
            -c "$src" -o "$obj" || return 1
        objs+=("$obj")
    done
    clang $LINK_FLAGS -Isrc/hphpc/runtime tests/fuzz/runtime_fuzz.c "${objs[@]}" \
        -lm -lpthread -o "$OBJ/fuzz_runtime" || return 1
}

run_one() { # $1 = name, $2 = binary
    # The fuzzer writes new inputs into a build directory; tests/fuzz/corpus
    # holds the curated seeds and stays small in git.
    local name=$1 bin=$2 seed="tests/fuzz/corpus/$1"
    local corpus="build/fuzz/corpus/$1"
    local log="tests/fuzz/logs/$1.log"
    mkdir -p "$corpus" tests/fuzz/logs
    if [ -z "$(ls -A "$corpus" 2>/dev/null)" ]; then
        cp "$seed"/* "$corpus"/ 2>/dev/null || true
    fi
    echo "== fuzz $name for ${DURATION}s"
    "$bin" "$corpus" \
        -max_total_time="$DURATION" \
        -timeout=25 \
        -rss_limit_mb="$FUZZ_RSS_MB" \
        -fork="$FUZZ_FORK" \
        -ignore_crashes=1 \
        -artifact_prefix="tests/fuzz/artifacts/$name/" \
        -print_final_stats=1 2>&1 | tee "$log"
    local rc=${PIPESTATUS[0]}

    # UBSan does not abort (known, non-fatal overflow sites would stop every
    # run); collect the summary/`runtime error:` lines, which always carry the
    # source location, and subtract tests/fuzz/known.txt.
    local found new
    found=$(grep -E 'SUMMARY: (AddressSanitizer|UndefinedBehaviorSanitizer)|runtime error:' "$log" || true)
    if [ -n "$found" ] && [ -s tests/fuzz/known.txt ]; then
        new=$(printf '%s\n' "$found" | grep -vF -f tests/fuzz/known.txt || true)
    else
        new=$found
    fi
    local known
    known=$(printf '%s\n' "$found" | grep -F -f tests/fuzz/known.txt 2>/dev/null || true)
    [ -n "$known" ] && printf '%s\n' "$known" | sed 's/^/KNOWN /' | head -10

    if [ $rc -ne 0 ] && [ $rc -ne 71 ]; then
        echo "FAIL fuzz/$name (exit $rc); inputs in tests/fuzz/artifacts/$name/"
        return 1
    fi
    if [ $rc -eq 71 ]; then
        # libFuzzer's OOM exit: the runtime has no reference counting yet, so a
        # job hits -rss_limit and is restarted. Counted, not a finding.
        echo "note fuzz/$name: run ended with OOM (known leaks), see log"
    fi
    if [ -n "$new" ]; then
        printf '%s\n' "$new" | sed 's/^/FINDING /' | head -20
        echo "FAIL fuzz/$name (new sanitizer finding; log tests/fuzz/logs/$name.log)"
        return 1
    fi
    echo "PASS fuzz/$name"
}

rc=0
case "$TARGET" in
    compiler) build_compiler || exit 1; run_one compiler "$OBJ/fuzz_frontend" || rc=1 ;;
    runtime)  build_runtime  || exit 1; run_one runtime  "$OBJ/fuzz_runtime"  || rc=1 ;;
    both)
        build_compiler || exit 1
        build_runtime  || exit 1
        run_one compiler "$OBJ/fuzz_frontend" || rc=1
        run_one runtime  "$OBJ/fuzz_runtime"  || rc=1
        ;;
    *) echo "usage: bash tests/fuzz/run.sh [compiler|runtime|both] [seconds]"; exit 2 ;;
esac
exit $rc
