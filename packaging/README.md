# HolyPHP installers

Self-contained install wizards for the **compiler only**. Every package carries
the same thing: the single `hphp` binary with the runtime and standard library
embedded. **No libraries ship in the installers** — `ui`, `websocket`, and
anything else is distributed through the built-in package manager:

```bash
hphp install ui
hphp install websocket
```

| Target | Builder | Result | PATH strategy |
|---|---|---|---|
| Windows (MSIX) | `packaging/windows/make-msix.ps1` | `HolyPHP-1.0.0-x64.msix` | App execution alias in `%LOCALAPPDATA%\Microsoft\WindowsApps` (already on PATH); wizard fallback for the no-MSIX route |
| Windows (setup.exe) | `packaging/windows/make-exe.ps1` | `HolyPHP-1.0.0-x64-setup.exe` | Standard Inno Setup wizard (choose folder, PATH checkbox, Apps & features uninstaller); per-user, no admin |
| Windows (wizard only) | `Install-HolyPHP.ps1` | install to `%LOCALAPPDATA%\HolyPHP` | Appends to the user PATH (`HKCU\Environment`), no admin |
| Debian/Ubuntu | `packaging/linux/make-deb.sh` | `holyphp_1.0.0_amd64.deb` | `/usr/bin/hphp` — on PATH for all users |
| Linux (AppImage) | `packaging/linux/make-appimage.sh` | `HolyPHP-1.0.0-x86_64.AppImage` | Zero-install, or `AppImage install` prepends itself to `~/.profile` / `~/.bashrc` |

Every wizard also verifies the binary runs, warns when `gcc` is missing
(`hphp run/build` shells out to it to compile your programs), and prints the
`hphp pkg install` commands for the libraries.

## Build one

```powershell
# Windows — needs hphp.exe at repo root (bash scripts/build.sh)
powershell -File packaging/windows/make-msix.ps1
powershell -File packaging/windows/make-exe.ps1   # bootstraps Inno Setup per-user if missing
```

```bash
# Linux/WSL — builds ./hphp itself if missing
bash packaging/linux/make-deb.sh
bash packaging/linux/make-appimage.sh
```

Artifacts land in `packaging/*/out/`. See `windows/README.md` and
`linux/README.md` for signing, install, and uninstall details.
