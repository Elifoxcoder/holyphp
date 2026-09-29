#!/usr/bin/env bash
# release.sh — cut a HolyPHP release: build, deploy, regenerate the winget manifest.
#
#   bash scripts/release.sh --version 1.0.1
#   bash scripts/release.sh --version 1.0.1 --no-deploy      # build + manifest only
#
# Steps:
#   1. build hphp.exe            (scripts/build.sh)
#   2. build the Inno installer  (packaging/windows/make-exe.ps1)
#   3. verify the binary still passes the thread/async self-tests
#   4. copy to the VPS /dl/ folder over ssh
#   5. verify the SHA256 on the VPS and over public HTTPS
#   6. rewrite the winget manifest with the new version, URL and hash
#   7. run `winget validate` on the result
#
# Nothing here pushes to git or opens a pull request — that is deliberately
# left to a human, because the winget-pkgs PR is a public, reviewed action.
set -euo pipefail
cd "$(dirname "$0")/.."

VERSION=""
DEPLOY=1
VPS_HOST="${HPHP_VPS_HOST:-root@91.216.248.93}"
VPS_DL_DIR="${HPHP_VPS_DL_DIR:-/var/www/hphp.ch/dl}"
SITE="${HPHP_SITE:-https://hphp.ch}"
REPO_SLUG="${HPHP_REPO_SLUG:-Elifoxcoder/holyphp}"

die() { echo "error: $*" >&2; exit 1; }

while [[ $# -gt 0 ]]; do
  case "$1" in
    --version)   VERSION="${2:-}"; shift 2 ;;
    --no-deploy) DEPLOY=0; shift ;;
    -h|--help)   sed -n '2,20p' "$0"; exit 0 ;;
    *)           die "unknown argument: $1" ;;
  esac
done

