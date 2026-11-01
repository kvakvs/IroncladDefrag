<#
.SYNOPSIS
    Scanner cross-check (T063): the MFT scanner gives the same extent index as the API scanner on every test scenario,
    for 1, 2 and all workers.

.DESCRIPTION
    For each scenario: a fresh VHDX test volume, filled by gen-scenario, remounted READ-ONLY (nothing can change between
    the scans), then "icd-cli compare-scanners <vol> --threads 1,2,0": the API scan is the reference, each MFT scan is
    compared with it field by field and with the first MFT scan exactly. Any difference fails the check.

    -RealDrives E,X also compares real data drives (live: a scan is repeated once when files changed in between).
    -ScaleDrive S compares a mounted scale volume (make-scalevol) and requires the MFT scan to be at least
    -MinSpeedup times faster than the API scan (plan.md 13, Phase 4 exit: 10x on a 1 M-file volume).

    Needs administrator rights and a Release build (build.bat Release). Exit code 0 when every check passes.

.EXAMPLE
    pwsh -File tests/integration/scanner-equivalence/check-scanners.ps1 -RealDrives E,X -ScaleDrive S
#>
[CmdletBinding()]
param(
    # Scenarios to check (default: all of plan.md section 12).
    [string[]]$Scenarios = @('dev', 'archive', 'photos', 'video', 'mixed', 'almost-full', 'vss', 'special'),

    # Size of each test volume.
    [int]$SizeGB = 4,

    # Drive letter for the test volume.
    [string]$Letter = 'Q',

    # Real data drives to compare as well (only quiet storage drives), e.g. E,X.
    [string[]]$RealDrives = @(),

    # A mounted scale volume for the speed check, e.g. S.
    [string]$ScaleDrive = '',

    # Required MFT speed-up over the API scan on the scale volume.
    [double]$MinSpeedup = 10,

    # Build configuration whose icd-cli and gen-scenario are used.
    [string]$Config = 'Release'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$Repo = Resolve-Path (Join-Path $PSScriptRoot '..\..\..')
$Bin = Join-Path $Repo "build\bin\$Config"
$Cli = Join-Path $Bin 'icd-cli.exe'
$Gen = Join-Path $Bin 'gen-scenario.exe'
$MakeVol = Join-Path $Repo 'tools\testvol\make-testvol.ps1'
$Work = Join-Path ([IO.Path]::GetTempPath()) 'icd-scanner-check'
New-Item -ItemType Directory -Force $Work | Out-Null
$Scenarios = @($Scenarios -split ',' | Where-Object { $_ }) # -Scenarios '' checks none.
$RealDrives = @($RealDrives -split ',' | Where-Object { $_ })
$script:Failures = 0

# Records one check result.
function Check([string]$What, [bool]$Ok, [string]$Detail = '') {
    if ($Ok) { Write-Host "    ok    $What" }
    else { Write-Host "    FAIL  $What $Detail" -ForegroundColor Red; $script:Failures++ }
}

# Runs make-testvol.ps1 and stops the check when it fails.
function Invoke-MakeVol([string[]]$Arguments) {
    pwsh -NoProfile -File $MakeVol @Arguments | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "make-testvol.ps1 $($Arguments -join ' ') failed" }
}

# Runs compare-scanners; returns the exit code and keeps the report in the work directory.
function Invoke-Compare([string]$Drive, [string]$Name, [string]$Threads) {
    $out = Join-Path $Work "$Name.txt"
    & $Cli compare-scanners "${Drive}:" --threads $Threads 2>$null | Set-Content -LiteralPath $out -Encoding utf8
    $code = $LASTEXITCODE
    Get-Content -LiteralPath $out | Where-Object { $_ -match '^(API|MFT) scan|against' } | ForEach-Object { Write-Host "      $_" }
    return $code
}

# Fresh volume, scenario, read-only remount, comparison.
function Test-Scenario([string]$Name) {
    Write-Host "[$Name]"
    Invoke-MakeVol @('-Action', 'Create', '-Name', 'scanners', '-SizeGB', $SizeGB, '-Letter', $Letter, '-Force')
    & $Gen $Name $Letter | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "gen-scenario $Name failed" }
    Invoke-MakeVol @('-Action', 'Dismount', '-Name', 'scanners')
    Invoke-MakeVol @('-Action', 'Mount', '-Name', 'scanners', '-Letter', $Letter, '-ReadOnly')
    $code = Invoke-Compare $Letter $Name '1,2,0'
    Check 'MFT scan = API scan for 1, 2 and all workers' ($code -eq 0) "(see $Work\$Name.txt)"
    Invoke-MakeVol @('-Action', 'Dismount', '-Name', 'scanners')
}

# A live drive: one retry when something wrote between the scans.
function Test-RealDrive([string]$Drive) {
    Write-Host "[real drive ${Drive}:]"
    $code = Invoke-Compare $Drive "real-$Drive" '1,2,0'
    if ($code -ne 0) { Write-Host '      differences; scanning once more (the drive may have changed)'; $code = Invoke-Compare $Drive "real-$Drive" '1,2,0' }
    Check "MFT scan = API scan on ${Drive}:" ($code -eq 0) "(see $Work\real-$Drive.txt)"
}

# The scale volume: equality and the speed-up of the parallel MFT scan.
function Test-Scale([string]$Drive) {
    Write-Host "[scale volume ${Drive}:]"
    $code = Invoke-Compare $Drive 'scale' '0'
    Check "MFT scan = API scan on ${Drive}:" ($code -eq 0) "(see $Work\scale.txt)"
    $line = Get-Content -LiteralPath (Join-Path $Work 'scale.txt') | Select-String '\(([0-9.]+)x the API scan\)' | Select-Object -First 1
    $speedup = if ($line) { [double]$line.Matches[0].Groups[1].Value } else { 0 }
    Check "MFT scan at least ${MinSpeedup}x faster than the API scan" ($speedup -ge $MinSpeedup) "(${speedup}x)"
}

if (-not (Test-Path $Cli) -or -not (Test-Path $Gen)) { throw "Build $Config first (build.bat $Config)." }
foreach ($scenario in $Scenarios) { Test-Scenario $scenario }
if ($Scenarios.Count -gt 0) { Invoke-MakeVol @('-Action', 'Remove', '-Name', 'scanners') }
foreach ($drive in $RealDrives) { Test-RealDrive $drive }
if ($ScaleDrive) { Test-Scale $ScaleDrive }
if ($script:Failures -eq 0) { Write-Host 'All scanner checks passed.' -ForegroundColor Green; exit 0 }
Write-Host "$script:Failures check(s) failed." -ForegroundColor Red
exit 1
