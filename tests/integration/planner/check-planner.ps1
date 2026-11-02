<#
.SYNOPSIS
    Planner check (T095, Phase 6 exit): on every test-bed scenario each level's plan validates, moves at most 1.1 x what
    its lower bound requires (plan.md 9.3.1), and Fast stays within its budget and inside its hot spots (plan.md 9.2.1).

.DESCRIPTION
    Per scenario: creates a test volume, fills it with gen-scenario, mounts it read-only (planning never writes) and runs
    "icd-cli plan <volume> --strategy <preset> --json --moves" with the preset that matches the scenario (dev ->
    development, photos -> photos, video -> video, archive -> archive, the rest -> mixed). For each level the report
    must say the plan is valid and its ratioToLowerBound must be 1.1 or less (Maximum: see -MaximumRatio); for Fast the
    bytes moved must be at most 2% of the used space (the default budget) and every move's sources must lie inside the
    reported hot spots. The exhaustive minimality check on tiny volumes is a unit test (OptimalityTest).

    Needs administrator rights and a Release build. Exit code 0 when every check passes.

.EXAMPLE
    pwsh -File tests/integration/planner/check-planner.ps1
    pwsh -File tests/integration/planner/check-planner.ps1 -Scenarios all -SizeGB 4
#>
[CmdletBinding()]
param(
    # Scenarios to check: a list, or 'all'. The quick default takes a realistic mixed volume and the dev scenario.
    [string[]]$Scenarios = @('mixed', 'dev'),

    # Size of each test volume.
    [int]$SizeGB = 2,

    # Drive letter for the test volume.
    [string]$Letter = 'Q',

    # Ratio allowed at Maximum, which may park files to break cycles (plan.md 9.3.7 reports that cost as extra).
    [double]$MaximumRatio = 1.1,

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
$Root = "${Letter}:\"
$script:Failures = 0
if ($Scenarios -contains 'all') { $Scenarios = @('dev', 'archive', 'photos', 'video', 'mixed', 'almost-full', 'vss', 'special') }
$PresetOf = @{ dev = 'development'; photos = 'photos'; video = 'video'; archive = 'archive' }

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

# Whether every source run of every move lies inside one of the hot spots ([start, clusters] pairs).
function Test-InsideHotSpots($Level) {
    foreach ($move in $Level.moveList) {
        foreach ($run in $move.sources) {
            $start = [uint64]$run[0]; $end = $start + [uint64]$run[1]
            $inside = $false
            foreach ($spot in $Level.hotSpots) {
                if ($start -ge [uint64]$spot[0] -and $end -le [uint64]$spot[0] + [uint64]$spot[1]) { $inside = $true; break }
            }
            if (-not $inside) { return $false }
        }
    }
    return $true
}

# Plans one scenario volume and checks every level.
function Test-Scenario([string]$Scenario) {
    $preset = if ($PresetOf.ContainsKey($Scenario)) { $PresetOf[$Scenario] } else { 'mixed' }
    $report = & $Cli plan $Root --strategy $preset --json --moves 2>$null | ConvertFrom-Json
    if ($null -eq $report) { throw "icd-cli plan failed on $Scenario" }
    $free = (Get-Volume -DriveLetter $Letter)
    $used = [uint64]($free.Size - $free.SizeRemaining)
    Write-Host "[$Scenario] preset $preset, planned in $([math]::Round($report.planningSeconds, 2)) s"
    foreach ($level in $report.levels) {
        $limit = if ($level.level -eq 'maximum') { $MaximumRatio } else { 1.1 }
        Write-Host ("    {0,-9} {1} moves, {2:N0} MB, ratio {3:N3}, skipped {4:N0} MB, deferred {5:N0} MB" -f $level.level,
            $level.moves, ($level.bytesMoved / 1MB), $level.ratioToLowerBound, ($level.skippedBytes / 1MB), ($level.deferredBytes / 1MB))
        Check "$($level.level): plan validates" ([bool]$level.valid) (($level.issues | Select-Object -First 1) -join '')
        Check "$($level.level): ratio to the lower bound <= $limit" ($level.ratioToLowerBound -le $limit) "($($level.ratioToLowerBound))"
        if ($level.level -eq 'fast') {
            Check 'fast: within 2% of used space' ([uint64]$level.bytesMoved -le [uint64]($used * 0.02) + $report.bytesPerCluster) "($($level.bytesMoved) of $used)"
            Check 'fast: every move starts inside a hot spot' (Test-InsideHotSpots $level)
        }
    }
}

if (-not (Test-Path $Cli) -or -not (Test-Path $Gen)) { throw "Build $Config first (build.bat $Config)." }
foreach ($scenario in $Scenarios) {
    Invoke-MakeVol @('-Action', 'Create', '-Name', "plan-$scenario", '-SizeGB', $SizeGB, '-Letter', $Letter, '-Force')
    try {
        & $Gen $scenario $Letter | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "gen-scenario $scenario failed" }
        Invoke-MakeVol @('-Action', 'Dismount', '-Name', "plan-$scenario")
        Invoke-MakeVol @('-Action', 'Mount', '-Name', "plan-$scenario", '-ReadOnly')
        Test-Scenario $scenario
    }
    finally {
        Invoke-MakeVol @('-Action', 'Remove', '-Name', "plan-$scenario")
    }
}

if ($script:Failures -eq 0) { Write-Host 'All planner checks passed.'; exit 0 }
Write-Host "$script:Failures planner check(s) FAILED." -ForegroundColor Red
exit 1
