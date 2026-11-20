# rss.ps1 — run one positive test and verify its resident set stays flat.
#
#   powershell -ExecutionPolicy Bypass -File tests/rss.ps1 tests/positive/memory_rc.hphp "memory rc loops ok"
#
# The test must print exactly $Expected (when given). The process is sampled
# every 5 ms; the warm-up median (first 15 % of the samples) and the tail
# median (last 30 %) may differ by at most 16 MiB, and the tail range by at
# most 24 MiB. That catches a loop that allocates without releasing, while
# tolerating the allocator's normal high-water behaviour.
param(
    [Parameter(Mandatory = $true)][string]$Test,
    [string]$Expected = ""
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$compiler = Join-Path $repo 'hphp.exe'
if (-not (Test-Path $compiler)) { throw "no compiler at $compiler (run scripts/build.sh)" }
$source = (Resolve-Path (Join-Path $repo $Test)).Path
$work = Join-Path $repo 'scratch\hphpbuild'
$exeName = [System.IO.Path]::GetFileNameWithoutExtension($source) + '_rss.exe'
$exe = Join-Path $repo ('scratch\' + $exeName)
$env:HPHP_WORK_DIR = $work.Replace('\', '/')

& $compiler build $source -o $exe
if ($LASTEXITCODE -ne 0) { throw "HolyPHP build failed ($LASTEXITCODE)" }

$psi = [System.Diagnostics.ProcessStartInfo]::new($exe)
$psi.UseShellExecute = $false
$psi.RedirectStandardOutput = $true
$psi.RedirectStandardError = $true
$p = [System.Diagnostics.Process]::new()
$p.StartInfo = $psi
[void]$p.Start()
$samples = [System.Collections.Generic.List[double]]::new()
while (-not $p.HasExited) {
    try {
        $p.Refresh()
        if (-not $p.HasExited) { $samples.Add($p.WorkingSet64 / 1MB) }
    } catch { }
    # no sleep: the sampling loop must be faster than the workload
}
$p.WaitForExit()
$output = $p.StandardOutput.ReadToEnd()
$err = $p.StandardError.ReadToEnd()
if ($p.ExitCode -ne 0) { throw "test failed ($($p.ExitCode)): $($err.Trim()) $($output.Trim())" }
$lastLine = ($output -split "`r?`n" | Where-Object { $_ -ne '' } | Select-Object -Last 1)
if ($Expected -ne '' -and $lastLine -ne $Expected) {
    throw "unexpected output: '$lastLine' (want '$Expected')"
}
if ($samples.Count -lt 8) {
    throw "too few RSS samples ($($samples.Count)); the workload finished too quickly to measure"
}

$warmCount = [Math]::Max(3, [int]($samples.Count * 0.15))
$warm = @($samples | Select-Object -First $warmCount | Sort-Object)
$tailStart = [Math]::Max(0, [int]($samples.Count * 0.70))
$tail = @($samples | Select-Object -Skip $tailStart | Sort-Object)
$warmMedian = $warm[[int]($warm.Count / 2)]
$tailMedian = $tail[[int]($tail.Count / 2)]
$tailRange = $tail[-1] - $tail[0]
if ($tailMedian -gt $warmMedian + 16 -or $tailRange -gt 24) {
    throw ('RSS did not flatten: warm median {0:N1} MiB, tail median {1:N1} MiB, tail range {2:N1} MiB' -f $warmMedian, $tailMedian, $tailRange)
}
Write-Output ('rss ok: {0} ({1} samples; warm {2:N1} MiB, tail {3:N1} MiB)' -f $Test, $samples.Count, $warmMedian, $tailMedian)
