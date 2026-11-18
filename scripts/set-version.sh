#!/usr/bin/env bash
# set-version.sh — set the HolyPHP version everywhere it is written down.
#
#   bash scripts/set-version.sh 1.0.3     # write it
#   bash scripts/set-version.sh --check   # only report; exit 1 if the places disagree
#
# Places (keep this list complete — check-repo.sh relies on it):
#   src/hphpc/main.c                       #define HPHP_VERSION   (hphp --version)
#   src/hphpc/runtime/hphp_std.c           #define HPHP_VERSION   (phpversion() in programs)
#   packaging/windows/holyphp.iss          #define AppVersion     (Windows installer)
#   packaging/linux/make-deb.sh            VERSION=
#   packaging/linux/make-appimage.sh       VERSION=
#   packaging/linux/debian/DEBIAN/control  Version:
#   packaging/linux/README.md              file names in the examples
#
# The VS Code extension (packages/extension/hphp/package.json) has its own
# version line and is not touched here.
set -euo pipefail
cd "$(dirname "$0")/.."

die() { echo "set-version: $*" >&2; exit 1; }

read_all() {
  printf '%s\t%s\n' \
    "src/hphpc/main.c"                      "$(sed -n 's/^#define HPHP_VERSION "\(.*\)"$/\1/p' src/hphpc/main.c)" \
    "src/hphpc/runtime/hphp_std.c"          "$(sed -n 's/^#define HPHP_VERSION "\(.*\)"$/\1/p' src/hphpc/runtime/hphp_std.c)" \
    "packaging/windows/holyphp.iss"         "$(sed -n 's/^[[:space:]]*#define AppVersion "\(.*\)"$/\1/p' packaging/windows/holyphp.iss)" \
    "packaging/linux/make-deb.sh"           "$(sed -n 's/^VERSION=\(.*\)$/\1/p' packaging/linux/make-deb.sh)" \
    "packaging/linux/make-appimage.sh"      "$(sed -n 's/^VERSION=\(.*\)$/\1/p' packaging/linux/make-appimage.sh)" \
    "packaging/linux/debian/DEBIAN/control" "$(sed -n 's/^Version: \(.*\)$/\1/p' packaging/linux/debian/DEBIAN/control)"
}

check() {
  local want="${1:-}" bad=0 first=""
  while IFS=$'\t' read -r file ver; do
    [[ -n "$ver" ]] || { echo "  $file: version line not found"; bad=1; continue; }
    [[ -n "$first" ]] || first="$ver"
    local ref="${want:-$first}"
    if [[ "$ver" != "$ref" ]]; then echo "  $file: $ver (expected $ref)"; bad=1; fi
  done < <(read_all)
  return $bad
}

if [[ "${1:-}" == "--check" ]]; then
  if check; then echo "versions consistent: $(read_all | head -1 | cut -f2)"; exit 0; fi
  echo "versions disagree (run: bash scripts/set-version.sh <x.y.z>)"; exit 1
fi

NEW="${1:-}"
[[ "$NEW" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || die "usage: set-version.sh <x.y.z> | --check"
OLD=$(sed -n 's/^#define HPHP_VERSION "\(.*\)"$/\1/p' src/hphpc/main.c)

sed -i "s/^#define HPHP_VERSION \".*\"$/#define HPHP_VERSION \"$NEW\"/" src/hphpc/main.c src/hphpc/runtime/hphp_std.c
sed -i "s/^\([[:space:]]*#define AppVersion \).*/\1\"$NEW\"/" packaging/windows/holyphp.iss
sed -i "s/^VERSION=.*/VERSION=$NEW/" packaging/linux/make-deb.sh packaging/linux/make-appimage.sh
sed -i "s/^Version: .*/Version: $NEW/" packaging/linux/debian/DEBIAN/control
if [[ -n "$OLD" && "$OLD" != "$NEW" ]]; then
  sed -i "s/holyphp_${OLD//./\\.}_amd64/holyphp_${NEW}_amd64/g; s/HolyPHP-${OLD//./\\.}-x86_64/HolyPHP-${NEW}-x86_64/g" packaging/linux/README.md
fi

check "$NEW" || die "some places did not take the new version (see above)"
echo "version set to $NEW (was ${OLD:-unknown})"
