# HolyPHP development quickstart
#
# Optimised for a local clone — no network required, no tool install needed.
#
# Sources compile from anywhere on disk. The temporary build tree (the CMake
# cache, generated C and object files) lives in the directory below by
# default, so a rebuild is fast even when the repo is on a restricted/network
# drive. Override with `set HPHP_BUILD_ROOT=...` before running the bat
# scripts, or `export HPHP_BUILD_ROOT=...` before the sh scripts.

# === Windows (cmd) ===
#  copy dev_scripts\compile.bat somewhere writable, e.g. C:\tmp
#  copy dev_scripts\run.bat    somewhere writable, e.g. C:\tmp
#  cd C:\tmp
#  compile.bat                 -> creates hphp.exe next to itself
#  run.bat examples\hello.hphp -> runs the program
#
#  The two scripts read their companion from the same folder, so they can be
#  renamed / moved together. Example:
#    mkdir C:\hp-build
#    copy dev_scripts\*.bat C:\hp-build\
#    cd C:\hp-build
#    compile.bat
#    run.bat examples\hello.hphp
#
#  To cross-compile for Linux (MinGW-w64 host):
#    copy dev_scripts\compile-mingw.bat C:\tmp
#    cd C:\tmp
#    compile-mingw.bat        -> creates hphp.exe that targets Linux
#
# === Windows (PowerShell) ===
#  cd C:\Users\elias\Downloads\HolyPHP
#  .\dev_scripts\compile.ps1   # writes hphp.exe into the current directory
#  .\dev_scripts\run.ps1 examples\hello.hphp
#
#  To pick a CMack build tree explicitly:
#    $env:HPHP_BUILD_ROOT = "C:\tmp\hphp-build"
#    .\dev_scripts\compile.ps1
#
# === Linux / WSL / MSYS2 (bash) ===
#  cd HolyPHP
#  bash dev_scripts/compile.sh   # writes ./hphp into the current directory
#  ./hphp run examples/hello.hphp
#
#  Bash is picky about the shebang; compile.sh is executable, so it can be
#  run directly too:  ./dev_scripts/compile.sh
#
#  To cross-compile for Linux from a MinGW-w64 or WSL host:
#    bash dev_scripts/compile-mingw.sh   # writes ./hphp (Linux target)

DEV_SCRIPTS_DIR := dev_scripts
BURNTHROUGH_DIR := burnthrough

TESTS_COVERAGE  := 98.1%
EXAMPLES_COVERAGE := 68.6%
BUILTIN_COVERAGE := 87.9%
GUIDE_COVERAGE := 79.2%

DEPLOY_HOST := hphdev.hphp.ch
DEPLOY_USER := root
DEPLOY_PORT := 22