[[ -n "$VERSION" ]] || die "--version is required (e.g. --version 1.0.1)"
[[ "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || die "version must be x.y.z, got '$VERSION'"

# The compiler's own version string has to agree with the release, otherwise
# `hphp --version` reports something the installer never shipped.
src_version=$(sed -n 's/^#define HPHP_VERSION "\(.*\)"$/\1/p' src/hphpc/main.c)
if [[ "$src_version" != "$VERSION" ]]; then
  die "HPHP_VERSION in src/hphpc/main.c is '$src_version' but releasing '$VERSION' -- update it first"
fi

# NB: the #define line is indented in holyphp.iss, so anchor on the definition
# itself rather than at the start of the line.
setver() {
  sed -i "s/^\([[:space:]]*#define AppVersion \).*/\1\"$VERSION\"/" packaging/windows/holyphp.iss
}
setver
grep -q "\"$VERSION\"" packaging/windows/holyphp.iss \
  || die "could not set AppVersion to $VERSION in packaging/windows/holyphp.iss"

EXE="packaging/windows/out/HolyPHP-${VERSION}-x64-setup.exe"
MANIFEST_DIR="packaging/winget/manifests/h/HolyPHP/HolyPHP/${VERSION}"
ASSET_NAME="HolyPHP-${VERSION}-x64-setup.exe"
RELEASE_URL="https://github.com/${REPO_SLUG}/releases/download/v${VERSION}/${ASSET_NAME}"

echo "==> 1/7 building hphp.exe"
bash scripts/build.sh

echo "==> 2/7 building the installer"
# -Version is what make-exe.ps1 hands to iscc as /DAppVersion; without it the
# script falls back to its own 1.0.0 default and silently emits the wrong
# filename. The .iss #define below is only the fallback for manual iscc runs.
powershell -NoProfile -ExecutionPolicy Bypass -File packaging/windows/make-exe.ps1 -Version "$VERSION"
[[ -f "$EXE" ]] || die "installer not produced: $EXE (got: $(ls packaging/windows/out/*.exe 2>/dev/null | tr '\n' ' '))"

# The installer embeds packaging/windows/stage/hphp/hphp.exe, which used to go
# stale -- make-exe.ps1 only re-staged when the file was missing. If the staged
# copy ever drifts from the compiler we just built, the release is wrong, so
# check it here too rather than trusting the packaging step.
staged=packaging/windows/stage/hphp/hphp.exe
if [[ -f "$staged" ]]; then
  built_hash=$(sha256sum hphp.exe | cut -d' ' -f1)
  staged_hash=$(sha256sum "$staged" | cut -d' ' -f1)
  [[ "$built_hash" == "$staged_hash" ]] \
    || die "staged hphp.exe is stale ($staged_hash != $built_hash) -- the installer would ship the wrong compiler"
fi

# The staged compiler is what actually ships, so smoke-test that copy too.
if [[ -x "$staged" ]]; then
  staged_version=$("./$staged" --version | awk '{print $2}')
  [[ "$staged_version" == "$VERSION" ]] \
    || die "staged compiler reports '$staged_version' but this release is '$VERSION'"
fi

echo "==> 3/7 smoke-testing the compiler"
./hphp.exe --version
if [[ -f examples/thr_demo.hphp ]]; then
  ./hphp.exe run examples/thr_demo.hphp
fi

hash=$(sha256sum "$EXE" | cut -d' ' -f1)
size=$(stat -c%s "$EXE")
echo "    $ASSET_NAME  $size bytes  sha256=$hash"

if [[ $DEPLOY -eq 1 ]]; then
  echo "==> 4/7 uploading to $VPS_HOST:$VPS_DL_DIR"
  scp "$EXE" "$VPS_HOST:$VPS_DL_DIR/$ASSET_NAME"
  ssh "$VPS_HOST" "chmod 644 '$VPS_DL_DIR/$ASSET_NAME'"

  echo "==> 5/7 verifying on the VPS and over HTTPS"
  remote=$(ssh "$VPS_HOST" "sha256sum '$VPS_DL_DIR/$ASSET_NAME' | cut -d' ' -f1")
  [[ "$remote" == "$hash" ]] || die "VPS hash mismatch: $remote != $hash"
  echo "    VPS hash ok"

  # Only check the public URL when the file is expected to be served from there.
  if [[ "${HPHP_PUBLIC_URL:-}" ]]; then
    tmp=$(mktemp)
    curl -fsSL -o "$tmp" "$HPHP_PUBLIC_URL"
    got=$(sha256sum "$tmp" | cut -d' ' -f1); rm -f "$tmp"
    [[ "$got" == "$hash" ]] || die "public URL hash mismatch: $got != $hash"
    echo "    public URL hash ok"
  else
    echo "    (set HPHP_PUBLIC_URL to also verify the served file over HTTPS)"
  fi
fi

echo "==> 6/7 generating the winget manifest in $MANIFEST_DIR"
rm -rf "$MANIFEST_DIR"
mkdir -p "$MANIFEST_DIR"

cat > "$MANIFEST_DIR/HolyPHP.yaml" <<EOF
# yaml-language-server: \$schema=https://aka.ms/winget-manifest.version.1.9.0.schema.json

PackageIdentifier: HolyPHP.HolyPHP
PackageVersion: $VERSION
DefaultLocale: en-US
ManifestType: version
ManifestVersion: 1.9.0
EOF

cat > "$MANIFEST_DIR/HolyPHP.locale.en-US.yaml" <<EOF
# yaml-language-server: \$schema=https://aka.ms/winget-manifest.defaultLocale.1.9.0.schema.json

PackageIdentifier: HolyPHP.HolyPHP
PackageVersion: $VERSION
PackageLocale: en-US
Publisher: HolyPHP
PublisherUrl: $SITE/
PackageName: HolyPHP
PackageUrl: $SITE/
License: MIT
LicenseUrl: https://github.com/$REPO_SLUG/blob/main/LICENSE
ShortDescription: Compile HolyPHP programs into standalone native executables.
Moniker: hphp
Tags:
- cli
- command-line
- compiler
- developer-tools
- programming
- shell
- utilities
ManifestType: defaultLocale
ManifestVersion: 1.9.0
EOF

cat > "$MANIFEST_DIR/HolyPHP.installer.yaml" <<EOF
# yaml-language-server: \$schema=https://aka.ms/winget-manifest.installer.1.9.0.schema.json

PackageIdentifier: HolyPHP.HolyPHP
PackageVersion: $VERSION
Platform:
- Windows.Desktop
MinimumOSVersion: 10.0.17763.0
InstallerType: inno
Installers:
- Architecture: x64
  InstallerUrl: $RELEASE_URL
  InstallerSha256: ${hash^^}
ManifestType: installer
ManifestVersion: 1.9.0
EOF

echo "==> 7/7 validating the manifest"
if command -v winget >/dev/null 2>&1; then
  flat=$(mktemp -d)
  cp "$MANIFEST_DIR"/*.yaml "$flat"/
  if out=$(winget validate --manifest "$(cygpath -w "$flat")" 2>&1); then
    echo "$out"
  else
    echo "$out"
    rm -rf "$flat"
    die "winget validate failed"
  fi
  rm -rf "$flat"
else
  echo "    winget not on PATH -- skipped (validate on Windows before submitting)"
fi

cat <<EOF

release $VERSION is ready.

  installer  $EXE
  size       $size bytes
  sha256     $hash
  manifest   $MANIFEST_DIR
  url        $RELEASE_URL

NOTE: the manifest points at the GitHub release asset. Upload $ASSET_NAME
to https://github.com/$REPO_SLUG/releases/tag/v$VERSION or the manifest will
fail validation with Error-Installer-Availability.
EOF
