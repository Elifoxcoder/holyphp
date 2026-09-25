# make-msix.ps1 -- build the HolyPHP MSIX from a freshly built hphp.exe.
#
#   powershell -File packaging/windows/make-msix.ps1
#
# Prerequisites (any one of):
#   * Windows SDK (makeappx.exe + signtool.exe) -- recommended
#   * nothing at all, for the wizard-only route: the script stages the layout
#     first, so Install-HolyPHP.ps1 -PackageRoot ...\stage works without the SDK
#
# The script produces:
#   packaging/windows/stage/                           (package layout)
#   packaging/windows/out/HolyPHP-<version>-x64.msix   (unsigned)
#
# Layout inside the package:
#   AppxManifest.xml
#   hphp/hphp.exe            <- the whole toolchain, runtime embedded
#   Install-HolyPHP.ps1      <- the wizard (gcc check + pkg hints; PATH is
#                               handled by the manifest's appExecutionAlias)
#   assets/icon.png
#
# Libraries (lib/ui.hphp, lib/websocket.hphp) are deliberately NOT in the
# package -- users install them with `hphp pkg install ui` etc.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)   # repo root
$version = '1.0.0'

# 1. stage the package layout
$stage = Join-Path $PSScriptRoot 'stage'
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force -Path "$stage\hphp", "$stage\assets" | Out-Null

if (-not (Test-Path "$root\hphp.exe")) {
    Write-Error 'no hphp.exe at repo root -- run: bash scripts/build.sh'
}
Copy-Item "$root\hphp.exe" "$stage\hphp\hphp.exe"
Copy-Item "$PSScriptRoot\AppxManifest.xml" $stage
Copy-Item "$PSScriptRoot\Install-HolyPHP.ps1" $stage
if (Test-Path "$PSScriptRoot\assets\icon.png") {
    Copy-Item "$PSScriptRoot\assets\icon.png" "$stage\assets\"
} else {
    Write-Warning "assets\icon.png missing -- the manifest references it; add a 256x256 PNG."
}
Write-Host "staged: $stage"

# 2. locate makeappx (Windows SDK)
$makeappx = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin" -Recurse -Filter makeappx.exe -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -match 'x64' } | Sort-Object FullName -Descending | Select-Object -First 1
if (-not $makeappx) {
    Write-Warning @"
makeappx.exe not found -- no .msix built. Two options:
  * install the Windows SDK and re-run:  winget install Microsoft.WindowsSDK
  * use the wizard-only route on the staged layout above:
      powershell -ExecutionPolicy Bypass -File packaging/windows/Install-HolyPHP.ps1
"@
    exit 1
}

# 3. pack
$outDir = Join-Path $PSScriptRoot 'out'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$msix = Join-Path $outDir "HolyPHP-$version-x64.msix"
if (Test-Path $msix) { Remove-Item $msix }
& $makeappx.FullName pack /o /d $stage /p $msix
if ($LASTEXITCODE -ne 0) { Write-Error 'makeappx pack failed' }

Write-Host ''
Write-Host "packed: $msix" -ForegroundColor Green

# 4. (optional) sign -- dev-test cert if present, otherwise explain the options
$signtool = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin" -Recurse -Filter signtool.exe -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -match 'x64' } | Sort-Object FullName -Descending | Select-Object -First 1
if ($signtool) {
    Write-Host ''
    Write-Host 'next steps to install on a machine:' -ForegroundColor Cyan
    Write-Host "  1. create + trust a test cert (once):"
    Write-Host '       New-SelfSignedCertificate -Type CodeSigningCert -Subject "CN=HolyPHP" -CertStoreLocation Cert:\CurrentUser\My'
    Write-Host "  2. sign:  $($signtool.FullName) sign /fd SHA256 /a `"$msix`""
    Write-Host '  3. trust: Import-Certificate ... Cert:\LocalMachine\TrustedPeople   (or install via double-click)'
    Write-Host "  4. install:  Add-AppxPackage -Path `"$msix`""
    Write-Host ''
    Write-Host 'or skip signing entirely for local testing:'
    Write-Host "  powershell -File packaging/windows/Install-HolyPHP.ps1 -PackageRoot packaging/windows/stage"
} else {
    Write-Warning 'signtool not found -- the .msix is unsigned; Windows will refuse Add-AppxPackage until you sign it.'
}
