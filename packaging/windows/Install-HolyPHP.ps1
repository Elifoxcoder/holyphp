# Install-HolyPHP.ps1 -- HolyPHP MSIX install wizard.
#
# Users run:  Install-HolyPHP.ps1        (the MSIX wizard route -- the
# double-clickable setup.exe ships its own standard Inno Setup wizard and does
# not use this script)
# It must be executed *inside* the unpacked MSIX layout (the wizard ships in the
# package next to AppxManifest.xml) or with -PackageRoot pointing at one.
# It also accepts a "flat" layout (hphp.exe next to this script) for ad-hoc
# manual runs.
#
# What it does:
#   1. verifies the packaged hphp.exe runs and reports its version
#   2. copies the toolchain to %LOCALAPPDATA%\HolyPHP (no admin needed)
#   3. adds it to the *user* PATH (HKCU\Environment, no admin)
#   4. broadcasts WM_SETTINGCHANGE so new shells pick up the PATH
#   5. checks for gcc (compiled programs need it) and prints how to get it
#   6. notes that ui/websocket libraries come via `hphp pkg install ...`
#
# The installers only contain the toolchain: no gcc, and none of the
# lib/*.hphp libraries -- those are distributed through the package manager.

[CmdletBinding()]
param(
    [string]$PackageRoot = $PSScriptRoot,
    [switch]$Uninstall
)

$ErrorActionPreference = 'Stop'

# always leave a trace: console output alone is useless for post-mortems
try { Start-Transcript -Path (Join-Path $env:TEMP 'holyphp-install.log') -Force | Out-Null } catch {}

$InstallDir = Join-Path $env:LOCALAPPDATA 'HolyPHP'
$ToolchainDir = Join-Path $PackageRoot 'hphp'
$Version = '1.0.0'

function Write-Step($n, $msg)  { Write-Host "`n== [$n] $msg" -ForegroundColor Cyan }
function Write-Ok($msg)        { Write-Host "   OK  $msg" -ForegroundColor Green }
function Write-Info($msg)      { Write-Host "   ..  $msg" }
function Write-Warn2($msg)     { Write-Host "   !!  $msg" -ForegroundColor Yellow }

function Show-Banner {
    Write-Host ''
    Write-Host '  _  _  ___  ___  ___ ' -ForegroundColor Magenta
    Write-Host ' | || |/ _ \| _ )/ _ \' -ForegroundColor Magenta
    Write-Host ' | __ | (_) | _ \ (_) |' -ForegroundColor Magenta
    Write-Host ' |_||_|\___/|___/\___/  installer ' -ForegroundColor Magenta
    Write-Host "      HolyPHP v$Version -- compiled, memory-safe, PHP-flavored" -ForegroundColor DarkGray
    Write-Host ''
}

# ---------------------------------------------------------------- uninstall
if ($Uninstall) {
    Show-Banner
    Write-Step 1 "Removing $InstallDir"
    if (Test-Path $InstallDir) { Remove-Item -Recurse -Force $InstallDir; Write-Ok "deleted $InstallDir" }
    else                       { Write-Info 'nothing to delete' }

    Write-Step 2 'Removing PATH entry'
    $userPath = [Environment]::GetEnvironmentVariable('Path', 'User')
    $cleaned  = ($userPath -split ';' | Where-Object { $_ -and $_ -ne $InstallDir }) -join ';'
    [Environment]::SetEnvironmentVariable('Path', $cleaned, 'User')
    Write-Ok 'PATH cleaned'

    Write-Host "`nHolyPHP removed. Note: installed packages live in %APPDATA%\hphp --" -ForegroundColor DarkGray
    Write-Host 'delete that folder too if you want a full wipe.' -ForegroundColor DarkGray
    try { Stop-Transcript | Out-Null } catch {}
    exit 0
}

# ------------------------------------------------------------------- wizard
Show-Banner

if (-not (Test-Path (Join-Path $ToolchainDir 'hphp.exe'))) {
    # flat layout: the binary sits next to the script instead of under hphp\
    # -- stage it into the expected layout
    if (Test-Path (Join-Path $PackageRoot 'hphp.exe')) {
        New-Item -ItemType Directory -Force -Path $ToolchainDir | Out-Null
        Copy-Item -Force (Join-Path $PackageRoot 'hphp.exe') $ToolchainDir
    }
}
if (-not (Test-Path (Join-Path $ToolchainDir 'hphp.exe'))) {
    Write-Warn2 "no hphp\hphp.exe (or flat hphp.exe) found under '$PackageRoot'"
    Write-Info 'run this script from inside the unpacked MSIX layout (or -PackageRoot <dir>)'
    try { Stop-Transcript | Out-Null } catch {}
    exit 1
}

