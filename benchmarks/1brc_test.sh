#!/usr/bin/env bash
# 1brc_test.sh — correctness + speed test for the HolyPHP 1BRC benchmark.
#
#   bash benchmarks/1brc_test.sh              # 1M-row deterministic sample
#   bash benchmarks/1brc_test.sh 10000000     # bigger sample (arg = row count)
#   bash benchmarks/1brc_test.sh 0            # run on the real 1e9-row file
#
# Generates a sample measurements.txt from the official 1BRC weather station
# list (seeded, deterministic), computes the expected result with the Python
# oracle (1brc_ref.py), then verifies both the HolyPHP implementation and the
# PHP twin against it and prints aggregate-loop timings.

set -u
cd "$(dirname "$0")/.."

HPHP=./hphp.exe
STATIONS="C:/Users/elias/Downloads/1brc-main/1brc-main/data/weather_stations.csv"
REAL="C:/Users/elias/Downloads/1brc-main/1brc-main/measurements.txt"
SAMPLE="benchmarks/1brc_sample.txt"

ROWS="${1:-1000000}"
fail=0

if [ "$ROWS" = "0" ]; then
    DATA="$REAL"
    if [ ! -f "$DATA" ]; then
        echo "SKIP: $DATA not found"
        exit 2
    fi
    echo "== 1brc: real measurements.txt ($(wc -c < "$DATA") bytes) =="
else
    DATA="$SAMPLE"
    if [ ! -f "$STATIONS" ]; then
        echo "SKIP: $STATIONS not found"
        exit 2
    fi
    echo "== 1brc: generating $ROWS-row sample (seeded, deterministic) =="
    cut -d';' -f1 "$STATIONS" | grep -v '^$' | grep -v '^#' > "$SAMPLE.stations"
    awk -v rows="$ROWS" 'BEGIN{
        n = 0;
        while ((getline line < ARGV[1]) > 0) { stations[n++] = line }
        for (i = 0; i < rows; i++) {
            s = stations[int(rand() * n)];
            t = int(rand() * 1999 - 999);
            printf "%s;%.1f\n", s, t / 10;
        }
    }' "$SAMPLE.stations" > "$DATA"
    rm -f "$SAMPLE.stations"
    echo "sample: $(wc -l < "$DATA") rows -> $DATA"
fi

if [ "$ROWS" != "0" ]; then
    echo "== oracle (python) =="
    if ! python benchmarks/1brc_ref.py "$DATA" > "$SAMPLE.expected" 2>"$SAMPLE.expected.err"; then
        echo "FAIL: oracle crashed"
        cat "$SAMPLE.expected.err"
        rm -f "$SAMPLE.expected" "$SAMPLE.expected.err"
        exit 1
    fi
    cat "$SAMPLE.expected.err"
    rm -f "$SAMPLE.expected.err"
    EXPECTED=$(head -1 "$SAMPLE.expected" | tr -d '\r')
fi

echo "== hphp (HolyPHP) =="
if ! $HPHP run benchmarks/1brc.hphp "$DATA" > "$SAMPLE.hphp.out" 2>"$SAMPLE.hphp.err"; then
    echo "FAIL: hphp run failed"
    cat "$SAMPLE.hphp.err"
    fail=1
fi
if [ "$fail" = "0" ]; then
    tail -n +2 "$SAMPLE.hphp.out" | sed 's/^/    /'
    GOT=$(head -1 "$SAMPLE.hphp.out" | tr -d '\r')
    if [ "${ROWS:-1}" != "0" ]; then
        if [ "$GOT" = "$EXPECTED" ]; then
            echo "PASS hphp output matches oracle"
        else
            echo "FAIL hphp output differs"
            python - "$SAMPLE.expected" "$SAMPLE.hphp.out" <<'PYEOF'
import sys
if hasattr(sys.stdout, "reconfigure"): sys.stdout.reconfigure(encoding="utf-8")
exp = open(sys.argv[1], encoding="utf-8").readline().rstrip("\r\n")
got = open(sys.argv[2], encoding="utf-8").readline().rstrip("\r\n")
for i, (a, b) in enumerate(zip(exp, got)):
    if a != b:
        lo = max(0, i - 80)
        print(f"first diff at char {i}:")
        print(f"  expected ...{exp[lo:i + 80]}...")
        print(f"  got      ...{got[lo:i + 80]}...")
        break
else:
    print(f"length differs: expected {len(exp)}, got {len(got)}")
    tail = exp[len(got):] if len(exp) > len(got) else got[len(exp):]
    print(f"  extra: {tail[:200]}")
PYEOF
            fail=1
        fi
    fi
fi

if command -v php >/dev/null 2>&1; then
    echo "== php (twin) =="
    if ! php benchmarks/1brc.php "$DATA" > "$SAMPLE.php.out" 2>"$SAMPLE.php.err"; then
        echo "FAIL: php run failed"
        cat "$SAMPLE.php.err"
        fail=1
    fi
    if [ "$fail" = "0" ]; then
        tail -n +2 "$SAMPLE.php.out" | sed 's/^/    /'
        PGOT=$(head -1 "$SAMPLE.php.out" | tr -d '\r')
        if [ "${ROWS:-1}" != "0" ] && [ "$PGOT" != "$EXPECTED" ]; then
            echo "FAIL php output differs"
            python - "$SAMPLE.expected" "$SAMPLE.php.out" <<'PYEOF'
import sys
if hasattr(sys.stdout, "reconfigure"): sys.stdout.reconfigure(encoding="utf-8")
exp = open(sys.argv[1], encoding="utf-8").readline().rstrip("\r\n")
got = open(sys.argv[2], encoding="utf-8").readline().rstrip("\r\n")
for i, (a, b) in enumerate(zip(exp, got)):
    if a != b:
        lo = max(0, i - 80)
        print(f"first diff at char {i}:")
        print(f"  expected ...{exp[lo:i + 80]}...")
        print(f"  got      ...{got[lo:i + 80]}...")
        break
else:
    print(f"length differs: expected {len(exp)}, got {len(got)}")
    tail = exp[len(got):] if len(exp) > len(got) else got[len(exp):]
    print(f"  extra: {tail[:200]}")
PYEOF
            fail=1
        elif [ "${ROWS:-1}" != "0" ]; then
            echo "PASS php output matches oracle"
        fi
    fi
else
    echo "== php not found, skipping twin =="
fi

rm -f "$SAMPLE.expected" "$SAMPLE.hphp.out" "$SAMPLE.hphp.err" "$SAMPLE.php.out" "$SAMPLE.php.err"

echo "== $([ $fail -eq 0 ] && echo ALL PASS || echo FAILURES) =="
exit $fail
