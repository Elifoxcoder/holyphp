#!/usr/bin/env bash
# build-linux.sh — build the `hphp` toolchain binary on Linux.
#
#   bash packaging/linux/build-linux.sh
#
# This is the entry point the .deb and AppImage scripts use; it delegates to
# scripts/build.sh, which has a POSIX branch (gcc driver link, ELF output) and
# also builds + embeds the vendored SQLite. One build path means the Linux
# packages ship the same compiler features as the Windows release, including
# the sqlite_* and bt_* builtins that work without a system SQLite.
#
# Output: ./hphp at the repo root (used by make-deb.sh / make-appimage.sh).
set -euo pipefail
cd "$(dirname "$0")/../.."          # repo root
bash scripts/build.sh
