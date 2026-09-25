# gen-icon.ps1 - generate the HolyPHP package icon (256x256 PNG)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$dir = Join-Path $PSScriptRoot 'assets'
New-Item -ItemType Directory -Force -Path $dir | Out-Null
$out = Join-Path $dir 'icon.png'

$bmp = New-Object System.Drawing.Bitmap 256, 256
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
$g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit

# dark background, matching the manifest's BackgroundColor #1e1e2e
$g.Clear([System.Drawing.Color]::FromArgb(30, 30, 46))

# rounded frame
$pen = New-Object System.Drawing.Pen ([System.Drawing.Color]::Magenta), 6
$g.DrawArc($pen, 10, 10, 236, 236, 0, 360)

# the "h" monogram
$font = New-Object System.Drawing.Font ('Consolas', 130, [System.Drawing.FontStyle]::Bold)
$brush = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 200, 200, 230))
$fmt = New-Object System.Drawing.StringFormat
$fmt.Alignment = [System.Drawing.StringAlignment]::Center
$fmt.LineAlignment = [System.Drawing.StringAlignment]::Center
$g.DrawString('h', $font, $brush, (New-Object System.Drawing.RectangleF 0, 8, 256, 256), $fmt)

$g.Dispose()
$bmp.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Host "icon written: $out"
