#!/usr/bin/env bash
# tests/project_cli.sh — hphp create / install / update / remove / deps / test.
#
# Offline: the registry points at a closed port, so dependencies come from the
# libraries bundled in the compiler or from local path: packages. Everything
# happens in a temporary folder with its own package home.
#
#   bash tests/project_cli.sh          (run.sh calls it in the "project" group)
set -u
cd "$(dirname "$0")/.." || exit 1
HPHP=${HPHP:-./hphp.exe}
[ -x "$HPHP" ] || HPHP=./hphp
HPHP=$(cd "$(dirname "$HPHP")" && pwd)/$(basename "$HPHP")

T=$(mktemp -d 2>/dev/null || echo "scratch/project-cli.$$")
mkdir -p "$T"
trap 'rm -rf "$T"' EXIT
export HPHP_REGISTRY=http://127.0.0.1:9
export HPHP_HOME="$T/home"

pass=0
fail=0
ok()   { echo "PASS project: $1"; pass=$((pass + 1)); }
bad()  { echo "FAIL project: $1"; [ -n "${2:-}" ] && printf '%s\n' "$2" | head -8 | sed 's/^/    /'; fail=$((fail + 1)); }
has() { # name, haystack, needle
    case "$2" in *"$3"*) ok "$1" ;; *) bad "$1" "$2" ;; esac
}

cd "$T" || exit 1

# --- create: every template type-checks and passes its own tests -------------
for t in console web lib empty; do
    out=$("$HPHP" create "app_$t" -t "$t" --yes --no-git --author Tester 2>&1)
    if [ $? -eq 0 ] && [ -f "app_$t/hphp.yaml" ] && [ -f "app_$t/hphp.lock" ]; then
        ok "create $t"
    else
        bad "create $t" "$out"
        continue
    fi
    out=$(cd "app_$t" && "$HPHP" check 2>&1) && ok "check $t" || bad "check $t" "$out"
    out=$(cd "app_$t" && "$HPHP" test 2>&1) && ok "test $t" || bad "test $t" "$out"
done

out=$("$HPHP" create bad-Name! --yes --no-git 2>&1)
has "create sanitizes names" "$out" 'using "bad_name"'
out=$("$HPHP" create app_console --yes --no-git 2>&1)
[ $? -ne 0 ] && ok "create refuses an existing folder" || bad "create refuses an existing folder" "$out"

# --- run / build without a file use the entry from hphp.yaml -----------------
out=$(cd app_console && echo Ada | "$HPHP" run 2>&1)
has "run uses the project entry" "$out" "Hello, Ada!"
out=$(cd app_console && echo Bo | "$HPHP" run src/main.hphp 2>&1)
has "run src/<file> from the project root" "$out" "Hello, Bo!"
out=$(cd app_empty && "$HPHP" build 2>&1)
if [ -f app_empty/build/app_empty.exe ] || [ -f app_empty/build/app_empty ]; then
    ok "build writes build/<name>"
else
    bad "build writes build/<name>" "$out"
fi

# --- the web template installed its dependency into .hphp/packages ----------
if [ -f app_web/.hphp/packages/async/async.hphp ] && grep -q "name: async" app_web/hphp.lock; then
    ok "web template installs async into .hphp/packages and locks it"
else
    bad "web template installs async into .hphp/packages and locks it" "$(cat app_web/hphp.lock 2>&1)"
fi

# --- install a local library, import it, lock it, remove it ------------------
cd app_console || exit 1
out=$("$HPHP" install ../app_lib 2>&1)
has "install a path package" "$out" "+ app_lib"
grep -q 'app_lib: "path:../app_lib"' hphp.yaml && ok "hphp.yaml records path:" \
    || bad "hphp.yaml records path:" "$(cat hphp.yaml)"
printf '<?php\nimport "app_lib";\necho app_lib_slug("Hello Project World");\n' > src/use.hphp
out=$("$HPHP" run src/use.hphp 2>&1)
has "import finds .hphp/packages" "$out" "hello-project-world"

out=$("$HPHP" install async 2>&1)
has "install a bundled library" "$out" "+ async"
grep -q 'async: "^' hphp.yaml && ok "install pins a ^ range" || bad "install pins a ^ range" "$(cat hphp.yaml)"
out=$("$HPHP" install 2>&1)
has "install keeps locked packages" "$out" "2 up to date"
out=$("$HPHP" deps 2>&1)
has "deps lists the lock" "$out" "async"

out=$("$HPHP" install async@^99 2>&1)
[ $? -ne 0 ] && has "an unsatisfiable constraint fails clearly" "$out" 'requires "^99"' \
    || bad "an unsatisfiable constraint fails clearly" "$out"
"$HPHP" install async@^1 > /dev/null 2>&1   # restore a working constraint

out=$("$HPHP" remove async 2>&1)
if [ ! -d .hphp/packages/async ] && ! grep -q "name: async" hphp.lock; then
    ok "remove drops the package, its folder and its lock entry"
