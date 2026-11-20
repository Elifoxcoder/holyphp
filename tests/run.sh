#!/usr/bin/env bash
# tests/run.sh — the one entry point for the test suite.
#
#   bash tests/run.sh              everything that must pass
#   bash tests/run.sh positive     only tests/positive
#   bash tests/run.sh manual       tests that need a window or a port
#   bash tests/run.sh project      hphp create / install / test in a temp folder
#   bash tests/run.sh --asan       everything under ASan + UBSan + LeakSanitizer
#
#   tests/positive/   must compile AND run to completion (exit 0)
#   tests/negative/   must FAIL to compile (the checker rejects them)
#   docs              every imported snippet in docs/*.html must type-check
#   tests/manual/     opens a window / binds a socket; run on demand
#
# --asan needs Linux (WSL); it builds hphp_asan on demand (scripts/build-asan.sh),
# routes every backend gcc call through a wrapper that adds the sanitizer flags,
# keeps the combined output per test in build/asan-logs/, and reports every
# sanitizer finding. See STATUS.md.
#
# Exit status is 0 only when nothing failed. A skipped docs check is
# reported as SKIP, never as PASS.
set -u
cd "$(dirname "$0")/.." || exit 1

# ---- --asan: run everything under ASan + UBSan + LeakSanitizer -------------
# Sanitizers exist only on Linux (WSL is the intended host). The compiler is
# built as hphp_asan (scripts/build-asan.sh); the program it compiles is built
# by a `gcc` wrapper that adds the same flags, because the runtime is embedded
# as C source and compiled at run time.
ASAN=0
MODE=all
for arg in "$@"; do
    case "$arg" in
        --asan) ASAN=1 ;;
        *)      MODE=$arg ;;
    esac
done

