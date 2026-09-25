# make-exe.ps1 -- build the HolyPHP setup.exe with Inno Setup.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File packaging/windows/make-exe.ps1
#
# Inno Setup is the standard toolkit for Windows installers: the output is a
# normal setup wizard (welcome -> install folder -> PATH checkbox -> progress
# -> finish) that registers an "Apps & features" uninstall entry. Like the old
# IExpress build it needs NO Windows SDK, NO code-signing certificate and NO
# Microsoft developer account -- and unlike IExpress it does not look like a
# 2005 self-extractor.
#
# If Inno Setup is not installed, the script downloads it from jrsoftware.org
# and installs it per-user (%LOCALAPPDATA%\Programs\Inno Setup 6, no admin).
# Pass -NoBootstrap to skip that and fail with instructions instead.
#
#   Output: packaging/windows/out/HolyPHP-<version>-x64-setup.exe
#

param(
    [string]$Version = '1.0.0',
    [switch]$NoBootstrap
)

$ErrorActionPreference = 'Stop'

$root   = Split-Path (Split-Path $PSScriptRoot)   # repo root
$stage  = Join-Path $PSScriptRoot 'stage'
$out    = Join-Path $PSScriptRoot 'out'
New-Item -ItemType Directory -Force -Path $out | Out-Null

# ---------------------------------------------------------------- 1. payload
$toolchain = Join-Path $stage 'hphp\hphp.exe'
if (-not (Test-Path $toolchain)) {
    if (Test-Path (Join-Path $root 'hphp.exe')) {
        Write-Host 'staging hphp.exe from repo root'
    } else {
        Write-Host 'hphp.exe not found -- building it first (bash scripts/build.sh)'
        Push-Location $root
        try { bash scripts/build.sh } finally { Pop-Location }
        if (-not (Test-Path (Join-Path $root 'hphp.exe'))) {
            throw 'build did not produce hphp.exe at the repo root'
        }
    }
    New-Item -ItemType Directory -Force -Path (Join-Path $stage 'hphp') | Out-Null
    Copy-Item -Force (Join-Path $root 'hphp.exe') $toolchain
}

# ------------------------------------------------------------- 2. find ISCC
function Find-ISCC {
    $cmd = Get-Command iscc.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    foreach ($base in @(${env:ProgramFiles(x86)}, $env:ProgramFiles, "$env:LOCALAPPDATA\Programs")) {
        if (-not $base) { continue }
        # install dir is 'Inno Setup 6' for the machine-wide default, but the
        # per-user install may land in plain 'Inno' -- search one level deep
        $found = Get-ChildItem -Path $base -Filter 'ISCC.exe' -Recurse -Depth 1 -ErrorAction SilentlyContinue |
                 Select-Object -First 1
        if ($found) { return $found.FullName }
    }
    return $null
}

$iscc = Find-ISCC

# ------------------------------------------------- 3. bootstrap Inno Setup
if (-not $iscc -and -not $NoBootstrap) {
    Write-Host 'Inno Setup not found -- installing it per-user (no admin needed)'
    try { [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12 } catch {}

    # resolve the current release from the official download page, with a
    # known-good fallback in case the page layout changes
    $fallbackVersion = '6.7.3'
    $dlVersion = $fallbackVersion
    try {
        $page = Invoke-WebRequest -UseBasicParsing 'https://jrsoftware.org/isdl.php'
        if ($page.Content -match 'innosetup-([0-9.]+)\.exe') { $dlVersion = $Matches[1] }
    } catch { Write-Warning "could not read jrsoftware.org ($($_.Exception.Message)) -- falling back to Inno Setup $fallbackVersion" }

    # jrsoftware.org/download.php serves an HTML redirect page, not the file;
    # the release binaries live on GitHub
    $url = "https://github.com/jrsoftware/issrc/releases/download/is-$($dlVersion.Replace('.', '_'))/innosetup-$dlVersion.exe"
    $installer = Join-Path $env:TEMP "innosetup-$dlVersion.exe"
    Write-Host "downloading $url"
    for ($attempt = 1; $attempt -le 2; $attempt++) {
        Invoke-WebRequest -UseBasicParsing -Uri $url -OutFile $installer
        # AV scanners and truncated downloads leave garbage behind; a real
        # installer is an MZ executable of several megabytes
        $bytes = [IO.File]::ReadAllBytes($installer)
        if ($bytes.Length -gt 1MB -and $bytes[0] -eq 0x4D -and $bytes[1] -eq 0x5A) { break }
        if ($attempt -eq 2) { throw "downloaded installer looks wrong ($($bytes.Length) bytes) -- delete $installer and retry, or install manually: winget install JRSoftware.InnoSetup" }
        Write-Warning 'download looks wrong (redirect page or truncated) -- retrying'
        Remove-Item -Force $installer
    }

    $innoDir = "$env:LOCALAPPDATA\Programs\Inno Setup 6"
    $p = Start-Process -FilePath $installer -Wait -PassThru -ArgumentList @(
        '/VERYSILENT', '/NORESTART', '/SUPPRESSMSGBOXES', '/CURRENTUSER', "/DIR=$innoDir"
    )
    if ($p.ExitCode -ne 0) {
        throw "Inno Setup installer exited with $($p.ExitCode). Install it manually: winget install JRSoftware.InnoSetup"
    }
    $iscc = Find-ISCC
    if (-not $iscc) { throw 'Inno Setup installed but ISCC.exe not found' }
}

if (-not $iscc) {
    Write-Error @'
Inno Setup is required to build the setup.exe and -NoBootstrap was given.
Install it with:  winget install JRSoftware.InnoSetup
'@
}

# ------------------------------------------------------------------ 4. build
$setupExe = Join-Path $out "HolyPHP-$Version-x64-setup.exe"
if (Test-Path $setupExe) { Remove-Item -Force $setupExe }

Write-Host "building $setupExe"
& $iscc "/DAppVersion=$Version" (Join-Path $PSScriptRoot 'holyphp.iss')
if ($LASTEXITCODE -ne 0 -or -not (Test-Path $setupExe)) {
    throw "ISCC failed (exit $LASTEXITCODE)"
}

$size = [math]::Round((Get-Item $setupExe).Length / 1KB)
Write-Host "done: $setupExe ($size KB)" -ForegroundColor Green
Write-Host ''
Write-Host 'Double-click it to install (standard wizard, no admin, no certificate).' -ForegroundColor White
Write-Host 'Pick the install folder, tick the PATH checkbox, done -- and it shows' -ForegroundColor White
Write-Host 'up in "Apps & features" with a proper uninstaller. Installs to' -ForegroundColor White
Write-Host '%LOCALAPPDATA%\Programs\HolyPHP by default; a legacy %LOCALAPPDATA%\HolyPHP' -ForegroundColor White
Write-Host 'install from the old script wizard is offered for cleanup on first run.' -ForegroundColor White
