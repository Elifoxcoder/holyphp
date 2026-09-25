# HolyPHP — Windows installer (MSIX)

Ships **the compiler only**: `hphp.exe` with runtime + stdlib embedded. No gcc,
and no libraries — `ui` / `websocket` are installed through the package manager
afterwards (`hphp install ui`, `hphp install websocket`).

## Build

```powershell
bash scripts/build.sh                                # -> hphp.exe at repo root
powershell -File packaging/windows/make-msix.ps1     # -> packaging/windows/out/HolyPHP-1.0.0-x64.msix
powershell -File packaging/windows/sign-msix.ps1     # sign with the HolyPHP dev cert
powershell -File packaging/windows/make-exe.ps1      # -> packaging/windows/out/HolyPHP-1.0.0-x64-setup.exe
```

`make-exe.ps1` builds the standard setup.exe with [Inno Setup](https://jrsoftware.org/isinfo.php).
If Inno Setup is missing it is downloaded from jrsoftware.org and installed
per-user (`%LOCALAPPDATA%\Programs\Inno Setup 6`, no admin, no account), so the
build needs no Windows SDK, no signing certificate and no developer account.
Pass `-NoBootstrap` to require an existing installation instead.

The setup.exe is a normal Windows installer: welcome page, install-folder
choice (default `%LOCALAPPDATA%\Programs\HolyPHP`), an "Add HolyPHP to your
PATH" checkbox, a gcc note on the finish page when gcc is missing, and a
proper uninstaller listed under **Apps & features**. A legacy
`%LOCALAPPDATA%\HolyPHP` install from the old script wizard is offered for
cleanup on first run.

Needs the [Windows SDK](https://developer.microsoft.com/windows/downloads/windows-sdk/)
(`makeappx` + `signtool`): `winget install Microsoft.WindowsSDK.10.0.18362`
works (the unversioned `Microsoft.WindowsSDK` id may not resolve on winget).

## Install (the wizard)

Two ways:

1. **MSIX route** — sign + `Add-AppxPackage` (the `make-msix.ps1` output prints
   the exact commands). Installing the package is enough: the manifest's app
   execution alias puts `hphp.exe` on your PATH (via the per-user
   `WindowsApps` folder) the moment the package lands. No wizard needed.
2. **Wizard-only route** — no MSIX tooling or signing needed:

   ```powershell
   powershell -ExecutionPolicy Bypass -File packaging/windows/Install-HolyPHP.ps1
   ```

   (`make-msix.ps1` stages the layout first; pass `-PackageRoot <dir>` to point
   the wizard at any unpacked package layout.)

Either way you get:

1. verifies the packaged `hphp.exe` actually runs
2. copies it to `%LOCALAPPDATA%\HolyPHP` (no admin rights needed) — with a
   verify-and-retry loop, because on-access AV scanners occasionally swallow
   freshly written exes on Windows (the compiler fights the same battle)
3. appends that folder to the **user PATH** (`HKCU\Environment`) and broadcasts
   `WM_SETTINGCHANGE`, so new terminals see `hphp` immediately
4. checks for `gcc` and, if missing, tells you how to get one
   (`winget install MSYS2.MSYS2` / `choco install mingw`) — `hphp run/build`
   shells out to gcc to compile your programs
5. prints the `hphp pkg` commands for the `ui` / `websocket` libraries

## Uninstall

```powershell
powershell -ExecutionPolicy Bypass -Command "& '.\packaging\windows\Install-HolyPHP.ps1' -Uninstall"
```

Removes `%LOCALAPPDATA%\HolyPHP` and its PATH entry. (Packages installed via
the manager live in `%APPDATA%\hphp` — delete that folder for a full wipe.)

## Signing notes

`make-msix.ps1` packs but does not sign; `sign-msix.ps1` signs with the
self-signed `HolyPHP-dev.pfx` (password `holyphp-dev`, header comment shows how
to recreate it). Before `Add-AppxPackage` accepts the package on a machine,
the cert's public part must be in `Cert:\LocalMachine\TrustedPeople` (admin,
one-time) — `sign-msix.ps1` prints the exact three commands.

Never call `signtool.exe` from Git Bash/MSYS: it rewrites the `/fd` `/f` `/p`
switches into Windows paths and signtool then fails with a misleading
"multiple certificates" error. Run it via `sign-msix.ps1` or plain PowerShell.
