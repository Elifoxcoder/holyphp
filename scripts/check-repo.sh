#!/usr/bin/env bash
# check-repo.sh — read-only checks for the things that keep going stale.
# Run it before and after any change; scripts/ship.sh runs it as its first step.
#
#   bash scripts/check-repo.sh              # report everything, exit 1 on any problem
#   bash scripts/check-repo.sh --release X  # also require a CHANGELOG entry for X
#                                           #   and a clean working tree
#
# Checks:
#   1. versions     every place in scripts/set-version.sh agrees
#   2. vps host     no server address outside scripts/vps.env
#   3. builtin docs every builtin in src/hphpc/builtins.c has a description in
#                   scripts/builtin-docs.js and an entry in scripts/builtin-examples.js
#   4. changelog    CHANGELOG.md has a section for the current/released version
#   5. hygiene      no untracked files in the repo root or in tests/ (scratch
#                   work goes to scratch/, which is ignored by these checks)
#   6. known fails  every name in tests/known-failures.txt still exists
#   7. clean tree   (--release only) nothing uncommitted
set -uo pipefail
cd "$(dirname "$0")/.."

RELEASE=""
[[ "${1:-}" == "--release" ]] && RELEASE="${2:-}"

problems=0
ok()   { printf '  ok    %s\n' "$*"; }
bad()  { printf '  FAIL  %s\n' "$*"; problems=$((problems + 1)); }
note() { printf '        %s\n' "$*"; }

echo "== check-repo"

# 1. versions
if out=$(bash scripts/set-version.sh --check 2>&1); then
  ok "$out"
  VERSION=$(echo "$out" | awk '{print $NF}')
else
  bad "versions disagree"; echo "$out" | sed 's/^/        /'
  VERSION=$(sed -n 's/^#define HPHP_VERSION "\(.*\)"$/\1/p' src/hphpc/main.c)
fi

# 2. vps host: an IPv4 after user@ anywhere but vps.env is a stale copy
hits=$(grep -rnE '[a-z]+@[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}' \
         scripts AGENTS.md CLAUDE.md OPENCODE.md tools packaging 2>/dev/null \
       | grep -v '^scripts/vps.env:' | grep -v 'root@1\.2\.3\.4' || true)
if [[ -z "$hits" ]]; then ok "server address only in scripts/vps.env"
else bad "server address hard-coded outside scripts/vps.env:"; echo "$hits" | sed 's/^/        /'; fi

# 3. builtin docs (same regex as scripts/genext.js)
if command -v node >/dev/null 2>&1; then
  missing=$(node -e '
    const fs = require("fs");
    const src = fs.readFileSync("src/hphpc/builtins.c", "utf8");
    const re = /add\(\s*"([^"]+)"\s*,\s*(B_[A-Z0-9_]+)/g;
    const docs = require("./scripts/builtin-docs.js");
    const ex = require("./scripts/builtin-examples.js");
    const todoFile = "scripts/builtin-examples-todo.txt";
    const todo = new Set(fs.existsSync(todoFile)
      ? fs.readFileSync(todoFile, "utf8").split(/\r?\n/).map(s => s.trim()).filter(s => s && !s.startsWith("#"))
      : []);
    const noDoc = [], noEx = [], done = [];
    let m, n = 0;
    while ((m = re.exec(src))) {
      n++;
      if (!docs[m[1]]) noDoc.push(m[1]);
      if (!ex[m[1]] && !todo.has(m[1])) noEx.push(m[1]);
      if (ex[m[1]] && todo.has(m[1])) done.push(m[1]);
    }
    if (n < 50) { console.log("REGEX: only " + n + " builtins found"); process.exit(0); }
    if (noDoc.length) console.log("no description in scripts/builtin-docs.js: " + noDoc.join(", "));
    if (noEx.length) console.log("no example in scripts/builtin-examples.js: " + noEx.join(", "));
    if (done.length) console.log("now documented, remove from " + todoFile + ": " + done.join(", "));
    console.error("TODO_COUNT " + todo.size);
  ' 2>/tmp/hphp_check_todo)
  todo_count=$(sed -n 's/^TODO_COUNT //p' /tmp/hphp_check_todo 2>/dev/null)
  if [[ -z "$missing" ]]; then ok "every builtin has a description; example pages still owed: ${todo_count:-0} (scripts/builtin-examples-todo.txt)"
  else bad "builtin documentation incomplete:"; echo "$missing" | fold -w 100 | sed 's/^/        /'; fi
else
  bad "node not found: cannot check builtin docs"
fi

# 4. changelog
want="${RELEASE:-$VERSION}"
if [[ -f CHANGELOG.md ]] && grep -qE "^## \[?${want//./\\.}\]?( |$)" CHANGELOG.md; then
  ok "CHANGELOG.md has a section for $want"
else
  bad "CHANGELOG.md has no '## $want' section (write what changed for users)"
fi

# 5. hygiene
root_stray=$(git status --porcelain --untracked-files=normal 2>/dev/null | awk '$1=="??"{print $2}' | grep -v '/' || true)
tests_stray=$(git status --porcelain --untracked-files=all tests 2>/dev/null | awk '$1=="??"{print $2}' || true)
if [[ -z "$root_stray" ]]; then ok "no untracked files in the repo root"
else bad "untracked files in the repo root (commit them or move them to scratch/):"; echo "$root_stray" | sed 's/^/        /'; fi
if [[ -z "$tests_stray" ]]; then ok "no untracked files in tests/"
else bad "untracked files in tests/ (commit real tests, delete throwaway ones):"; echo "$tests_stray" | sed 's/^/        /'; fi

# 6. known failures still refer to real tests
if [[ -f tests/known-failures.txt ]]; then
  gone=$(grep -vE '^\s*(#|$)' tests/known-failures.txt | awk '{print $1}' | while read -r f; do [[ -e "$f" ]] || echo "$f"; done)
  if [[ -z "$gone" ]]; then ok "tests/known-failures.txt refers to existing tests"
  else bad "tests/known-failures.txt lists tests that no longer exist (remove them):"; echo "$gone" | sed 's/^/        /'; fi
fi

# 7. clean tree for a release
if [[ -n "$RELEASE" ]]; then
  dirty=$(git status --porcelain 2>/dev/null | grep -v '^?? scratch/' || true)
  if [[ -z "$dirty" ]]; then ok "working tree clean"
  else bad "working tree not clean ($(echo "$dirty" | wc -l) entries): commit your work before shipping"; echo "$dirty" | head -10 | sed 's/^/        /'; fi
fi

if [[ $problems -eq 0 ]]; then echo "check-repo ok"; exit 0; fi
echo "check-repo: $problems problem(s)"; exit 1
