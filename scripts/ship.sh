#!/usr/bin/env bash
# ship.sh — the ONE way a change reaches users. A task is done when this ends
# with "ship ok". Every step stops the script on failure; nothing is skipped
# silently. Run from Git Bash on the Windows machine (it builds hphp.exe and
# the Windows installer, and the Linux packages through WSL).
#
#   bash scripts/ship.sh --version 1.0.3            # everything
#   bash scripts/ship.sh --version 1.0.3 --local    # build, test, docs, packages; no push/release/deploy
#
# Options:
#   --local         no git push, no GitHub release, no VPS upload or deploy
#   --no-linux      skip the .deb/AppImage (then the GitHub release has no Linux assets — say so)
#   --no-vsix-install  build the VSIX but do not install it into VS Code
#
# Steps:
#    1  preflight      on main, up to date with origin, no uncommitted changes to tracked
#                      files, gh/ssh reachable (unless --local)
#    2  changelog      "## Unreleased" becomes "## <version> — <date>"
#    3  version        scripts/set-version.sh <version>
#    4  check-repo     versions, server address, builtin docs, changelog, hygiene
#    5  build          scripts/build.sh -> hphp.exe
#    6  tests          tests/run.sh all; failures must be listed in tests/known-failures.txt,
#                      and listed tests that now pass must be removed from it
#    7  docs           builtins.json, builtins.html, per-builtin pages, playground
#                      completions, and every builtin example actually run
#    8  extension      scripts/vsix.sh
#    9  local install  copy hphp.exe over the compiler in the PATH
#   10  windows        scripts/release.sh (installer, upload to the VPS, winget manifest)
#   11  linux          packaging/linux/make-deb.sh + make-appimage.sh (in WSL)
#   12  commit + tag   only the files this script generated, then tag v<version>
#   13  push           main and the tag to origin (GitHub)
#   14  github release v<version> with the installers; notes = the changelog section
#   15  deploy         scripts/deploy-vps.sh (compiler, website, playground, live checks)
#   16  verify         the VPS compiler, the GitHub release and the website show <version>
set -euo pipefail
cd "$(dirname "$0")/.."
# shellcheck source=scripts/vps.env
source scripts/vps.env

VERSION=""; LOCAL=0; LINUX=1; VSIX_INSTALL=1
WSL_DISTRO="${HPHP_WSL_DISTRO:-Ubuntu-24.04}"
while [[ $# -gt 0 ]]; do
  case "$1" in
    --version) VERSION="${2:-}"; shift 2 ;;
    --local) LOCAL=1; shift ;;
    --no-linux) LINUX=0; shift ;;
    --no-vsix-install) VSIX_INSTALL=0; shift ;;
    -h|--help) sed -n '2,37p' "$0"; exit 0 ;;
    *) echo "ship: unknown argument $1" >&2; exit 2 ;;
  esac
done

die()  { printf '\nSHIP FAILED at step %s: %s\n' "${STEP:-?}" "$*" >&2; exit 1; }
step() { STEP="$1"; shift; printf '\n== %s/16 %s\n' "$STEP" "$*"; }
trap 'die "command failed (line $LINENO)"' ERR

