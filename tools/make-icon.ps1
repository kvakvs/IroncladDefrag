<#
.SYNOPSIS
    Builds res/IroncladDefrag.ico (T141) from the square PNG master res/icon/hdd-color.png.

.DESCRIPTION
    Resizes the master once per size (16, 20, 24, 32, 48, 64 and 256 px) with ImageMagick's Lanczos filter, so each
    size is drawn from the full-size image rather than from the next larger one, and packs them into one .ico (the
    256 px image PNG-compressed, the others as 32-bit images with alpha). The .ico is committed; run this again only
    when the master changes. Needs ImageMagick 7 ("magick") on PATH.

.EXAMPLE
    pwsh -File tools/make-icon.ps1
#>
[CmdletBinding()]
param(
    # The square PNG master, at least 256 px.
    [string]$Master = (Join-Path $PSScriptRoot '..\res\icon\hdd-color.png'),

    # The icon to write.
    [string]$Output = (Join-Path $PSScriptRoot '..\res\IroncladDefrag.ico')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$Sizes = 16, 20, 24, 32, 48, 64, 256
if (-not (Get-Command magick -ErrorAction SilentlyContinue)) { throw 'ImageMagick 7 (magick) is not on PATH.' }

# Writes an .ico with the frames of an existing one plus a 256 px PNG frame at the end (ICONDIR, ICONDIRENTRY, data).
function Write-PngIcon([string]$SmallIcon, [string]$LargePng, [string]$Path) {
    $ico = [IO.File]::ReadAllBytes($SmallIcon)
    $png = [IO.File]::ReadAllBytes($LargePng)
    $count = [BitConverter]::ToUInt16($ico, 4)
    $out = [IO.MemoryStream]::new()
    $w = [IO.BinaryWriter]::new($out)
    $w.Write([uint16]0); $w.Write([uint16]1); $w.Write([uint16]($count + 1))
    $offset = 6 + 16 * ($count + 1)
    $datas = @()
    for ($i = 0; $i -lt $count; $i++) {
        $e = 6 + 16 * $i
        $size = [BitConverter]::ToUInt32($ico, $e + 8)
        $start = [BitConverter]::ToUInt32($ico, $e + 12)
        $w.Write($ico, $e, 8) # Width, height, colors, reserved, planes, bit count.
        $w.Write([uint32]$size); $w.Write([uint32]$offset)
        $datas += , ($ico[$start..($start + $size - 1)])
        $offset += $size
    }
    $w.Write([byte]0); $w.Write([byte]0); $w.Write([byte]0); $w.Write([byte]0) # 256 x 256, no palette.
    $w.Write([uint16]1); $w.Write([uint16]32); $w.Write([uint32]$png.Length); $w.Write([uint32]$offset)
    foreach ($d in $datas) { $w.Write([byte[]]$d) }
    $w.Write($png)
    $w.Flush()
    [IO.File]::WriteAllBytes($Path, $out.ToArray())
}

$Work = Join-Path ([IO.Path]::GetTempPath()) "icd-icon-$PID"
New-Item -ItemType Directory -Force $Work | Out-Null
try {
    $frames = foreach ($size in $Sizes) {
        $frame = Join-Path $Work "$size.png"
        & magick $Master -background none -filter Lanczos -resize "${size}x${size}" -gravity center `
            -extent "${size}x${size}" "PNG32:$frame"
        if ($LASTEXITCODE -ne 0) { throw "magick failed for $size px" }
        $frame
    }
    # ImageMagick stores every frame as a bitmap; the small ones stay so, the 256 px one goes in as its PNG.
    $small = Join-Path $Work 'small.ico'
    & magick @($frames | Select-Object -SkipLast 1) $small
    if ($LASTEXITCODE -ne 0) { throw 'magick failed to write the icon' }
    Write-PngIcon -SmallIcon $small -LargePng $frames[-1] -Path $Output
    & magick identify $Output | ForEach-Object { Write-Host $_ }
}
finally {
    Remove-Item -Recurse -Force $Work -ErrorAction SilentlyContinue
}