else
    bad "remove drops the package, its folder and its lock entry" "$out"
fi
out=$("$HPHP" remove nothing_here 2>&1)
[ $? -ne 0 ] && ok "remove of an unknown package fails" || bad "remove of an unknown package fails" "$out"

out=$("$HPHP" install tooling --dev 2>&1)
[ $? -ne 0 ] && has "an unknown package fails clearly" "$out" "cannot install" \
    || bad "an unknown package fails clearly" "$out"
cd .. || exit 1

# --- packing a hphp.yaml library adds a generated hphp.json ------------------
out=$(cd app_lib && "$HPHP" pkg pack 2>&1)
hpx=$(ls "$HPHP_HOME"/hphp/cache/app_lib-0.1.0.hpx 2>/dev/null || ls "$HPHP_HOME"/cache/app_lib-0.1.0.hpx 2>/dev/null)
if [ -n "$hpx" ] && grep -q '^hphp.json ' "$hpx" && ! grep -q '^\.hphp/' "$hpx"; then
    ok "pkg pack adds hphp.json and skips .hphp/"
else
    bad "pkg pack adds hphp.json and skips .hphp/" "$out"
fi

# --- trust: community packages are reviewed, official names are protected ----
mkdir -p evil squat
printf '{"name":"leftpad","version":"1.2.0","entry":"leftpad.hphp","author":"someone"}\n' > evil/hphp.json
printf '<?php\n// exec( in a comment is not counted\nfunction leftpad(string $s): string {\n    shell_exec("x");\n    unlink("/tmp/x");\n    return $s;\n}\n' > evil/leftpad.hphp
printf '{"name":"http","version":"9.9.9","entry":"http.hphp"}\n' > squat/hphp.json
printf '<?php\nfunction pwn(): void {}\n' > squat/http.hphp
"$HPHP" pkg pack --dir evil > /dev/null 2>&1
"$HPHP" pkg pack --dir squat > /dev/null 2>&1
evil_hpx=$(ls "$HPHP_HOME"/hphp/cache/leftpad-1.2.0.hpx "$HPHP_HOME"/cache/leftpad-1.2.0.hpx 2>/dev/null | head -1)
squat_hpx=$(ls "$HPHP_HOME"/hphp/cache/http-9.9.9.hpx "$HPHP_HOME"/cache/http-9.9.9.hpx 2>/dev/null | head -1)

out=$("$HPHP" install "$evil_hpx" < /dev/null 2>&1)
rc=$?
if [ $rc -ne 0 ]; then ok "community package without an answer is refused"; else bad "community package without an answer is refused" "$out"; fi
has "review names the package" "$out" "community package: leftpad 1.2.0"
has "review finds shell commands" "$out" "run shell commands"
has "review finds file deletion" "$out" "write or delete files"
case "$out" in *"leftpad.hphp:2"*) bad "comments are not counted" "$out" ;; *) ok "comments are not counted" ;; esac
out=$(echo n | "$HPHP" install "$evil_hpx" 2>&1)
has "answer n refuses" "$out" "not installed"
out=$(echo y | "$HPHP" install "$evil_hpx" 2>&1)
has "answer y installs" "$out" "installed leftpad 1.2.0"
out=$("$HPHP" install "$evil_hpx" --trust 2>&1)
has "--trust installs without asking" "$out" "--trust given"
out=$("$HPHP" install "$squat_hpx" --trust 2>&1)
[ $? -ne 0 ] && has "a community package cannot take an official name" "$out" "official HolyPHP library" \
    || bad "a community package cannot take an official name" "$out"
out=$("$HPHP" install http 2>&1)
has "official names install the bundled library" "$out" "installed http"
out=$(cd app_console && "$HPHP" deps 2>&1)
has "deps shows trust levels" "$out" "TRUST"

# --- a stale per-user copy never shadows an official library -----------------
"$HPHP" install testing > /dev/null 2>&1
stale=$(ls -d "$HPHP_HOME"/hphp/packages/testing "$HPHP_HOME"/packages/testing 2>/dev/null | head -1)
if [ -n "$stale" ]; then
    printf '<?php\nfunction broken_on_purpose( {\n' > "$stale/testing.hphp"
    mkdir -p loose && printf '<?php\nimport "testing";\n$t = new Test("x");\n$t->equal(1, 1, "one");\n$t->done();\n' > loose/use_testing.hphp
    out=$("$HPHP" run loose/use_testing.hphp 2>&1)
    has "official imports ignore a stale per-user copy" "$out" "1 passed"
else
    bad "official imports ignore a stale per-user copy" "testing was not installed for the user"
fi

# --- outside a project ------------------------------------------------------
mkdir -p outside && cd outside || exit 1
out=$("$HPHP" test 2>&1)
[ $? -ne 0 ] && has "project commands need hphp.yaml" "$out" "hphp create" \
    || bad "project commands need hphp.yaml" "$out"

echo "== project: $pass passed, $fail failed =="
[ "$fail" -eq 0 ]
