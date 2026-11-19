#!/usr/bin/env bash
# Paired A/B for two SQLite benchmark binaries.
#
#   bash benchmarks/sqlite/paired.sh OLD_BIN NEW_BIN [rounds] [iters] [mode]
#
# A single run of this benchmark varies with host frequency and with whatever
# else runs on the machine (another worktree, a build, another agent), so the
# minimum or median of a long series of runs is not a fair comparison of two
# binaries. This script alternates A and B within one loop, so both see the
# same conditions, and prints the median of every round for each side.
#
# Both binaries are built by `hphp build benchmarks/sqlite/bench.hphp -o ...`;
# each one embeds the runtime, the SQLite archive and the compile options of
# the compiler that produced it. Keep a copy of the old binary before
# rebuilding, e.g.
#
#   hphp build benchmarks/sqlite/bench.hphp -o build/sqlite-bench/bench_old
#   ... change build.sh or the runtime, rebuild ...
#   hphp build benchmarks/sqlite/bench.hphp -o build/sqlite-bench/bench_new
#   bash benchmarks/sqlite/paired.sh \
#       build/sqlite-bench/bench_old build/sqlite-bench/bench_new 21 50000 feed
#
# Needs Linux/WSL and the challenge fixture; the database copy lives on tmpfs.
set -u
cd "$(dirname "$0")/../.." || exit 1

A=${1:-}; B=${2:-}
ROUNDS=${3:-21}
ITERS=${4:-50000}
MODE=${5:-point}
if [ -z "$A" ] || [ -z "$B" ]; then
    sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'
    exit 2
fi
[ -x "$A" ] || { echo "paired.sh: no executable at $A"; exit 1; }
[ -x "$B" ] || { echo "paired.sh: no executable at $B"; exit 1; }
FIXTURE=benchmarks/challenge/fixture/feed.db
[ -f "$FIXTURE" ] || { echo "paired.sh: missing $FIXTURE"; exit 1; }
BASE=${BENCH_TMPDIR:-/dev/shm}
[ -d "$BASE" ] && [ -w "$BASE" ] || BASE=${TMPDIR:-/tmp}
W=$(mktemp -d "$BASE/hphp-paired.XXXXXX") || exit 1
trap 'rm -rf "$W"' EXIT

run() { # binary -> ns/op
    rm -f "$W/bench.db" "$W/bench.db-wal" "$W/bench.db-shm"
    cp "$FIXTURE" "$W/bench.db" || exit 1
    BENCH_DB="$W/bench.db" BENCH_ITERS="$ITERS" BENCH_ONLY="$MODE" \
        "$1" 2>/dev/null \
        | sed -n 's/^BENCH [a-z]* iters=[0-9]* ns_per_op=\([0-9][0-9]*\).*/\1/p'
}
median() {
    printf '%s\n' "$@" | sort -n | sed -n "$(( ($# + 1) / 2 ))p"
}

av=()
bv=()
for r in $(seq 1 "$ROUNDS"); do
    if [ $((r % 2)) -eq 1 ]; then
        x=$(run "$A"); y=$(run "$B")
    else
        y=$(run "$B"); x=$(run "$A")
    fi
    av+=("$x"); bv+=("$y")
done
printf 'mode=%s iters=%s rounds=%s\n' "$MODE" "$ITERS" "$ROUNDS"
printf 'A %-55s median=%s\n' "$A" "$(median "${av[@]}")"
printf 'B %-55s median=%s\n' "$B" "$(median "${bv[@]}")"
