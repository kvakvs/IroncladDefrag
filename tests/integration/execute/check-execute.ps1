<#
.SYNOPSIS
    Execution check (T104, Phase 7 exit): on every test-bed scenario plans are revalidated, executed and verified
    (plan.md 12): file hashes unchanged, chkdsk clean, every moved file where the plan predicted after a full rescan,
    and the snapshot saved after execution equal to a full scan once caught up. Also: changes between planning and
    execution are detected, a stale bitmap is handled, and (on -ResumeScenarios) pause, cancel and a killed process are
    resumed safely. Every execution is capped at -MaxMoves moves, so no run moves gigabytes: a prefix of a plan is a
    valid plan, and the rest stays in the log. The whole sweep (-Scenarios all) takes about 15 minutes.

.DESCRIPTION
    Per scenario, on a fresh 1 GB test volume with a USN journal (revalidation then catches up instead of rescanning):
      1. "icd-cli plan --save" at Moderate; one file among the first -MaxMoves moves is deleted and another grown by
         2 MB; the volume is hashed (once per scenario).
      2. "icd-cli execute --plan" must drop both (file-gone, file-changed) and run the rest of its prefix.
      3. Main run at Maximum with the scenario's preset; another file takes the first destination cluster of move 3
         just before it starts (--take-destination), which must go elsewhere (replaced) and verify.
      4. -ResumeScenarios only: a run paused after 3 moves and cancelled after 12, then --resume; a run slowed by a
         throughput limit and killed with Stop-Process after 15 moves, then --resume.
      5. -ChurnGap on dev: the main run is not capped, then 200 new 256 KB build files are written and the share of
         their clusters NTFS put into the churn gap is printed (plan.md 8.3). Slow: run it only to measure that.
    After every execution: the report's checks (exit code, verified moves, rescan) and "icd-cli check-catchup" on the
    saved snapshot. At the end of a scenario: hashes against the manifest and chkdsk /scan. Every step prints its time;
    the app's log (%LOCALAPPDATA%\IroncladDefrag\logs\icd-cli.log) has the executor's progress and every move that did
    not complete.

    Needs administrator rights and a Release build. Exit code 0 when every check passes.

.EXAMPLE
    pwsh -File tests/integration/execute/check-execute.ps1
    pwsh -File tests/integration/execute/check-execute.ps1 -Scenarios all
    pwsh -File tests/integration/execute/check-execute.ps1 -Scenarios dev -ChurnGap
