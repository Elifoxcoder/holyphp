# sign-msix.ps1 - sign the built HolyPHP msix with the HolyPHP dev cert.
#
#   powershell -ExecutionPolicy Bypass -File packaging/windows/sign-msix.ps1
#
# Expects packaging/windows/out/HolyPHP-dev.pfx (self-signed dev cert,
# password 'holyphp-dev'). Recreate it with:
#
#   $c = New-SelfSignedCertificate -Type Custom -Subject "CN=HolyPHP" `
#           -KeyUsage DigitalSignature -CertStoreLocation Cert:\CurrentUser\My `
#           -TextExtension @('2.5.29.37={text}1.3.6.1.5.5.7.3.3','2.5.29.19={text}')
#   $p = ConvertTo-SecureString -String 'holyphp-dev' -Force -AsPlainText
#   Export-PfxCertificate -Cert $c -FilePath packaging\windows\out\HolyPHP-dev.pfx -Password $p
#
# NOTE: run this via PowerShell, never by calling signtool.exe directly from
# Git Bash / MSYS -- MSYS rewrites the /fd /f /p switches into Windows paths
# and signtool then "can't find" its arguments.
#
# After signing, a client machine must trust the cert before
# Add-AppxPackage accepts the package (admin, one-time):
#
#   Import-Certificate -FilePath <exported .cer> -CertStoreLocation Cert:\LocalMachine\TrustedPeople

$ErrorActionPreference = 'Stop'
$signtool = 'C:\Program Files (x86)\Windows Kits\10\bin\10.0.18362.0\x64\signtool.exe'
if (-not (Test-Path $signtool)) {
    # fall back to newest SDK installed
    $signtool = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin" -Recurse -Filter signtool.exe |
                Where-Object { $_.FullName -match 'x64' } | Sort-Object FullName -Descending |
                Select-Object -First 1 -ExpandProperty FullName
}

$out = Join-Path $PSScriptRoot 'out'
$msix = Join-Path $out 'HolyPHP-1.0.0-x64.msix'
$pfx = Join-Path $out 'HolyPHP-dev.pfx'

if (-not (Test-Path $pfx)) { Write-Error "no $pfx -- create the dev cert first (see header comment)" }
if (-not (Test-Path $msix)) { Write-Error 'no msix - run make-msix.ps1 first' }

& $signtool sign /fd SHA256 /f $pfx /p holyphp-dev $msix
if ($LASTEXITCODE -ne 0) { Write-Error "signtool sign failed (exit $LASTEXITCODE)" }
Write-Host "signed: $msix"

# NOTE: `signtool verify /pa` will still report the self-signed root as
# untrusted until the cert is imported into LocalMachine\TrustedPeople on the
# target machine. That is the documented install-time step, not an error here.
Write-Host ''
Write-Host 'to install on a machine (one-time trust, admin):' -ForegroundColor Cyan
Write-Host '  1. export the public cert:'
Write-Host "       Export-Certificate -Cert (Get-ChildItem Cert:\CurrentUser\My | Where-Object Subject -eq 'CN=HolyPHP') -FilePath HolyPHP.cer"
Write-Host '  2. trust it (admin PowerShell):'
Write-Host "       Import-Certificate -FilePath HolyPHP.cer -CertStoreLocation Cert:\LocalMachine\TrustedPeople"
Write-Host '  3. install:'
Write-Host "       Add-AppxPackage -Path `"$msix`""