$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$compiler = Join-Path $repo 'hphp.exe'
$source = Join-Path $PSScriptRoot 'memory_rc.hphp'
$work = Join-Path $repo 'scratch\hphpbuild'
$exe = Join-Path $repo 'scratch\memory_rc_rss.exe'
$env:HPHP_WORK_DIR = $work.Replace('\', '/')

& $compiler build $source -o $exe
if ($LASTEXITCODE -ne 0) { throw "HolyPHP build failed ($LASTEXITCODE)" }

$psi = [System.Diagnostics.ProcessStartInfo]::new($exe)
$psi.UseShellExecute = $false
$psi.RedirectStandardOutput = $true
$p = [System.Diagnostics.Process]::new()
$p.StartInfo = $psi
[void]$p.Start()
$samples = [System.Collections.Generic.List[double]]::new()
while (-not $p.HasExited) {
    try {
        $p.Refresh()
        if (-not $p.HasExited) { $samples.Add($p.WorkingSet64 / 1MB) }
    } catch { }
    Start-Sleep -Milliseconds 20
}
$p.WaitForExit()
$output = $p.StandardOutput.ReadToEnd()
if ($p.ExitCode -ne 0) { throw "Memory loop failed ($($p.ExitCode)): $output" }
if ($output.Trim() -ne 'memory rc loops ok') { throw "Unexpected output: $output" }
if ($samples.Count -lt 8) { throw "Too few RSS samples ($($samples.Count)); workload completed too quickly to measure" }

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
Write-Output ('memory rc RSS ok: {0} samples; warm {1:N1} MiB, tail {2:N1} MiB' -f $samples.Count, $warmMedian, $tailMedian)


