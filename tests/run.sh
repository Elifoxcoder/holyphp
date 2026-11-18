#!/usr/bin/env bash
# tests/run.sh — the one entry point for the test suite.
#
#   bash tests/run.sh              everything that must pass
#   bash tests/run.sh positive     only tests/positive
#   bash tests/run.sh manual       tests that need a window or a port
#
#   tests/positive/   must compile AND run to completion (exit 0)
#   tests/negative/   must FAIL to compile (the checker rejects them)
#   docs              every imported snippet in docs/*.html must type-check
#   tests/manual/     opens a window / binds a socket; run on demand
#
# Exit status is 0 only when nothing failed. A skipped docs check is
# reported as SKIP, never as PASS.
set -u
cd "$(dirname "$0")/.." || exit 1

HPHP=${HPHP:-./hphp.exe}
[ -x "$HPHP" ] || [ -x "${HPHP%.exe}" ] || {
    echo "run.sh: no compiler at $HPHP (build it with scripts/build.sh)"
    exit 1
}

pass=0
fail=0
skip=0

run_positive() {
    echo "== positive =="
    for f in tests/positive/*.hphp; do
        [ -e "$f" ] || continue
        if out=$("$HPHP" run "$f" 2>&1); then
            echo "PASS $f"
            pass=$((pass + 1))
        else
            echo "FAIL $f"
            echo "$out" | head -6 | sed 's/^/    /'
            fail=$((fail + 1))
        fi
    done
}

run_negative() {
    echo "== negative =="
    for f in tests/negative/*.hphp; do
        [ -e "$f" ] || continue
        if "$HPHP" check "$f" >/dev/null 2>&1; then
            echo "FAIL $f (accepted, should be rejected)"
            fail=$((fail + 1))
        else
            echo "PASS $f (rejected as expected)"
            pass=$((pass + 1))
        fi
    done
}

run_docs() {
    echo "== docs =="
    bash tests/check_docs.sh
    case $? in
        0)  echo "PASS docs snippets"; pass=$((pass + 1)) ;;
        77) echo "SKIP docs snippets (no python)"; skip=$((skip + 1)) ;;
        *)  echo "FAIL docs snippets"; fail=$((fail + 1)) ;;
    esac
}

run_diag() {
    echo "== diagnostics =="
    bad=tests/negative/borrow_bad.hphp
    if [ ! -e "$bad" ]; then
        echo "SKIP diagnostics (no negative fixture)"
        skip=$((skip + 1))
        return
    fi
    rich=$(HPHP_DIAG=rich "$HPHP" check "$bad" 2>&1)
    compact=$(HPHP_DIAG=compact "$HPHP" check "$bad" 2>&1)
    ok=1
    case "$rich" in *"--> "*) ;; *) echo "FAIL rich: no location line"; ok=0 ;; esac
    case "$rich" in *"could not compile"*) ;; *) echo "FAIL rich: no cargo summary"; ok=0 ;; esac
    case "$rich" in *"^"*) ;; *) echo "FAIL rich: no caret"; ok=0 ;; esac
    case "$compact" in *": error: "*) ;; *) echo "FAIL compact: no file:line:col: error line"; ok=0 ;; esac
    if [ "$ok" = 1 ]; then
        echo "PASS diagnostics (rich frame + compact line)"
        pass=$((pass + 1))
    else
        fail=$((fail + 1))
    fi
}

run_ext() {
    echo "== extension =="
    if ! command -v node >/dev/null 2>&1; then
        echo "SKIP extension tests (no node)"
        skip=$((skip + 1))
        return
    fi
    if ( cd packages/extension/hphp && node --test test/*.test.js ); then
        echo "PASS extension tests"
        pass=$((pass + 1))
    else
        echo "FAIL extension tests"
        fail=$((fail + 1))
    fi
}

run_manual() {
    echo "== manual (window / sockets) =="
    for f in tests/manual/*.hphp; do
        [ -e "$f" ] || continue
        if out=$("$HPHP" run "$f" 2>&1); then
            echo "PASS $f"
            pass=$((pass + 1))
        else
            echo "FAIL $f"
            echo "$out" | head -6 | sed 's/^/    /'
            fail=$((fail + 1))
        fi
    done
}

case "${1:-all}" in
    positive) run_positive ;;
    negative) run_negative ;;
    docs)     run_docs ;;
    diag)     run_diag ;;
    ext)      run_ext ;;
    manual)   run_manual ;;
    all)      run_positive; run_negative; run_docs; run_diag; run_ext ;;
    *)        echo "usage: bash tests/run.sh [all|positive|negative|docs|diag|ext|manual]"; exit 2 ;;
esac

echo "== $pass passed, $fail failed, $skip skipped =="
[ $fail -eq 0 ]