Write-Step 1 'Verifying the toolchain'
$ver = & (Join-Path $ToolchainDir 'hphp.exe') version   # prints to stdout, exits 0
if ($LASTEXITCODE -ne 0 -or -not $ver) {
    Write-Warn2 'hphp.exe did not run -- AV scanner or missing DLL?'
    try { Stop-Transcript | Out-Null } catch {}
    exit 1
}
Write-Ok $ver

Write-Step 2 "Installing to $InstallDir"
# on-access AV scanners can swallow freshly written exes on this kind of
# machine -- copy, verify, retry (same pattern the compiler itself uses)
$dest = Join-Path $InstallDir 'hphp.exe'
for ($i = 1; $i -le 3; $i++) {
    New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
    Copy-Item -Force (Join-Path $ToolchainDir 'hphp.exe') $InstallDir
    Start-Sleep -Milliseconds ($i * 400)
    if ((Test-Path $dest) -and (Get-Item $dest).Length -gt 0) { break }
    Write-Info "copy attempt $i lost (AV scanner?) -- retrying"
}
if (-not (Test-Path $dest) -or (Get-Item $dest).Length -eq 0) {
    Write-Warn2 'could not place hphp.exe -- your antivirus may be quarantining it.'
    Write-Info '  whitelist the HolyPHP folder and re-run this installer.'
    try { Stop-Transcript | Out-Null } catch {}
    exit 1
}
Write-Ok 'hphp.exe copied (runtime + stdlib embedded, no extra files)'

Write-Step 3 'Adding to PATH (user scope, no admin needed)'
$userPath = [Environment]::GetEnvironmentVariable('Path', 'User')
if (($userPath -split ';') -notcontains $InstallDir) {
    [Environment]::SetEnvironmentVariable('Path', "$userPath;$InstallDir".TrimStart(';'), 'User')
    Write-Ok "$InstallDir appended to user PATH"
} else {
    Write-Info 'already on PATH'
}
# nudge running shells
Add-Type -Namespace Win32 -Name NativeMethods -MemberDefinition @"
[DllImport("user32.dll", SetLastError = true, CharSet = CharSet.Auto)]
public static extern IntPtr SendMessageTimeout(
    IntPtr hWnd, uint Msg, UIntPtr wParam, string lParam,
    uint fuFlags, uint uTimeout, out UIntPtr lpdwResult);
"@
$HWND_BROADCAST = [IntPtr]0xFFFF
$WM_SETTINGCHANGE = 0x001A
$result = [UIntPtr]::Zero
[Win32.NativeMethods]::SendMessageTimeout($HWND_BROADCAST, $WM_SETTINGCHANGE,
    [UIntPtr]::Zero, 'Environment', 2, 5000, [ref]$result) | Out-Null
Write-Ok 'running shells notified (new terminals see it immediately)'
# keep an uninstaller next to the installed binary: the installer itself may
# have come from a package layout (MSIX) that no longer exists
Copy-Item -Force $PSCommandPath (Join-Path $InstallDir 'Uninstall-HolyPHP.ps1')

Write-Step 4 'Checking for gcc (compiled programs need it at build time)'
$gcc = Get-Command gcc -ErrorAction SilentlyContinue
if ($gcc) { Write-Ok "found $($gcc.Source)" }
else {
    Write-Warn2 'gcc not found on PATH'
    Write-Info '  hphp itself runs fine, but `hphp build/run` shells out to gcc.'
    Write-Info '  install one of:  winget install MSYS2.MSYS2   |   choco install mingw'
}

Write-Step 5 'Libraries'
Write-Info 'ui and websocket are NOT bundled -- install them with the manager:'
Write-Host  '      hphp pkg registry http://your-server:8930   # once (optional)' -ForegroundColor White
Write-Host  '      hphp install ui' -ForegroundColor White
Write-Host  '      hphp install websocket' -ForegroundColor White

Write-Host ''
Write-Host 'HolyPHP installed.' -ForegroundColor Green
Write-Host '  open a NEW terminal and try:  hphp run hello.hphp' -ForegroundColor White
$Uninstaller = Join-Path $InstallDir 'Uninstall-HolyPHP.ps1'
Write-Host "  uninstall any time:           powershell -ExecutionPolicy Bypass -File `"$Uninstaller`" -Uninstall" -ForegroundColor DarkGray
Write-Host ''
try { Stop-Transcript | Out-Null } catch {}