#>
[CmdletBinding()]
param(
    # Scenarios to check: a list, or 'all'. The quick default takes every special case and a realistic mixed volume.
    [string[]]$Scenarios = @('special', 'mixed'),

    # Size of each test volume.
    [int]$SizeGB = 1,

    # Most moves one execution runs.
    [int]$MaxMoves = 400,

    # Scenarios that also get the pause, cancel, kill and resume runs (they test the log, not the data).
    [string[]]$ResumeScenarios = @('mixed'),

    # Measure where NTFS puts new build output after a full development layout (dev only; moves the whole plan).
    [switch]$ChurnGap,

    # Drive letter for the test volume.
    [string]$Letter = 'Q',

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
$VerifyVol = Join-Path $Repo 'tools\testvol\verify-testvol.ps1'
$Root = "${Letter}:\"
$Work = Join-Path ([IO.Path]::GetTempPath()) 'icd-check-execute'
$LogDir = Join-Path $env:LOCALAPPDATA 'IroncladDefrag\logs\execute'
$script:Failures = 0
if ($Scenarios -contains 'all') { $Scenarios = @('dev', 'archive', 'photos', 'video', 'mixed', 'almost-full', 'vss', 'special') }
$PresetOf = @{ dev = 'development'; photos = 'photos'; video = 'video'; archive = 'archive' }
$Cap = @('--max-moves', "$MaxMoves")

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

# Runs icd-cli with JSON output; returns the report, the exit code and the wall time.
function Invoke-Cli([string[]]$Arguments) {
    $clock = [Diagnostics.Stopwatch]::StartNew()
    $text = & $Cli @Arguments 2>$null
    $code = $LASTEXITCODE
    $report = if ($text) { ($text -join "`n") | ConvertFrom-Json } else { $null }
    return [pscustomobject]@{ Report = $report; Code = $code; Seconds = $clock.Elapsed.TotalSeconds }
}

# Runs a step and prints how long it took.
function Measure-Step([string]$What, [scriptblock]$Body) {
    $clock = [Diagnostics.Stopwatch]::StartNew()
    & $Body
    Write-Host ("    time  {0}: {1:N1} s" -f $What, $clock.Elapsed.TotalSeconds) -ForegroundColor DarkGray
}

# The common checks of one execution report.
function Test-Execution($Result, [string]$What, [int]$ExpectedCode = 0) {
    $r = $Result.Report
    if ($null -eq $r) { Check "${What}: icd-cli execute reported" $false "(exit $($Result.Code))"; return }
    $e = $r.execution
    Write-Host ("    {0}: {1} of {2} moves tried: {3} moved, {4} done before, {5} partial, {6} skipped, {7} replaced, {8} dropped; moves {9:N1} s, command {10:N1} s" -f
        $What, $r.plan.movesTried, $r.plan.moves, $e.moved, $e.alreadyDone, $e.partial, $e.skipped, $e.replaced,
        $r.revalidation.dropped, $e.seconds, $Result.Seconds)
    foreach ($p in @($e.problems | Select-Object -First 5)) { Write-Host "          $($p.outcome) $($p.reason) $($p.path)" }
    Check "${What}: exit code $ExpectedCode" ($Result.Code -eq $ExpectedCode) "(got $($Result.Code); $($r.refused))"
    Check "${What}: every completed move verified" ($e.unverified -eq 0) "($($e.unverified) unverified)"
    if ($r.PSObject.Properties.Name -contains 'rescan') {
        Check "${What}: moved files where predicted after a rescan" ($r.rescan.mismatches -eq 0) `
            "($($r.rescan.mismatches) of $($r.rescan.filesChecked): $(@($r.rescan.samples | Select-Object -First 2) -join '; '))"
    }
    Test-SavedSnapshot $r $What
}

# The snapshot saved after execution, caught up from the journal, must equal a full scan (plan.md 6.2 principle 6).
# When the runs read back cannot be patched in (Windows changed the volume in ways the moves do not explain, such as
# deleting a shadow copy), no snapshot is saved and the next analysis scans in full: the safe outcome.
function Test-SavedSnapshot($Report, [string]$What) {
    if (-not $Report.snapshot.saved) {
        Write-Host "    note  ${What}: no snapshot saved: $($Report.snapshot.note)" -ForegroundColor Yellow
        return
    }
    Write-VolumeCache -DriveLetter $Letter
    $catchUp = & $Cli check-catchup $Root 2>&1 | Where-Object { "$_" -notmatch '^\s*(Reading|Building|Summarizing)' }
    Check "${What}: saved snapshot, caught up, equals a full scan" ($LASTEXITCODE -eq 0) `
        "($(@($catchUp | ForEach-Object { "$_".Trim() } | Where-Object { $_ } | Select-Object -Last 5) -join '; '))"
}

# Planned regular files among the first moves of a saved plan's report, for scripted changes.
function Get-PlannedFiles($PlanReport) {
    $level = $PlanReport.levels | Where-Object { $_.level -eq 'moderate' }
    return @($level.moveList | Select-Object -First $MaxMoves | ForEach-Object { $_.file } | Select-Object -Unique |
        Where-Object {
            $full = Join-Path $Root $_.TrimStart('\')
            (Test-Path -LiteralPath $full -PathType Leaf) -and -not ((Get-Item -LiteralPath $full -Force).Attributes -band
                ([IO.FileAttributes]::Compressed -bor [IO.FileAttributes]::SparseFile -bor [IO.FileAttributes]::ReparsePoint -bor
                 [IO.FileAttributes]::ReadOnly -bor [IO.FileAttributes]::Hidden -bor [IO.FileAttributes]::System))
        })
}

# Step 1: a saved plan and changes to two of its files; returns the plan file and the two paths (or $null).
function New-ChangedPlan([string]$Preset, [string]$Dir) {
    $planFile = Join-Path $Dir 'changes.jsonl'
    $plan = Invoke-Cli @('plan', $Root, '--strategy', $Preset, '--level', 'moderate', '--json', '--moves', '--save', $planFile)
    $files = @(Get-PlannedFiles $plan.Report)
    if ($files.Count -lt 2) { Write-Host '    skip  changes between planning and execution (fewer than two planned files)'; return $null }
    Remove-Item -LiteralPath (Join-Path $Root $files[0].TrimStart('\')) -Force
    $stream = [IO.File]::Open((Join-Path $Root $files[1].TrimStart('\')), 'Append')
    $stream.Write([byte[]]::new(2MB), 0, 2MB); $stream.Close()
    return [pscustomobject]@{ File = $planFile; Gone = $files[0]; Grown = $files[1] }
}

# Step 2: the changed plan runs without the changed files.
function Test-ChangedPlan($Changed) {
    $run = Invoke-Cli (@('execute', $Root, '--plan', $Changed.File, '--json', '--check-rescan') + $Cap)
    Test-Execution $run 'changed plan'
    $drops = @($run.Report.revalidation.drops)
    Check 'changed plan: the deleted file was dropped as gone' (@($drops | Where-Object { $_.path -eq $Changed.Gone -and $_.reason -eq 'file-gone' }).Count -ge 1)
    Check 'changed plan: the grown file was dropped as changed' (@($drops | Where-Object { $_.path -eq $Changed.Grown -and $_.reason -eq 'file-changed' }).Count -ge 1)
}

# Step 3: the main run, with a stale bitmap before one move; returns its report.
function Test-MainRun([string]$Preset, [bool]$Whole) {
    $limit = if ($Whole) { @() } else { $Cap }
    $run = Invoke-Cli (@('execute', $Root, '--strategy', $Preset, '--level', 'max', '--json', '--check-rescan', '--take-destination', '3') + $limit)
    Test-Execution $run "main run ($Preset, max)"
    if ($run.Report -and $run.Report.plan.movesTried -gt 3) {
        # Elsewhere in its zone when a free run holds it; on a nearly full volume skipped as destination-in-use.
        $e = $run.Report.execution
        $handled = $e.replaced -ge 1 -or @($e.problems | Where-Object { $_.reason -eq 'destination-in-use' }).Count -ge 1
        Check 'stale bitmap: the move whose destination was taken went elsewhere or was skipped' $handled
    }
    return $run.Report
}

# Step 4a: pause and cancel in the middle of a run, then resume it.
function Test-PauseCancelResume {
    $first = Invoke-Cli @('execute', $Root, '--strategy', 'directory-clustering', '--level', 'max', '--json',
        '--chunk-mb', '1', '--pause-after', '3', '--pause-ms', '400', '--cancel-after', '12')
    if ($first.Report -and $first.Report.plan.moves -le 12) { Write-Host '    skip  pause and cancel (12 moves or fewer)'; return }
    Test-Execution $first 'paused, then cancelled' 3
    Check 'cancelled run: stopped after 12 moves' ([bool]$first.Report.execution.cancelled -and $first.Report.execution.moved -eq 12)
    $second = Invoke-Cli (@('execute', $Root, '--resume', '--json', '--check-rescan') + $Cap)
    Test-Execution $second 'resumed after cancel'
    Check 'resumed after cancel: continued with the moves left, none dropped' (
        $second.Report.plan.resumed -and $second.Report.plan.movesTried -ge 1 -and $second.Report.revalidation.dropped -eq 0)
}

# The newest execution log written after a time.
function Get-NewestLog([datetime]$After) {
    return Get-ChildItem -LiteralPath $LogDir -Filter '*.jsonl' -ErrorAction SilentlyContinue |
        Where-Object { $_.LastWriteTime -ge $After } | Sort-Object LastWriteTime | Select-Object -Last 1
}

# Step 4b: a run killed in the middle, then resumed.
function Test-KillResume([string]$Dir) {
    $start = Get-Date
    $process = Start-Process -FilePath $Cli -PassThru -WindowStyle Hidden -RedirectStandardOutput (Join-Path $Dir 'kill.out') `
        -ArgumentList @('execute', $Root, '--strategy', 'size-placement', '--level', 'max', '--json', '--chunk-mb', '1', '--limit-mbps', '8')
    $killed = $false
    for ($i = 0; $i -lt 600 -and -not $process.HasExited; $i++) {
        Start-Sleep -Milliseconds 100
        $log = Get-NewestLog $start
        if ($log -and @(Select-String -LiteralPath $log.FullName -Pattern '"type":"outcome"' -SimpleMatch).Count -ge 15) {
            Stop-Process -Id $process.Id -Force
            $killed = $true
            break
        }
    }
    $process.WaitForExit()
    if (-not $killed) { Write-Host '    skip  kill and resume (the run ended before 15 moves)'; return }
    Write-Host '    killed icd-cli execute after 15 moves'
    $resumed = Invoke-Cli (@('execute', $Root, '--resume', '--json', '--check-rescan') + $Cap)
    Test-Execution $resumed 'resumed after kill'
    Check 'resumed after kill: started where the killed run stopped' (
        $resumed.Report.plan.resumed -and $resumed.Report.execution.alreadyDone + $resumed.Report.execution.moved -ge 1)
}

# Clusters of a run that lie inside a set of runs.
function Measure-Inside([uint64]$Start, [uint64]$Length, $Runs) {
    [uint64]$inside = 0
    foreach ($g in $Runs) {
        $a = [math]::Max($Start, [uint64]$g[0]); $b = [math]::Min($Start + $Length, [uint64]$g[0] + [uint64]$g[1])
        if ($b -gt $a) { $inside += $b - $a }
    }
    return $inside
}

# Step 5 (dev, -ChurnGap): where NTFS allocates new build output after a development layout.
function Measure-ChurnGap($MainReport) {
    $zone = @($MainReport.zones | Sort-Object { ($_.gap | ForEach-Object { [uint64]$_[1] } | Measure-Object -Sum).Sum } | Select-Object -Last 1)
    if (-not $zone -or @($zone[0].gap).Count -eq 0) { Write-Host '    skip  churn gap (the plan has no gap)'; return }
    $gap = $zone[0].gap
    $dir = Join-Path $Root 'churn-probe\obj'
    New-Item -ItemType Directory -Path $dir -Force | Out-Null
    $block = [byte[]]::new(256KB)
    for ($i = 0; $i -lt 200; $i++) { [IO.File]::WriteAllBytes((Join-Path $dir "unit$i.obj"), $block) }
    Write-VolumeCache -DriveLetter $Letter
    $analysis = & $Cli analyze $Root --json --dump-extents 2>$null | ConvertFrom-Json
    [uint64]$total = 0; [uint64]$inside = 0
    foreach ($f in @($analysis.files | Where-Object { $_.path -like '\churn-probe\obj\*' })) {
        foreach ($run in $f.runs) { $total += [uint64]$run[1]; $inside += Measure-Inside ([uint64]$run[0]) ([uint64]$run[1]) $gap }
    }
    $gapClusters = ($gap | ForEach-Object { [uint64]$_[1] } | Measure-Object -Sum).Sum
    $share = if ($total -gt 0) { 100.0 * $inside / $total } else { 0 }
    $freeClusters = [uint64]$analysis.summary.freeBytes / [uint64]$MainReport.plan.bytesPerCluster
    $freeShare = 100.0 * $gapClusters / [math]::Max(1, $freeClusters)
    Write-Host ("    churn gap after '{0}': {1:N1}% of 200 new build files' clusters landed in the gap ({2:N0} clusters, {3:N1}% of free space)" -f
        $zone[0].name, $share, $gapClusters, $freeShare) -ForegroundColor Cyan
    Remove-Item -LiteralPath (Join-Path $Root 'churn-probe') -Recurse -Force
}

# Every step of one scenario on its fresh volume.
function Test-Scenario([string]$Scenario, [string]$Preset, [string]$Dir) {
    Measure-Step 'generate' {
        & $Gen $Scenario $Letter | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "gen-scenario $Scenario failed" }
        fsutil usn createjournal m=33554432 a=4194304 "${Letter}:" | Out-Null
        Write-VolumeCache -DriveLetter $Letter # Raw MFT reads see only what is on disk.
    }
    $changed = $null
    Measure-Step 'plan and change two planned files' { $script:changed = New-ChangedPlan $Preset $Dir }
    $changed = $script:changed
    Write-VolumeCache -DriveLetter $Letter
    $manifest = Join-Path $Dir 'manifest.json'
    Measure-Step 'hash the volume' { Save-Manifest $manifest }
    if ($changed) { Measure-Step 'changed plan' { Test-ChangedPlan $changed } }
    $whole = $ChurnGap -and $Scenario -eq 'dev'
    Measure-Step 'main run' { $script:main = Test-MainRun $Preset $whole }
    if ($ResumeScenarios -contains $Scenario) {
        Measure-Step 'pause, cancel and resume' { Test-PauseCancelResume }
        Measure-Step 'kill and resume' { Test-KillResume $Dir }
    }
    if ($whole -and $script:main) { Measure-ChurnGap $script:main }
    Measure-Step 'verify hashes and chkdsk' { Test-Volume $manifest 'all runs' }
}

# Hashes every file of the volume into a manifest.
function Save-Manifest([string]$Path) {
    pwsh -NoProfile -File $VerifyVol -Action Hash -Drive "${Letter}:" -Manifest $Path | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'hashing the volume failed' }
}

# Compares the volume with a manifest and runs chkdsk.
function Test-Volume([string]$Manifest, [string]$What) {
    $output = pwsh -NoProfile -File $VerifyVol -Action Verify -Drive "${Letter}:" -Manifest $Manifest 2>&1
    $ok = $LASTEXITCODE -eq 0
    Check "${What}: file hashes unchanged and chkdsk clean" $ok
    if (-not $ok) { $output | Select-Object -Last 12 | ForEach-Object { Write-Host "          $_" } }
}

if (-not (Test-Path $Cli) -or -not (Test-Path $Gen)) { throw "Build $Config first (build.bat $Config)." }
$total = [Diagnostics.Stopwatch]::StartNew()
foreach ($scenario in $Scenarios) {
    $preset = if ($PresetOf.ContainsKey($scenario)) { $PresetOf[$scenario] } else { 'mixed' }
    $dir = Join-Path $Work $scenario
    Remove-Item -LiteralPath $dir -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Path $dir -Force | Out-Null
    Write-Host "[$scenario] preset $preset"
    $clock = [Diagnostics.Stopwatch]::StartNew()
    Invoke-MakeVol @('-Action', 'Create', '-Name', "exec-$scenario", '-SizeGB', $SizeGB, '-Letter', $Letter, '-Force')
    try {
        Test-Scenario $scenario $preset $dir
    }
    catch {
        Write-Host "    error $($_.Exception.Message)`n$($_.ScriptStackTrace)" -ForegroundColor Red
        throw
    }
    finally {
        Invoke-MakeVol @('-Action', 'Remove', '-Name', "exec-$scenario")
    }
    Write-Host ("    time  scenario {0}: {1:N0} s" -f $scenario, $clock.Elapsed.TotalSeconds) -ForegroundColor DarkGray
}

Write-Host ("Total {0:N1} min." -f $total.Elapsed.TotalMinutes)
if ($script:Failures -eq 0) { Write-Host 'All execution checks passed.'; exit 0 }
Write-Host "$script:Failures execution check(s) FAILED." -ForegroundColor Red
exit 1