[[ "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo "usage: ship.sh --version x.y.z [--local] [--no-linux]" >&2; exit 2; }
TAG="v$VERSION"

# Files this script may change; only these are committed in step 12.
GENERATED=(CHANGELOG.md src/hphpc/main.c src/hphpc/runtime/hphp_std.c
  packaging/windows/holyphp.iss packaging/linux packaging/winget/manifests
  docs extension/hphp/data packages/extension/hphp/data tests/known-failures.txt
  src/hphpc/runtime_embed.h packages/extension/hphp/extension.bundle.js
  packages/extension/hphp/server.bundle.js packages/extension/hphp/syntaxes
  packages/extension/hphp/package.json packages/extension/hphp/holyphp-*.vsix)

# ---------------------------------------------------------------- 1
step 1 "preflight"
branch=$(git rev-parse --abbrev-ref HEAD)
[[ "$branch" == "main" ]] || die "releases come from main, this is '$branch' (merge your branch first)"
# Leftovers of an earlier, aborted ship run are fine: generated output, and the
# two source files when only their HPHP_VERSION line differs. Anything else is
# real work that has to be committed first.
dirty=$(git status --porcelain --untracked-files=no | awk '{print $2}' | while read -r f; do
  case "$f" in
    CHANGELOG.md|tests/known-failures.txt|docs/*|extension/hphp/data/*|packages/extension/hphp/data/*|packaging/*) ;;
    src/hphpc/main.c|src/hphpc/runtime/hphp_std.c)
      if git diff -U0 -- "$f" | grep -E '^[-+][^-+]' | grep -vqE '^[-+]#define HPHP_VERSION '; then echo "$f"; fi ;;
    *) echo "$f" ;;
  esac
done)
[[ -z "$dirty" ]] || die "uncommitted changes to tracked files -- commit your work first:
$(echo "$dirty" | head -15)"
if git rev-parse -q --verify "refs/tags/$TAG" >/dev/null; then die "tag $TAG already exists -- pick the next version"; fi
if [[ $LOCAL -eq 0 ]]; then
  git fetch -q origin
  behind=$(git rev-list --count HEAD..origin/main)
  [[ "$behind" == 0 ]] || die "main is $behind commit(s) behind origin/main -- pull first"
  command -v gh >/dev/null || die "gh (GitHub CLI) not installed"
  gh auth status >/dev/null 2>&1 || die "gh is not logged in (gh auth login)"
  ssh -o BatchMode=yes -o ConnectTimeout=10 "$HPHP_VPS_HOST" true \
    || die "cannot reach $HPHP_VPS_HOST over ssh (VPN? key? address in scripts/vps.env?)"
fi
echo "  main, clean, $([[ $LOCAL -eq 1 ]] && echo 'local run' || echo "origin up to date, gh + ssh $HPHP_VPS_HOST ok")"

# ---------------------------------------------------------------- 2
step 2 "changelog"
if grep -qE "^## \[?${VERSION//./\\.}\]?( |$)" CHANGELOG.md; then
  echo "  CHANGELOG.md already has $VERSION"
else
  body=$(awk '/^## Unreleased/{f=1;next} /^## /{f=0} f' CHANGELOG.md | grep -v '^\s*$' || true)
  [[ -n "$body" ]] || die "CHANGELOG.md: nothing under '## Unreleased' -- write what changed for users"
  sed -i "s/^## Unreleased$/## Unreleased\n\n## $VERSION — $(date +%Y-%m-%d)/" CHANGELOG.md
  echo "  'Unreleased' recorded as $VERSION"
fi

# ---------------------------------------------------------------- 3
step 3 "version"
bash scripts/set-version.sh "$VERSION"

# ---------------------------------------------------------------- 4
step 4 "check-repo"
bash scripts/check-repo.sh || die "fix the problems above"

# ---------------------------------------------------------------- 5
step 5 "build"
bash scripts/build.sh 2>&1 | tee /tmp/hphp_ship_build.log | tail -3
grep -q '^build ok' /tmp/hphp_ship_build.log || die "build did not end with 'build ok'"
grep -q 'error:' /tmp/hphp_ship_build.log && die "build log contains 'error:'"
got=$(./hphp.exe --version | awk '{print $2}')
[[ "$got" == "$VERSION" ]] || die "hphp.exe reports '$got', expected $VERSION"

# ---------------------------------------------------------------- 6
step 6 "tests"
bash tests/run.sh all > /tmp/hphp_ship_tests.log 2>&1 || true   # failures are judged below (known-failures.txt); "|| true" keeps the ERR trap quiet
tail -1 /tmp/hphp_ship_tests.log
known=$(grep -vE '^\s*(#|$)' tests/known-failures.txt 2>/dev/null | awk '{print $1}' | sort -u || true)
failed=$(grep '^FAIL ' /tmp/hphp_ship_tests.log | awk '{print $2}' | sort -u || true)
new_fail=$(comm -23 <(echo "$failed") <(echo "$known") | grep . || true)
now_pass=$(comm -13 <(echo "$failed") <(echo "$known") | grep . || true)
[[ -z "$new_fail" ]] || die "new test failures (fix them; tests/known-failures.txt is not a bin):
$new_fail"
[[ -z "$now_pass" ]] || die "these known failures pass now -- delete them from tests/known-failures.txt:
$now_pass"
echo "  no new failures ($(echo "$known" | grep -c . || true) known)"

# ---------------------------------------------------------------- 7
step 7 "docs"
node scripts/genext.js
node scripts/gendocs.js
node scripts/gensite.js     # libraries/, docs.html, redirects (after gendocs: reads builtins.html)
node scripts/genbuiltinsites.js
[[ -f scripts/gen-playground-completions.mjs ]] && node scripts/gen-playground-completions.mjs
node scripts/check-examples.js

# ---------------------------------------------------------------- 8
step 8 "extension"
if [[ $VSIX_INSTALL -eq 1 ]]; then bash scripts/vsix.sh; else bash scripts/vsix.sh --no-install; fi

# ---------------------------------------------------------------- 9
step 9 "local install"
dest="${LOCALAPPDATA:-}/Programs/HolyPHP"
if [[ -n "${LOCALAPPDATA:-}" && -d "$dest" ]]; then
  cp hphp.exe "$dest/hphp.exe"
  echo "  $dest/hphp.exe -> $("$dest/hphp.exe" --version)"
else
  echo "  no installed compiler at \$LOCALAPPDATA/Programs/HolyPHP -- nothing to replace"
fi

# ---------------------------------------------------------------- 10
step 10 "windows installer"
if [[ $LOCAL -eq 1 ]]; then bash scripts/release.sh --version "$VERSION" --no-deploy
else bash scripts/release.sh --version "$VERSION"; fi
WIN_EXE="packaging/windows/out/HolyPHP-${VERSION}-x64-setup.exe"
[[ -f "$WIN_EXE" ]] || die "missing $WIN_EXE"

# ---------------------------------------------------------------- 11
step 11 "linux packages"
DEB="packaging/linux/out/holyphp_${VERSION}_amd64.deb"
APPIMG="packaging/linux/out/HolyPHP-${VERSION}-x86_64.AppImage"
if [[ $LINUX -eq 1 ]]; then
  wsl_dir=$(wsl.exe -d "$WSL_DISTRO" wslpath -a "$(pwd -W)" | tr -d '\r')
  wsl.exe -d "$WSL_DISTRO" -- bash -lc "cd '$wsl_dir' && bash packaging/linux/make-deb.sh && bash packaging/linux/make-appimage.sh"
  [[ -f "$DEB" && -f "$APPIMG" ]] || die "linux packages not produced ($DEB, $APPIMG)"
else
  echo "  skipped (--no-linux): the release will have no Linux packages"
fi

# ---------------------------------------------------------------- 12
step 12 "commit + tag"
existing=()
for p in "${GENERATED[@]}"; do [[ -e "$p" ]] && existing+=("$p"); done
git add -- "${existing[@]}"
if git diff --cached --quiet; then echo "  nothing generated changed"
else git commit -q -m "Release $VERSION" && echo "  committed: $(git log -1 --format='%h %s')"; fi
git tag -a "$TAG" -m "HolyPHP $VERSION"
bash scripts/check-repo.sh --release "$VERSION" || die "repo not clean after the release commit (see above)"

if [[ $LOCAL -eq 1 ]]; then
  printf '\nship ok (local): %s built, tested and packaged; NOT pushed, released or deployed.\n' "$VERSION"
  echo "  undo the local tag with: git tag -d $TAG"
  exit 0
fi

# ---------------------------------------------------------------- 13
step 13 "push"
git push -q origin main
git push -q origin "$TAG"

# ---------------------------------------------------------------- 14
step 14 "github release"
notes=$(awk -v v="$VERSION" '$0 ~ "^## \\[?"v"\\]?( |$)" {f=1; next} /^## /{f=0} f' CHANGELOG.md)
assets=("$WIN_EXE"); [[ $LINUX -eq 1 ]] && assets+=("$DEB" "$APPIMG")
gh release create "$TAG" "${assets[@]}" --repo "$HPHP_REPO_SLUG" --title "HolyPHP $VERSION" --notes "$notes"

# ---------------------------------------------------------------- 15
step 15 "deploy"
bash scripts/deploy-vps.sh

# ---------------------------------------------------------------- 16
step 16 "verify"
remote=$(ssh "$HPHP_VPS_HOST" "/opt/hphp/hphp --version" | awk '{print $2}')
[[ "$remote" == "$VERSION" ]] || die "VPS compiler reports '$remote'"
latest=$(gh release view --repo "$HPHP_REPO_SLUG" --json tagName -q .tagName)
[[ "$latest" == "$TAG" ]] || die "latest GitHub release is '$latest', not $TAG"
code=$(curl -s -o /dev/null -w '%{http_code}' "$HPHP_SITE/")
[[ "$code" == 200 ]] || die "$HPHP_SITE answers $code"

printf '\nship ok: HolyPHP %s is on GitHub (%s), released with installers, and live on %s.\n' \
  "$VERSION" "$TAG" "$HPHP_SITE"
