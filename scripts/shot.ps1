# shot.ps1 — capture a single top-level window to PNG (native resolution).
#   powershell -NoProfile -File scripts/shot.ps1 -Proc shot -Out C:\hphpbuild\shot.png
param(
    [Parameter(Mandatory=$true)][string]$Proc,
    [Parameter(Mandatory=$true)][string]$Out
)

Add-Type -AssemblyName System.Drawing

# Without this the host is DPI-unaware, GetWindowRect hands back virtualised
# (logical) coordinates and PrintWindow renders a 1350x1200 window into a
# 900x800 bitmap -- so the capture comes out scaled and every measurement
# taken from it is wrong.
try {
    Add-Type @"
using System;
using System.Runtime.InteropServices;
public class DpiAwake {
    [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr v);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
}
"@
    [void][DpiAwake]::SetProcessDpiAwarenessContext([IntPtr](-4))
    if (-not $?) { [void][DpiAwake]::SetProcessDPIAware() }
} catch { }

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Win {
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint f);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
}
"@ -ErrorAction SilentlyContinue

$p = Get-Process -Name $Proc -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $p) { Write-Host "no process $Proc"; exit 1 }
$h = $p.MainWindowHandle
if ($h -eq 0) { Write-Host "no main window"; exit 1 }

$r = New-Object Win+RECT
[void][Win]::GetWindowRect($h, [ref]$r)
$w = $r.R - $r.L
$hgt = $r.B - $r.T
if ($w -le 0 -or $hgt -le 0) { Write-Host "bad rect"; exit 1 }

$bmp = New-Object System.Drawing.Bitmap $w, $hgt
$g = [System.Drawing.Graphics]::FromImage($bmp)
$dc = $g.GetHdc()
[void][Win]::PrintWindow($h, $dc, 2)
$g.ReleaseHdc($dc)
$g.Dispose()
$bmp.Save($Out)
Write-Host "saved $Out ${w}x${hgt}"
