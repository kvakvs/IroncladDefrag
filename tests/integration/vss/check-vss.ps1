<#
.SYNOPSIS
    VSS check (T121, Phase 9 exit): on the vss scenario, an execution with unaligned moves blocked leaves the shadow
    storage as it was.

.DESCRIPTION
    Creates a test VHDX, fills it with the vss scenario (mixed content, a shadow copy, then changes after it), and runs
    "icd-cli execute <volume> --block-unaligned" at the chosen level. It reads the shadow storage used by the volume's
    copies before and after (Win32_ShadowStorage.UsedSpace, the figure "vssadmin list shadowstorage" prints, which is
    also shown) and checks that files moved and that it grew only by the file system's own records of the moves:
    each move rewrites the file's MFT record and a few bitmap blocks, which are copied on write like any other block,
    so at most -MetadataKBPerMove per move (16 KB for the record's block, 32 KB for bitmap blocks) plus -FixedMB for
    the run (the NTFS log and the volume's own records). Moving file data without the block grows it far more (see
    -Contrast). With -Contrast a second volume runs the same plan without the block and prints its growth (no
    check), to show what blocking saves. The VHDX files are removed at the end. Needs administrator rights and a
    Release (or -Config) build of icd-cli and gen-scenario.

.EXAMPLE
    pwsh -File tests/integration/vss/check-vss.ps1
.EXAMPLE
    pwsh -File tests/integration/vss/check-vss.ps1 -Level maximum -Contrast
#>
[CmdletBinding()]
param(
    # Drive letter for the test volume.
    [string]$Letter = 'Q',

    # Size of the test volume.
    [int]$SizeGB = 2,

    # Plan level to execute.
    [ValidateSet('fast', 'moderate', 'maximum')]
    [string]$Level = 'moderate',

    # Shadow storage growth accepted per move: the file system records each move rewrites (copied on write too).
    [double]$MetadataKBPerMove = 48,

    # Shadow storage growth accepted once per run, whatever moves.
    [double]$FixedMB = 1,

    # Also run without the block on a fresh volume and print the growth.
    [switch]$Contrast,

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
$CliLevel = if ($Level -eq 'maximum') { 'max' } else { $Level }
$script:Failures = 0

# Records one check result.
function Check([string]$What, [bool]$Ok, [string]$Detail = '') {
    if ($Ok) { Write-Host "    ok    $What" }
    else { Write-Host "    FAIL  $What $Detail" -ForegroundColor Red; $script:Failures++ }
}

# Bytes of shadow storage the volume's shadow copies use (0 when it has none).
function Get-ShadowUsed([string]$VolumeGuid) {
    $storage = Get-CimInstance Win32_ShadowStorage | Where-Object { $_.Volume.DeviceID -eq $VolumeGuid }
    if (-not $storage) { return [uint64]0 }
    return [uint64](($storage | Measure-Object -Property UsedSpace -Sum).Sum)
}

# A vss scenario volume at $Letter; returns its GUID path.
function New-VssVolume([string]$Name) {
    pwsh -NoProfile -File $MakeVol -Action Create -Name $Name -SizeGB $SizeGB -Letter $Letter -Force | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'make-testvol.ps1 Create failed' }
    & $Gen vss $Letter | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'gen-scenario vss failed' }
    Write-VolumeCache -DriveLetter $Letter
    Start-Sleep -Seconds 5 # The scenario's changes after the shadow copy reach the diff area first.
    return (Get-Volume -DriveLetter $Letter).Path
}

# Executes the level, with or without the block; returns the report and the shadow storage growth in bytes.
function Invoke-Run([string]$Guid, [bool]$Block) {
    $before = Get-ShadowUsed $Guid
    $flags = @('--strategy', 'mixed', '--level', $CliLevel, '--json')
    if ($Block) { $flags += '--block-unaligned' }
    $json = & $Cli execute $Letter @flags 2>$null | Out-String
    $code = $LASTEXITCODE
    Write-VolumeCache -DriveLetter $Letter
    Start-Sleep -Seconds 5 # Let the copy-on-write writes reach the diff area.
    $after = Get-ShadowUsed $Guid
    Write-Host ("    shadow storage {0:N1} MB -> {1:N1} MB" -f ($before / 1MB), ($after / 1MB))
    & vssadmin list shadowstorage "/for=${Letter}:" 2>&1 | Select-String 'Used' | ForEach-Object { Write-Host "    vssadmin: $($_.Line.Trim())" }
    return @{ Code = $code; Report = $(if ($json) { $json | ConvertFrom-Json } else { $null }); Growth = [int64]$after - [int64]$before }
}

if (-not (Test-Path $Cli) -or -not (Test-Path $Gen)) { throw "Build $Config first (build.bat $Config)." }
try {
    $guid = New-VssVolume 'vss-check'
    Check 'scenario: the volume has shadow storage' ((Get-ShadowUsed $guid) -gt 0)
    Write-Host "[vss] $Level with unaligned moves blocked"
    $run = Invoke-Run $guid $true
    Check 'execute: finished' ($run.Code -eq 0) "(exit $($run.Code))"
    $moved = if ($run.Report) { [uint64]$run.Report.execution.clustersMoved * [uint64]$run.Report.plan.bytesPerCluster } else { 0 }
    Check 'execute: files moved' ($moved -gt 0) "($moved bytes)"
    $moves = if ($run.Report) { [uint64]$run.Report.execution.moved } else { 0 }
    $planned = if ($run.Report) { [uint64]$run.Report.plan.moves } else { 0 }
    Write-Host "    moves planned $planned, executed $moves"
    $allowed = $FixedMB * 1MB + $moves * $MetadataKBPerMove * 1KB
    Check ("shadow storage grew only by the moves' file system records (<= {0:N1} MB for {1} moves)" -f ($allowed / 1MB), $moves) ($run.Growth -le $allowed) ("({0:N2} MB)" -f ($run.Growth / 1MB))
}
finally {
    pwsh -NoProfile -File $MakeVol -Action Remove -Name 'vss-check' | Out-Null
}
if ($Contrast) {
    try {
        $guid = New-VssVolume 'vss-contrast'
        Write-Host "[vss] $Level without the block (contrast, not checked)"
        $open = Invoke-Run $guid $false
        Write-Host ("    growth without the block: {0:N1} MB" -f ($open.Growth / 1MB))
    }
    finally {
        pwsh -NoProfile -File $MakeVol -Action Remove -Name 'vss-contrast' | Out-Null
    }
}

if ($script:Failures -eq 0) { Write-Host 'All VSS checks passed.'; exit 0 }
Write-Host "$script:Failures VSS check(s) FAILED." -ForegroundColor Red
exit 1
