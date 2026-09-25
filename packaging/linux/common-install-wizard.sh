#!/bin/sh
# common-install-wizard.sh — shared installer logic for the Linux packages.
#
# Two ways to use it:
#   * sourced (functions become available), or
#   * executed with a subcommand, which is what debian/postinst does:
#       common-install-wizard.sh banner            print the header
#       common-install-wizard.sh verify <bin>      check the binary runs
#       common-install-wizard.sh path <dir>        add <dir> to PATH (user)
#       common-install-wizard.sh gcc               check for gcc
#       common-install-wizard.sh libs              package-manager hints
#       common-install-wizard.sh done              closing hint
#
# The packages ship ONLY the compiler (runtime + stdlib embedded). The
# ui/websocket libraries are not included — users install them via
#   hphp pkg install ui
#   hphp pkg install websocket

hphp_banner() {
    echo ""
    echo "  HolyPHP installer"
    echo "  -----------------"
}

# verify <hphp-binary> — run it once and show its help line
hphp_verify() {
    if "$1" --help >/dev/null 2>&1; then
        echo "  [ok]   compiler runs: $1"
        return 0
    fi
    echo "  [FAIL] $1 did not run (missing loader? run 'ldd \"$1\"')"
    return 1
}

# add_to_path <dir> — ensure <dir> is on PATH for login + interactive shells,
# without admin rights. Writes ~/.profile and ~/.bashrc markers (idempotent).
hphp_add_to_path() {
    _dir="$1"
    _marker="# added by HolyPHP installer"
    _changed=0

    for _rc in "$HOME/.profile" "$HOME/.bashrc"; do
        [ -f "$_rc" ] || touch "$_rc"
        if ! grep -qs "$_marker" "$_rc"; then
            {
                echo ""
                echo "$_marker"
                echo "case \":\$PATH:\" in *\":$_dir:\"*) ;; *) PATH=\"$_dir:\$PATH\" ;; esac"
            } >> "$_rc"
            _changed=1
        fi
    done

    if [ "$_changed" = 1 ]; then
        echo "  [ok]   $_dir prepended to PATH in ~/.profile and ~/.bashrc"
        echo "         (open a new shell, or: source ~/.profile)"
    else
        echo "  [ok]   PATH already set up"
    fi
}

# check_gcc — hphp shells out to gcc to compile programs; warn when missing
hphp_check_gcc() {
    if command -v gcc >/dev/null 2>&1; then
        echo "  [ok]   gcc found: $(command -v gcc)"
    else
        echo "  [warn] gcc not found — 'hphp run/build' needs it to compile programs."
        echo "         install it with:  sudo apt install build-essential"
    fi
}

# libs_hint — the libraries ride the package manager, not the installer
hphp_libs_hint() {
    echo "  [info] ui and websocket libraries are NOT bundled."
    echo "         install them with the package manager:"
    echo "            hphp install ui"
    echo "            hphp install websocket"
    echo "         (set a registry first if you run your own:"
    echo "            hphp pkg registry http://your-server:8930 )"
}

hphp_done_hint() {
    echo ""
    echo "  Done. Try:  hphp run hello.hphp"
    echo ""
}

# ---- CLI dispatch (used by debian/postinst) --------------------------------
if [ "$#" -gt 0 ] && [ -n "$1" ]; then
    case "$1" in
        banner) hphp_banner ;;
        verify) hphp_verify "${2:?usage: $0 verify <binary>}" ;;
        path)   hphp_add_to_path "${2:?usage: $0 path <dir>}" ;;
        gcc)    hphp_check_gcc ;;
        libs)   hphp_libs_hint ;;
        done)   hphp_done_hint ;;
        *) echo "common-install-wizard.sh: unknown step '$1'" >&2; exit 1 ;;
    esac
fi
