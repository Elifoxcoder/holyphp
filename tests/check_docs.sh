#!/usr/bin/env bash
# check_docs.sh — every HolyPHP snippet in docs/*.html must type-check.
#
# The docs went stale once already: the UI examples still showed the 1.0
# coordinate API long after the library shipped 2.0. This script extracts each
# <code class="hphp"> block that uses the ui library and runs `hphp check` on it,
# so a broken example fails CI instead of quietly misleading readers.
#
# Snippets are illustrative and may reference a sibling file (e.g.
# `import "helpers";`), so we run each one in its own temp dir next to a stub.

set -u
cd "$(dirname "$0")/../.." || exit 1

HPHP=${HPHP:-./hphp.exe}
[ -x "$HPHP" ] || { echo "check_docs: no compiler at $HPHP"; exit 1; }

PY=${PY:-}
if [ -z "$PY" ]; then
    for c in python3 python py; do
        command -v "$c" >/dev/null 2>&1 || continue
        # the Windows Store stub exits 0 but prints an install prompt; probe it
        if "$c" -c "pass" >/dev/null 2>&1; then PY=$c; break; fi
    done
fi
if [ -z "$PY" ]; then
    echo "check_docs: no working python found, skipping doc snippet check"
    exit 0
fi

tmp=$(mktemp -d 2>/dev/null || echo "build/_docsnip.$$")
mkdir -p "$tmp"
trap 'rm -rf "$tmp"' EXIT

# extract -> one .hphp per snippet, plus a helpers.hphp stub
"$PY" - "$tmp" <<'PYEOF'
import io, re, os, sys, glob, html

out = sys.argv[1]
n = 0
for path in sorted(glob.glob('docs/*.html')):
    raw = io.open(path, 'rb').read().replace(b'\r\n', b'\n').decode('utf-8')
    blocks = re.findall(r'<code class="hphp">(.*?)</code></pre>', raw, re.S)
    for i, b in enumerate(blocks):
        code = html.unescape(b)
        # only the UI examples: those are the ones that drift with the library
        if 'import "ui"' not in code:
            continue
        name = '%s_%02d.hphp' % (os.path.basename(path)[:-5], i)
        with io.open(os.path.join(out, name), 'w', encoding='utf-8', newline='\n') as f:
            f.write(code)
        n += 1
# snippets like the import list reference a neighbouring file
with io.open(os.path.join(out, 'helpers.hphp'), 'w', encoding='utf-8', newline='\n') as f:
    f.write('function doc_stub(): int { return 1; }\n')
print(n)
PYEOF

if ! ls "$tmp"/*.hphp >/dev/null 2>&1; then
    echo "check_docs: no ui snippets found in docs/*.html"
    exit 0
fi

pass=0
fail=0
for f in "$tmp"/*.hphp; do
    name=$(basename "$f")
    [ "$name" = "helpers.hphp" ] && continue   # the stub we planted, not a snippet
    if out=$("$HPHP" check "$f" 2>&1); then
        echo "PASS docs snippet $name"
        pass=$((pass + 1))
    else
        echo "FAIL docs snippet $name"
        echo "$out" | head -3 | sed 's/^/    /'
        fail=$((fail + 1))
    fi
done

echo "== docs: $pass passed, $fail failed =="
[ "$fail" -eq 0 ]