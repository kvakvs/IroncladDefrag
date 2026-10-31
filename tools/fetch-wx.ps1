<#
.SYNOPSIS
    Downloads the pinned wxWidgets prebuilt MSVC x64 DLL build into 3rdparty/wxWidgets.

.DESCRIPTION
    Fetches the Dev (import libs + debug DLLs), ReleaseDLL (release DLLs) and headers archives of the
    pinned wxWidgets version, checks their SHA-256, and extracts them into 3rdparty/wxWidgets.
    Running it again is a no-op when the same version is already extracted. Use -Force to redo it.

.EXAMPLE
    pwsh -File tools/fetch-wx.ps1
#>
[CmdletBinding()]
param(
    # Re-download and re-extract even if the pinned version is already present.
    [switch]$Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'  # Invoke-WebRequest is very slow with the progress bar on.

# Pinned version and archive hashes (from the GitHub release asset digests).
# When changing the version, also update ICD_WX_VERSION in CMakeLists.txt.
$WxVersion = '3.3.4'
$BaseUrl = "https://github.com/wxWidgets/wxWidgets/releases/download/v$WxVersion"
$Archives = @(
    @{ Name = "wxMSW-${WxVersion}_vc14x_x64_Dev.7z"
       Sha256 = '11cf9e49ec867063ab9011239d76be2c4c736124e61171c5ce4c983aaa0b549e' }
    @{ Name = "wxMSW-${WxVersion}_vc14x_x64_ReleaseDLL.7z"
       Sha256 = 'd9bf482cded63f2f5b02603cfe5218cff413e1ff36876c1e9ea5a94286439936' }
    @{ Name = "wxWidgets-${WxVersion}-headers.7z"
       Sha256 = '8fd1cf657c3e8999a2e305604383bccb43bd8b766c86c379b411d2174b1402ef' }
)

$RepoRoot = Split-Path -Parent $PSScriptRoot
$ThirdParty = Join-Path $RepoRoot '3rdparty'
$Downloads = Join-Path $ThirdParty 'downloads'
$Target = Join-Path $ThirdParty 'wxWidgets'
$StampFile = Join-Path $Target 'icd-wx-stamp.txt'

# The stamp records exactly what was extracted, so a changed pin triggers a fresh extract.
$Stamp = (@("version=$WxVersion") + ($Archives | ForEach-Object { "$($_.Name)=$($_.Sha256)" })) -join "`n"

if (-not $Force -and (Test-Path $StampFile)) {
    $existing = (Get-Content -Raw $StampFile).Trim() -replace "`r", ''
    if ($existing -eq $Stamp) {
        Write-Host "wxWidgets $WxVersion is already in $Target"
        exit 0
    }
    Write-Host "wxWidgets in $Target does not match the pinned version; refreshing"
}

# Picks 7-Zip when installed, otherwise the bsdtar that ships with Windows 10 1803+ (reads .7z).
function Get-Extractor {
    $sevenZip = Get-Command 7z.exe -ErrorAction SilentlyContinue
    if ($sevenZip) { return @{ Kind = '7z'; Path = $sevenZip.Source } }
    $installed = Join-Path $env:ProgramFiles '7-Zip\7z.exe'
    if (Test-Path $installed) { return @{ Kind = '7z'; Path = $installed } }
    $tar = Join-Path $env:SystemRoot 'System32\tar.exe'
    if (Test-Path $tar) { return @{ Kind = 'tar'; Path = $tar } }
    throw 'No extractor found: install 7-Zip or use Windows 10 1803+ (System32\tar.exe).'
}

# SHA-256 of a file as lowercase hex. Uses .NET directly: Get-FileHash is missing when Windows PowerShell
# runs with PowerShell 7's PSModulePath (for example from a cmd started by pwsh).
function Get-Sha256([string]$file) {
    $sha = [System.Security.Cryptography.SHA256]::Create()
    $stream = [System.IO.File]::OpenRead($file)
    try { return ([System.BitConverter]::ToString($sha.ComputeHash($stream)) -replace '-', '').ToLowerInvariant() }
    finally { $stream.Dispose(); $sha.Dispose() }
}

# Downloads one archive unless a copy with the right hash is already cached.
function Get-Archive([hashtable]$archive) {
    $path = Join-Path $Downloads $archive.Name
    if (Test-Path $path) {
        if ((Get-Sha256 $path) -eq $archive.Sha256) { return $path }
        Write-Host "Cached $($archive.Name) has a wrong hash; downloading again"
        Remove-Item -Force $path
    }
    $url = "$BaseUrl/$($archive.Name)"
    Write-Host "Downloading $url"
    $partial = "$path.part"
    Invoke-WebRequest -Uri $url -OutFile $partial -UseBasicParsing
    $actual = Get-Sha256 $partial
    if ($actual -ne $archive.Sha256) {
        Remove-Item -Force $partial
        throw ("SHA-256 mismatch for $($archive.Name)`n  expected $($archive.Sha256)`n  actual   " +
               $actual + "`nThe download was deleted. Check the pin in tools/fetch-wx.ps1.")
    }
    Move-Item -Force $partial $path
    return $path
}

# Extracts one archive into a directory, failing on a non-zero exit code.
function Expand-Archive7z([hashtable]$extractor, [string]$archive, [string]$destination) {
    if ($extractor.Kind -eq '7z') {
        & $extractor.Path x -y -bso0 -bsp0 "-o$destination" $archive
    }
    else {
        & $extractor.Path -xf $archive -C $destination
    }
    if ($LASTEXITCODE -ne 0) { throw "Extracting $archive failed with exit code $LASTEXITCODE" }
}

New-Item -ItemType Directory -Force $Downloads | Out-Null
$paths = $Archives | ForEach-Object { Get-Archive $_ }

# Extract into a staging directory first so a failure never leaves a half-written tree behind.
$extractor = Get-Extractor
$staging = Join-Path $ThirdParty 'wxWidgets.staging'
if (Test-Path $staging) { Remove-Item -Recurse -Force $staging }
New-Item -ItemType Directory -Force $staging | Out-Null
foreach ($path in $paths) {
    Write-Host "Extracting $(Split-Path -Leaf $path) with $($extractor.Kind)"
    Expand-Archive7z $extractor $path $staging
}

foreach ($required in @('include\wx\wx.h', 'include\msvc\wx\setup.h', 'lib\vc14x_x64_dll')) {
    if (-not (Test-Path (Join-Path $staging $required))) {
        throw "Extracted tree is missing $required; the archive layout may have changed."
    }
}

Set-Content -NoNewline -Path (Join-Path $staging 'icd-wx-stamp.txt') -Value $Stamp
if (Test-Path $Target) { Remove-Item -Recurse -Force $Target }
Move-Item $staging $Target
Write-Host "wxWidgets $WxVersion is ready in $Target"
