#!/usr/bin/env bash
# run_all.sh — HolyPHP test suite.
#   tests/positive/*.hphp  must compile AND run to completion (exit 0)
#   tests/negative/*.hphp  must FAIL to compile (sema rejects)
set -u
cd "$(dirname "$0")/../.."
HPHP=./hphp.exe
pass=0; fail=0

echo "== positive =="
for f in tests/positive/*.hphp; do
    [ -e "$f" ] || continue
    out=$($HPHP run "$f" 2>&1)
    if [ $? -eq 0 ]; then
        echo "PASS $f"
        pass=$((pass+1))
    else
        echo "FAIL $f"
        echo "$out" | head -5 | sed 's/^/    /'
        fail=$((fail+1))
    fi
done

echo "== negative =="
for f in tests/negative/*.hphp; do
    [ -e "$f" ] || continue
    if $HPHP check "$f" >/dev/null 2>&1; then
        echo "FAIL $f (accepted, should be rejected)"
        fail=$((fail+1))
    else
        echo "PASS $f (rejected as expected)"
        pass=$((pass+1))
    fi
done

echo "== $pass passed, $fail failed =="
[ $fail -eq 0 ]
