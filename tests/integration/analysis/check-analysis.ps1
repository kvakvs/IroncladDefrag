<#
.SYNOPSIS
    Phase 3 exit check (T050): analysis results against Windows' own tools on every test scenario.

.DESCRIPTION
    For each scenario: create a fresh VHDX test volume, fill it with gen-scenario, remount it READ-ONLY (the
    volume then refuses every write, which proves analysis writes nothing), run
    "icd-cli analyze <vol> --json --dump-extents" and check:
      - free space: summary.freeBytes minus NTFS reserved clusters equals "fsutil volume diskfree";
      - completeness: the runs of all files plus the free space cover every cluster exactly once;
      - extents: the runs of sampled files equal "fsutil file queryextents" (holes left out, runs that
        continue on disk joined).
    -RealDrive X additionally analyses a real data drive (read-only by nature of the analysis) with the free-space
    and extent checks.

    Needs administrator rights and a Release build (build.bat Release). Exit code 0 when every check passes.

.EXAMPLE
    pwsh -File tests/integration/analysis/check-analysis.ps1
    pwsh -File tests/integration/analysis/check-analysis.ps1 -Scenarios special,photos -RealDrive X
#>
[CmdletBinding()]
param(
    # Scenarios to check (default: all of plan.md section 12).
    [string[]]$Scenarios = @('dev', 'archive', 'photos', 'video', 'mixed', 'almost-full', 'vss', 'special'),

    # Size of each test volume.
    [int]$SizeGB = 4,

    # Drive letter for the test volume.
    [string]$Letter = 'Q',

    # Files compared with fsutil per volume.
    [int]$Samples = 40,

    # A real data drive to analyse as well (optional), e.g. X.
    [string]$RealDrive = '',

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
$Work = Join-Path ([IO.Path]::GetTempPath()) 'icd-analysis-check'
New-Item -ItemType Directory -Force $Work | Out-Null
$Scenarios = $Scenarios -split ','
$script:Failures = 0

# Records one check result.
function Check([string]$What, [bool]$Ok, [string]$Detail = '') {
    if ($Ok) { Write-Host "    ok    $What" }
    else { Write-Host "    FAIL  $What $Detail" -ForegroundColor Red; $script:Failures++ }
}

# "Total free bytes : 1,972,736,000 (1.8 GB)" from fsutil volume diskfree.
function Get-DiskFree([string]$Drive) {
    $line = fsutil volume diskfree "${Drive}:" | Select-String 'Total free bytes' | Select-Object -First 1
    return [uint64](($line.Line -split ':')[1].Trim() -split ' ')[0].Replace(',', '')
}

# Allocated runs of a file's main stream from fsutil, holes left out, runs continuing on disk joined.
function Get-FsutilRuns([string]$Path) {
    $runs = [Collections.Generic.List[object]]::new()
    $output = fsutil file queryextents "$Path" 2>&1
    foreach ($line in $output) {
        if ("$line" -match 'VCN:\s*0x([0-9a-f]+)\s+Clusters:\s*0x([0-9a-f]+)\s+LCN:\s*0x([0-9a-f]+)') {
            if ($Matches[3].Length -ge 16) { continue } # 0xffffffffffffffff: a hole.
            $lcn = [Convert]::ToUInt64($Matches[3], 16); $len = [Convert]::ToUInt64($Matches[2], 16)
            if ($runs.Count -gt 0 -and $runs[-1][0] + $runs[-1][1] -eq $lcn) { $runs[-1][1] += $len }
            else { $runs.Add([uint64[]]@($lcn, $len)) }
        }
    }
    return , $runs
}

# The main stream's runs of a file from the JSON dump (already joined within a stream).
function Get-SnapshotRuns($File) {
    $runs = [Collections.Generic.List[object]]::new()
    foreach ($r in $File.runs) {
        if ([uint64]$r[2] -ne 0) { continue }
        $lcn = [uint64]$r[0]; $len = [uint64]$r[1]
        if ($runs.Count -gt 0 -and $runs[-1][0] + $runs[-1][1] -eq $lcn) { $runs[-1][1] += $len }
        else { $runs.Add([uint64[]]@($lcn, $len)) }
    }
    return , $runs
}

# Runs the analysis and returns the parsed JSON.
function Invoke-Analysis([string]$Drive, [string]$Name) {
    $json = Join-Path $Work "$Name.json"
    & $Cli analyze "${Drive}:" --json --dump-extents 2>$null | Set-Content -LiteralPath $json -Encoding utf8
    if ($LASTEXITCODE -ne 0) { throw "icd-cli analyze ${Drive}: failed with exit code $LASTEXITCODE" }
    return Get-Content -LiteralPath $json -Raw | ConvertFrom-Json
}

# Free space and extent checks shared by test volumes and the real drive.
function Test-Analysis([string]$Drive, $Report, [bool]$Quiet) {
    $cluster = [uint64]$Report.volume.bytesPerCluster
    $reserved = [uint64]$Report.volume.ntfs.totalReserved
    $free = [uint64]$Report.summary.freeBytes
    $diskFree = Get-DiskFree $Drive
    if ($Quiet) {
        Check 'free space = fsutil volume diskfree + reserved' (($free - $reserved * $cluster) -eq $diskFree) "($free - $reserved clusters vs $diskFree)"
    }
    else {
        # A live drive keeps changing: within 0.1% is the same free space.
        $delta = [math]::Abs([double]($free - $reserved * $cluster) - [double]$diskFree)
        Check 'free space ~ fsutil volume diskfree (live drive)' ($delta -le 0.001 * $Report.volume.totalBytes) "(off by $delta bytes)"
    }
    Test-Extents $Drive $Report
}

# Runs of sampled files equal fsutil's.
function Test-Extents([string]$Drive, $Report) {
    $skip = @('metadata', 'directory', 'reparse-point')
    $candidates = @($Report.files | Where-Object {
            $flags = @($_.flags); -not ($flags | Where-Object { $skip -contains $_ }) -and
            $_.path.Length -lt 200 -and $_.path -notmatch '[^\x20-\x7e]'
        })
    $step = [math]::Max(1, [math]::Floor($candidates.Count / $Samples))
    $compared = 0; $mismatched = @()
    for ($i = 0; $i -lt $candidates.Count -and $compared -lt $Samples; $i += $step) {
        $file = $candidates[$i]
        $expected = Get-FsutilRuns ("${Drive}:" + $file.path)
        $actual = Get-SnapshotRuns $file
        $same = ($expected.Count -eq $actual.Count)
        for ($k = 0; $same -and $k -lt $expected.Count; $k++) {
            $same = ($expected[$k][0] -eq $actual[$k][0]) -and ($expected[$k][1] -eq $actual[$k][1])
        }
        if (-not $same) { $mismatched += $file.path }
        $compared++
    }
    Check "extents of $compared sampled files = fsutil file queryextents" ($mismatched.Count -eq 0 -and $compared -gt 0) ($mismatched -join ', ')
}

# Every cluster is in exactly one file run or free.
function Test-Completeness($Report) {
    $used = [uint64]0
    foreach ($f in $Report.files) { foreach ($r in $f.runs) { $used += [uint64]$r[1] } }
    $freeClusters = [uint64]$Report.summary.freeBytes / [uint64]$Report.volume.bytesPerCluster
    Check 'runs + free space = every cluster' (($used + $freeClusters) -eq [uint64]$Report.volume.totalClusters) "($used + $freeClusters vs $($Report.volume.totalClusters))"
    Check 'nothing changed during the scan' ([uint64]$Report.scan.changedDuringScan -eq 0)
}

# Runs make-testvol.ps1 and stops the check when it fails.
function Invoke-MakeVol([string[]]$Arguments) {
    pwsh -NoProfile -File $MakeVol @Arguments | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "make-testvol.ps1 $($Arguments -join ' ') failed" }
}