if [ "$ASAN" = 1 ]; then
    if [ "$(uname -s)" != Linux ]; then
        echo "SKIP asan (sanitizers need Linux; run: wsl -d Ubuntu-24.04 -u root)"
        exit 77
    fi
    HPHP=${HPHP:-./build/hphp_asan}
    if [ ! -x "$HPHP" ]; then
        echo "== asan: building $HPHP =="
        bash scripts/build-asan.sh || exit 1
    fi
    mkdir -p build/asan-bin build/asan-logs
    rm -f build/asan-logs/*.log
    REAL_GCC=$(command -v gcc)
    cat > build/asan-bin/gcc <<EOF
#!/bin/sh
exec "$REAL_GCC" -w -fsanitize=address,undefined -fsanitize=leak \\
     -fno-omit-frame-pointer -g "\$@"
EOF
    chmod +x build/asan-bin/gcc
    PATH="$PWD/build/asan-bin:$PATH"
    export PATH
    # the normal object cache holds unsanitised code: never reuse it here
    HPHP_NO_CACHE=1
    export HPHP_NO_CACHE
    ASAN_OPTIONS=${ASAN_OPTIONS:-detect_leaks=1:abort_on_error=0:halt_on_error=0:symbolize=1}
    UBSAN_OPTIONS=${UBSAN_OPTIONS:-print_stacktrace=1:halt_on_error=0}
    # leaks of the compiler process itself are expected (see the .supp file);
    # leaks of the compiled test programs are still reported
    LSAN_OPTIONS=${LSAN_OPTIONS:-suppressions=$PWD/tests/lsan-compiler.supp:print_suppressions=0}
    export ASAN_OPTIONS UBSAN_OPTIONS LSAN_OPTIONS
    asan_findings=0
fi

HPHP=${HPHP:-./hphp.exe}
[ -x "$HPHP" ] || [ -x "${HPHP%.exe}" ] || {
    echo "run.sh: no compiler at $HPHP (build it with scripts/build.sh)"
    exit 1
}

pass=0
fail=0
skip=0

# Return the interesting sanitizer lines of $1 (empty when clean).
asan_marker() {
    printf '%s\n' "$1" | grep -E \
        'ERROR: (Address|Leak|UndefinedBehavior)Sanitizer|runtime error:|AddressSanitizer:DEADLYSIGNAL|SUMMARY: (Address|UndefinedBehavior)Sanitizer' \
        | head -6
}
asan_note() {
    # $1 = test, $2 = sanitizer lines
    echo "ASAN $1"
    printf '%s\n' "$2" | sed 's/^/    /'
    asan_findings=$((asan_findings + 1))
}
# Run one test, log its combined output under --asan, set $ok and $out.
run_logged() {
    local f=$1
    if [ "$ASAN" = 1 ]; then
        log="build/asan-logs/$(basename "$f").log"
        if "$HPHP" run "$f" >"$log" 2>&1; then ok=1; else ok=0; fi
        out=$(cat "$log")
    else
        if out=$("$HPHP" run "$f" 2>&1); then ok=1; else ok=0; fi
    fi
}

run_positive() {
    echo "== positive =="
    for f in tests/positive/*.hphp; do
        [ -e "$f" ] || continue
        run_logged "$f"
        if [ "$ASAN" = 1 ]; then
            hit=$(asan_marker "$out")
            if [ -n "$hit" ]; then
                asan_note "$f" "$hit"
                ok=0
            fi
        fi
        if [ "$ok" = 1 ]; then
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
        if [ "$ASAN" = 1 ]; then
            log="build/asan-logs/$(basename "$f").log"
            if "$HPHP" check "$f" >"$log" 2>&1; then accepted=1; else accepted=0; fi
            hit=$(asan_marker "$(cat "$log")")
            if [ -n "$hit" ]; then asan_note "$f" "$hit"; fi
        else
            hit=
            if "$HPHP" check "$f" >/dev/null 2>&1; then accepted=1; else accepted=0; fi
        fi
        if [ "$accepted" = 1 ]; then
            echo "FAIL $f (accepted, should be rejected)"
            fail=$((fail + 1))
        elif [ -n "$hit" ]; then
            echo "FAIL $f (rejected, but with a sanitizer finding)"
            fail=$((fail + 1))
        else
            echo "PASS $f (rejected as expected)"
            pass=$((pass + 1))
        fi
    done
}

run_docs() {
    echo "== docs =="
    if [ "$ASAN" = 1 ]; then
        if out=$(bash tests/check_docs.sh 2>&1); then drc=0; else drc=$?; fi
        echo "$out"
        hit=$(asan_marker "$out")
        if [ -n "$hit" ]; then asan_note "docs snippets" "$hit"; drc=1; fi
    else
        bash tests/check_docs.sh
        drc=$?
    fi
    case $drc in
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
    if [ "$ASAN" = 1 ]; then
        hit=$(asan_marker "$rich$compact")
        if [ -n "$hit" ]; then asan_note "diagnostics" "$hit"; ok=0; fi
    fi
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

run_project() {
    echo "== project (hphp create / install / test) =="
    if HPHP="$HPHP" bash tests/project_cli.sh; then
        pass=$((pass + 1))
    else
        fail=$((fail + 1))
    fi
}

run_manual() {
    echo "== manual (window / sockets) =="
    for f in tests/manual/*.hphp; do
        [ -e "$f" ] || continue
        run_logged "$f"
        if [ "$ASAN" = 1 ]; then
            hit=$(asan_marker "$out")
            if [ -n "$hit" ]; then asan_note "$f" "$hit"; ok=0; fi
        fi
        if [ "$ok" = 1 ]; then
            echo "PASS $f"
            pass=$((pass + 1))
        else
            echo "FAIL $f"
            echo "$out" | head -6 | sed 's/^/    /'
            fail=$((fail + 1))
        fi
    done
}

case "$MODE" in
    positive) run_positive ;;
    negative) run_negative ;;
    docs)     run_docs ;;
    diag)     run_diag ;;
    ext)      run_ext ;;
    project)  run_project ;;
    manual)   run_manual ;;
    all)      run_positive; run_negative; run_docs; run_diag; run_project; run_ext ;;
    *)        echo "usage: bash tests/run.sh [--asan] [all|positive|negative|docs|diag|project|ext|manual]"; exit 2 ;;
esac

if [ "$ASAN" = 1 ]; then
    echo "== asan: $asan_findings test(s) with sanitizer findings (logs: build/asan-logs/) =="
fi

echo "== $pass passed, $fail failed, $skip skipped =="
[ $fail -eq 0 ]
