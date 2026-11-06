# Records a built installer in the site's release list (website/data/releases.json), for the /release skill.
#   pwsh deploy/record-release.ps1 -Version 1.0.1 [-Installer dist\IroncladDefrag-1.0.1-abc1234-setup.exe] [-Date 2026-11-06]
# The version's entry (version, date, changes) must already be in releases.json; this fills in the installer's file
# name, commit, size and SHA-256, and writes the .sha256 file next to the installer (sha256sum format) when missing.
# Without -Installer it takes the installer built from HEAD. A "-dirty" build (uncommitted changes) is refused.
param(
    [Parameter(Mandatory)][string]$Version,
    [string]$Installer,
    [string]$Date
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$releasesPath = Join-Path $repo 'website/data/releases.json'

if (-not $Installer) {
    $head = (git -C $repo rev-parse --short=7 HEAD).Trim()
    $Installer = Join-Path $repo "dist/IroncladDefrag-$Version-$head-setup.exe"
}
if (-not (Test-Path $Installer)) { throw "No installer at ${Installer}: run make-installer.bat first." }
$file = Split-Path $Installer -Leaf
if ($file -notmatch '^IroncladDefrag-(?<version>\d+\.\d+\.\d+)-(?<commit>[0-9a-f]{7})-setup\.exe$') {
    throw "$file is not a clean release build (IroncladDefrag-<version>-<commit>-setup.exe, no -dirty)."
}
if ($Matches.version -ne $Version) { throw "$file is version $($Matches.version), not $Version." }
$commit = $Matches.commit

# The checksum file the site links to; an existing one must agree with the installer.
$sha256 = (Get-FileHash $Installer -Algorithm SHA256).Hash.ToLowerInvariant()
$sumPath = "$Installer.sha256"
if (Test-Path $sumPath) {
    $saved = ((Get-Content $sumPath -Raw) -split '\s+')[0]
    if ($saved -ne $sha256) { throw "$sumPath says $saved, but the installer hashes to $sha256." }
} else {
    [IO.File]::WriteAllText($sumPath, "$sha256  $file`n")
}

$releases = Get-Content $releasesPath -Raw | ConvertFrom-Json -NoEnumerate
$entry = $releases | Where-Object { $_.version -eq $Version }
if (-not $entry) { throw "releases.json has no entry for ${Version}: add its version, date and changes first." }
$facts = [ordered]@{ commit = $commit; file = $file; bytes = (Get-Item $Installer).Length; sha256 = $sha256 }
if ($Date) { $facts.date = $Date }
foreach ($key in $facts.Keys) {
    $entry | Add-Member -NotePropertyName $key -NotePropertyValue $facts[$key] -Force
}
$json = ConvertTo-Json -InputObject $releases -Depth 5
[IO.File]::WriteAllText($releasesPath, ($json -replace "`r`n", "`n") + "`n")
Write-Output "Recorded ${Version}: $file, $($facts.bytes) bytes, SHA-256 $sha256"
