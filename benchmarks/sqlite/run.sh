#!/usr/bin/env bash
# SQLite micro benchmark for the runtime module (benchmarks/sqlite/bench.hphp).
#
#   bash benchmarks/sqlite/run.sh                 # 20000 ops, 3 timing runs
#   bash benchmarks/sqlite/run.sh --iters 50000 --repeat 5
#   BENCH_PREPARE_EACH=1 bash benchmarks/sqlite/run.sh --only point
#
# Builds the compiler when ./hphp is missing, then for every workload runs a
# fresh copy of benchmarks/challenge/fixture/feed.db (on /tmp, so page-cache
# and WAL I/O are not the variable under test):
#
#   - `--repeat` timed runs; the smallest ns/op is reported
#   - two counting runs through malloc_count.so (BENCH_ITERS=0 vs full); the
#     difference is the marginal malloc cost per measured op
#
# Output is one RESULT line per workload, suitable for benchmarks/RESULTS.md.
# Needs Linux or WSL (the malloc counter uses glibc's __libc_* entry points).
set -u
cd "$(dirname "$0")/../.." || exit 1
ROOT=$PWD

ITERS=20000
REPEAT=3
ONLY=""
while [ $# -gt 0 ]; do
    case "$1" in
        --iters) ITERS=${2:-}; shift 2 ;;
        --iters=*) ITERS=${1#--iters=}; shift ;;
        --repeat) REPEAT=${2:-}; shift 2 ;;
        --repeat=*) REPEAT=${1#--repeat=}; shift ;;
        --only) ONLY=${2:-}; shift 2 ;;
        --only=*) ONLY=${1#--only=}; shift ;;
        -h|--help)
            sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'
            exit 0 ;;
        *) echo "run.sh: unknown argument '$1'"; exit 2 ;;
    esac
done
case "$ITERS" in ''|*[!0-9]*) echo "run.sh: --iters needs a number"; exit 2 ;; esac
case "$REPEAT" in ''|*[!0-9]*) echo "run.sh: --repeat needs a number"; exit 2 ;; esac
[ "$ITERS" -gt 0 ] || { echo "run.sh: --iters must be positive"; exit 2; }
[ "$REPEAT" -gt 0 ] || { echo "run.sh: --repeat must be positive"; exit 2; }

HPHP=${HPHP:-$ROOT/hphp}
if [ ! -x "$HPHP" ] && [ -x "$HPHP.exe" ]; then HPHP=$HPHP.exe; fi
if [ ! -x "$HPHP" ]; then
    echo "== building the compiler"
    bash scripts/build.sh || exit 1
    HPHP=$ROOT/hphp
    [ -x "$HPHP" ] || HPHP=$ROOT/hphp.exe
fi
[ -x "$HPHP" ] || { echo "run.sh: no compiler at $HPHP"; exit 1; }

OUT=$ROOT/build/sqlite-bench
mkdir -p "$OUT" || exit 1
echo "== building $OUT/bench"
"$HPHP" build benchmarks/sqlite/bench.hphp -o "$OUT/bench" || exit 1
CC=${CC:-cc}
[ -f "$OUT/malloc_count.so" ] && [ "$OUT/malloc_count.so" -nt benchmarks/sqlite/malloc_count.c ] || {
    "$CC" -O2 -fPIC -shared -o "$OUT/malloc_count.so" benchmarks/sqlite/malloc_count.c || exit 1
}

FIXTURE=$ROOT/benchmarks/challenge/fixture/feed.db
[ -f "$FIXTURE" ] || { echo "run.sh: missing $FIXTURE"; exit 1; }
# The database lives on tmpfs when possible: this benchmark measures SQLite's
# CPU and allocator work, not the disk. BENCH_TMPDIR picks another location.
BASE=${BENCH_TMPDIR:-/dev/shm}
[ -d "$BASE" ] && [ -w "$BASE" ] || BASE=${TMPDIR:-/tmp}
WORK=$(mktemp -d "$BASE/hphp-sqlite-bench.XXXXXX") || exit 1
cleanup() { rm -rf "$WORK"; }
trap cleanup EXIT

fresh_db() {
    rm -f "$WORK/bench.db" "$WORK/bench.db-wal" "$WORK/bench.db-shm"
    cp "$FIXTURE" "$WORK/bench.db" || exit 1
}

WORKLOADS=${ONLY:-point range feed insert bt prepare}

REV=${GIT_REV:-$(git rev-parse --short HEAD 2>/dev/null || echo no-git)}
printf 'sqlite-bench: %s, iters=%s, repeat=%s, compiler=%s\n' \
    "$REV" "$ITERS" "$REPEAT" "$("$HPHP" version 2>/dev/null | tr -d '\n')"
for mode in $WORKLOADS; do
    vals=()
    for _ in $(seq 1 "$REPEAT"); do
        fresh_db
        line=$(BENCH_DB="$WORK/bench.db" BENCH_ITERS="$ITERS" BENCH_ONLY="$mode" \
            "$OUT/bench" 2>/dev/null | sed -n 's/^BENCH [a-z]* iters=[0-9]* ns_per_op=\([0-9][0-9]*\).*/\1/p')
        [ -n "$line" ] || { echo "run.sh: no BENCH line for $mode"; exit 1; }
        vals+=("$line")
    done
    sorted=$(printf '%s\n' "${vals[@]}" | sort -n)
    best=$(printf '%s\n' "$sorted" | head -1)
    # median keeps one boosted run from defining the result; the minimum is
    # printed next to it when BENCH_MIN=1
    median=$(printf '%s\n' "$sorted" | sed -n "$(( (REPEAT + 1) / 2 ))p")
    extra=""
    [ "${BENCH_MIN:-0}" = 1 ] && extra=" ns_min=$best"

    fresh_db
    setup=$(BENCH_DB="$WORK/bench.db" BENCH_ITERS=0 BENCH_ONLY="$mode" MALLOC_TAG=setup \
        LD_PRELOAD="$OUT/malloc_count.so" "$OUT/bench" 2>&1 >/dev/null \
        | sed -n 's/^MALLOC tag=setup malloc=\([0-9]*\) calloc=\([0-9]*\) realloc=\([0-9]*\) free=\([0-9]*\) bytes=\([0-9]*\).*/\1 \2 \3 \4 \5/p')
    fresh_db
    full=$(BENCH_DB="$WORK/bench.db" BENCH_ITERS="$ITERS" BENCH_ONLY="$mode" MALLOC_TAG=full \
        LD_PRELOAD="$OUT/malloc_count.so" "$OUT/bench" 2>&1 >/dev/null \
        | sed -n 's/^MALLOC tag=full malloc=\([0-9]*\) calloc=\([0-9]*\) realloc=\([0-9]*\) free=\([0-9]*\) bytes=\([0-9]*\).*/\1 \2 \3 \4 \5/p')
    if [ -z "$setup" ] || [ -z "$full" ]; then
        echo "run.sh: malloc counter produced no line for $mode"; exit 1
    fi

    set -- $setup
    m0=$(( $1 + $2 + $3 )); b0=$5
    set -- $full
    m1=$(( $1 + $2 + $3 )); b1=$5
    read -r mops bops < <(awk -v m0="$m0" -v m1="$m1" -v b0="$b0" -v b1="$b1" -v n="$ITERS" \
        'BEGIN { printf "%.2f %.1f\n", (m1-m0)/n, (b1-b0)/n }')
    printf 'RESULT %-8s ns_per_op=%-8s malloc_per_op=%-7s malloc_bytes_per_op=%s%s\n' \
        "$mode" "$median" "$mops" "$bops" "$extra"
done
