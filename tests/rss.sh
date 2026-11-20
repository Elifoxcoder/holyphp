#!/usr/bin/env bash
# rss.sh — the Linux twin of tests/rss.ps1: run one positive test and verify
# that its resident set stays flat.
#
#   bash tests/rss.sh tests/positive/memory_rc.hphp "memory rc loops ok"
#
# Samples /proc/<pid>/status VmRSS every 5 ms. The warm-up median (first 15 %)
# and the tail median (last 30 %) may differ by at most 16 MiB; the tail range
# by at most 24 MiB. Needs Linux (WSL is fine); see tests/README.md.
set -eu
cd "$(dirname "$0")/.."
TEST=${1:?usage: bash tests/rss.sh <test.hphp> [expected output]}
EXPECTED=${2:-}
HPHP=${HPHP:-./hphp}
[ -x "$HPHP" ] || { echo "rss: no compiler at $HPHP" >&2; exit 2; }
[ -f "$TEST" ] || { echo "rss: no test at $TEST" >&2; exit 2; }

work=build/rss
mkdir -p "$work"
name=$(basename "$TEST" .hphp)
exe="$work/$name"
HPHP_WORK_DIR="$PWD/scratch/hphpbuild" "$HPHP" build "$TEST" -o "$exe" >/dev/null

out="$work/$name.out"
"$exe" >"$out" 2>"$work/$name.err" &
pid=$!
samples="$work/$name.rss"
: >"$samples"
while kill -0 "$pid" 2>/dev/null; do
    rss=$(awk '/^VmRSS:/ {print $2}' "/proc/$pid/status" 2>/dev/null || true)
    [ -n "$rss" ] && echo "$rss" >>"$samples"
    sleep 0.005
done
wait "$pid"
rc=$?
if [ $rc -ne 0 ]; then
    echo "rss: test failed ($rc): $(cat "$work/$name.err") $(cat "$out")" >&2
    exit 1
fi
if [ -n "$EXPECTED" ]; then
    got=$(tr -d '\r' <"$out" | sed -e 's/[[:space:]]*$//' | tail -n 1)
    [ "$got" = "$EXPECTED" ] || { echo "rss: output '$got' (want '$EXPECTED')" >&2; exit 1; }
fi
n=$(wc -l <"$samples")
[ "$n" -ge 8 ] || { echo "rss: too few samples ($n); workload too quick to measure" >&2; exit 1; }
warm_n=$(( n * 15 / 100 )); [ "$warm_n" -lt 3 ] && warm_n=3
tail_start=$(( n * 70 / 100 + 1 ))
warm=$(head -n "$warm_n" "$samples" | sort -n | sed -n "$(( warm_n / 2 + 1 ))p")
tail_count=$(( n - tail_start + 1 ))
tail_med=$(tail -n +"$tail_start" "$samples" | sort -n | sed -n "$(( tail_count / 2 + 1 ))p")
tail_first=$(tail -n +"$tail_start" "$samples" | sort -n | head -n 1)
tail_last=$(tail -n +"$tail_start" "$samples" | sort -n | tail -n 1)
awk -v test="$TEST" -v n="$n" -v warm="$warm" -v tail="$tail_med" \
    -v lo="$tail_first" -v hi="$tail_last" 'BEGIN {
    warm /= 1024; tail /= 1024; range = (hi - lo) / 1024;
    printf "rss ok: %s (%d samples; warm %.1f MiB, tail %.1f MiB)\n", test, n, warm, tail;
    if (tail > warm + 16 || range > 24) {
        printf "rss: did not flatten: warm %.1f MiB, tail %.1f MiB, tail range %.1f MiB\n", warm, tail, range > "/dev/stderr";
        exit 1;
    }
}'