# Fresh volume, scenario, read-only remount, analysis, checks.
function Test-Scenario([string]$Name) {
    Write-Host "[$Name]"
    Invoke-MakeVol @('-Action', 'Create', '-Name', 'analysis', '-SizeGB', $SizeGB, '-Letter', $Letter, '-Force')
    & $Gen $Name $Letter | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "gen-scenario $Name failed" }
    Invoke-MakeVol @('-Action', 'Dismount', '-Name', 'analysis')
    Invoke-MakeVol @('-Action', 'Mount', '-Name', 'analysis', '-Letter', $Letter, '-ReadOnly')
    $probe = Join-Path "${Letter}:\" 'icd-write-probe.tmp'
    $writable = $true
    try { Set-Content -LiteralPath $probe -Value 'x' -ErrorAction Stop } catch { $writable = $false }
    Check 'volume mounted read-only (any write would fail)' (-not $writable)
    $report = Invoke-Analysis $Letter $Name
    Write-Host ("    {0} files, {1:N1}% fragmented, {2} free runs, {3:N2} s" -f $report.summary.files,
        $report.summary.fragmentationPercent, $report.summary.freeRuns, $report.scan.seconds)
    Test-Analysis $Letter $report $true
    Test-Completeness $report
    Invoke-MakeVol @('-Action', 'Dismount', '-Name', 'analysis')
}

if (-not (Test-Path $Cli) -or -not (Test-Path $Gen)) { throw "Build $Config first (build.bat $Config)." }
foreach ($scenario in $Scenarios) { Test-Scenario $scenario }
if ($RealDrive) {
    Write-Host "[real drive ${RealDrive}:]"
    $report = Invoke-Analysis $RealDrive 'real'
    Write-Host ("    {0} files, {1:N1}% fragmented, {2:N2} s" -f $report.summary.files, $report.summary.fragmentationPercent,
        $report.scan.seconds)
    Test-Analysis $RealDrive $report $false
}
Invoke-MakeVol @('-Action', 'Remove', '-Name', 'analysis')
if ($script:Failures -eq 0) { Write-Host 'All analysis checks passed.' -ForegroundColor Green; exit 0 }
Write-Host "$script:Failures check(s) failed." -ForegroundColor Red
exit 1
