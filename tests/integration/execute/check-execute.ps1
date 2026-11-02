<#
.SYNOPSIS
    Execution check (T104, Phase 7 exit): on every test-bed scenario a plan is analysed, revalidated, executed and
    verified (plan.md 12): file hashes unchanged, chkdsk clean, every moved file where the plan predicted when the
    volume is scanned again. Also: changes between planning and execution are detected, a stale bitmap is handled,
    pause, cancel and resume are safe, a killed run resumes to completion, and where NTFS puts new build output after a
    development layout (the churn gap of plan.md 8.3) is measured.

.DESCRIPTION
    Per scenario, on a fresh test volume with a USN journal (so revalidation catches up instead of rescanning):
      1. Changes between planning and execution: "icd-cli plan --save" at Moderate, then one planned file deleted and
         another grown by 2 MB; "icd-cli execute --plan" must drop them (file-gone, file-changed) and run the rest.
      2. Main run at -Level with the scenario's preset; another file takes the first destination cluster of a move
         just before it starts (--take-destination), which must go elsewhere (replaced) and verify.
      3. Pause and cancel in the middle of a run of another strategy (--pause-after, --cancel-after), then --resume.
      4. A run of a third strategy, slowed by a throughput limit, killed with Stop-Process mid-run, then --resume.
      5. dev only: 200 new 256 KB files in a build folder after the development layout; the share of their clusters
         NTFS put into the churn gap is reported (plan.md 8.3 asks for it to be recorded).
    After every execution: "--check-rescan" (moved files where predicted after a full rescan), the file hashes against
    the manifest taken before it, and chkdsk /scan.

    Needs administrator rights and a Release build. Exit code 0 when every check passes.

.EXAMPLE
    pwsh -File tests/integration/execute/check-execute.ps1
    pwsh -File tests/integration/execute/check-execute.ps1 -Scenarios all -SizeGB 4
#>
[CmdletBinding()]
param(
    # Scenarios to check: a list, or 'all'. The quick default takes every special case and a realistic mixed volume.
    [string[]]$Scenarios = @('special', 'mixed'),

    # Size of each test volume.
    [int]$SizeGB = 2,

    # Drive letter for the test volume.
    [string]$Letter = 'Q',

    # Level of the main run.
    [ValidateSet('fast', 'moderate', 'max')]
    [string]$Level = 'max',

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

# Runs icd-cli with JSON output; returns the report and the exit code.
function Invoke-Cli([string[]]$Arguments) {
    $text = & $Cli @Arguments 2>$null
    $code = $LASTEXITCODE
    $report = if ($text) { ($text -join "`n") | ConvertFrom-Json } else { $null }
    return [pscustomobject]@{ Report = $report; Code = $code }
}

# Hashes every file of the volume into a manifest.
function Save-Manifest([string]$Path) {
    pwsh -NoProfile -File $VerifyVol -Action Hash -Drive "${Letter}:" -Manifest $Path | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'hashing the volume failed' }
}

# Compares the volume with a manifest and runs chkdsk.
function Test-Volume([string]$Manifest, [string]$What) {
    pwsh -NoProfile -File $VerifyVol -Action Verify -Drive "${Letter}:" -Manifest $Manifest | Out-Null
    Check "${What}: file hashes unchanged and chkdsk clean" ($LASTEXITCODE -eq 0)
}

# The common checks of one execution report.
function Test-Execution($Result, [string]$What, [int]$ExpectedCode = 0) {
    $r = $Result.Report
    if ($null -eq $r) { Check "${What}: icd-cli execute reported" $false "(exit $($Result.Code))"; return }
    $e = $r.execution
    Write-Host ("    {0}: {1} moved, {2} done before, {3} partial, {4} skipped, {5} replaced, {6} dropped, {7:N1} s" -f $What,
        $e.moved, $e.alreadyDone, $e.partial, $e.skipped, $e.replaced, $r.revalidation.dropped, $e.seconds)
    foreach ($p in @($e.problems | Select-Object -First 5)) { Write-Host "          $($p.outcome) $($p.reason) $($p.path)" }
    Check "${What}: exit code $ExpectedCode" ($Result.Code -eq $ExpectedCode) "(got $($Result.Code); $($r.refused))"
    Check "${What}: every completed move verified" ($e.unverified -eq 0) "($($e.unverified) unverified)"
    if ($r.PSObject.Properties.Name -contains 'rescan') {
        Check "${What}: moved files where predicted after a rescan" ($r.rescan.mismatches -eq 0) `
            "($($r.rescan.mismatches) of $($r.rescan.filesChecked): $(@($r.rescan.samples | Select-Object -First 2) -join '; '))"
        if ($null -ne $r.rescan.snapshotDifferences -and $r.rescan.snapshotDifferences -ne 0) {
            Write-Host "    note  saved snapshot differs from the rescan in $($r.rescan.snapshotDifferences) places: $(@($r.rescan.snapshotSamples | Select-Object -First 2) -join '; ')" -ForegroundColor Yellow
        }
    }
}

# Planned regular files of a saved plan's report, for scripted changes.
function Get-PlannedFiles($PlanReport) {
    $level = $PlanReport.levels | Where-Object { $_.level -eq 'moderate' }
    return @($level.moveList | ForEach-Object { $_.file } | Select-Object -Unique | Where-Object {
        $full = Join-Path $Root $_.TrimStart('\')
        (Test-Path -LiteralPath $full -PathType Leaf) -and -not ((Get-Item -LiteralPath $full -Force).Attributes -band
            ([IO.FileAttributes]::Compressed -bor [IO.FileAttributes]::SparseFile -bor [IO.FileAttributes]::ReparsePoint))
    })
}

# Step 1: changes between planning and execution are detected.
function Test-ChangedPlan([string]$Preset, [string]$Dir) {
    $planFile = Join-Path $Dir 'changes.jsonl'
    $plan = Invoke-Cli @('plan', $Root, '--strategy', $Preset, '--level', 'moderate', '--json', '--moves', '--save', $planFile)
    $files = Get-PlannedFiles $plan.Report
    if ($files.Count -lt 2) { Write-Host '    skip  changes between planning and execution (fewer than two planned files)'; return }
    $gone = $files[0]; $grown = $files[1]
    Remove-Item -LiteralPath (Join-Path $Root $gone.TrimStart('\')) -Force
    $stream = [IO.File]::Open((Join-Path $Root $grown.TrimStart('\')), 'Append')
    $stream.Write([byte[]]::new(2MB), 0, 2MB); $stream.Close()
    Write-VolumeCache -DriveLetter $Letter
    $manifest = Join-Path $Dir 'changes.json'
    Save-Manifest $manifest
    $run = Invoke-Cli @('execute', $Root, '--plan', $planFile, '--json', '--check-rescan')
    Test-Execution $run 'changed plan'
    $drops = @($run.Report.revalidation.drops)
    Check 'changed plan: the deleted file was dropped as gone' (@($drops | Where-Object { $_.path -eq $gone -and $_.reason -eq 'file-gone' }).Count -ge 1)
    Check 'changed plan: the grown file was dropped as changed' (@($drops | Where-Object { $_.path -eq $grown -and $_.reason -eq 'file-changed' }).Count -ge 1)
    Test-Volume $manifest 'changed plan'
}

# Step 2: the main run, with a stale bitmap before one move; returns its report.
function Test-MainRun([string]$Preset, [string]$Dir) {
    $manifest = Join-Path $Dir 'main.json'
    Save-Manifest $manifest
    $run = Invoke-Cli @('execute', $Root, '--strategy', $Preset, '--level', $Level, '--json', '--check-rescan', '--take-destination', '3')
    Test-Execution $run "main run ($Preset, $Level)"
    if ($run.Report -and $run.Report.plan.moves -gt 3) {
        Check 'stale bitmap: the move whose destination was taken went elsewhere' ($run.Report.execution.replaced -ge 1)
    }
    Test-Volume $manifest 'main run'
    return $run.Report
}

# Step 3: pause and cancel in the middle of a run, then resume it.
function Test-PauseCancelResume([string]$Dir) {
    $manifest = Join-Path $Dir 'cancel.json'
    Save-Manifest $manifest
    $first = Invoke-Cli @('execute', $Root, '--strategy', 'directory-clustering', '--level', 'max', '--json',
        '--chunk-mb', '1', '--pause-after', '3', '--pause-ms', '400', '--cancel-after', '12')
    if ($first.Report -and $first.Report.plan.moves -le 12) { Write-Host '    skip  pause and cancel (12 moves or fewer)'; return }
    Test-Execution $first 'paused, then cancelled' 3
    Check 'cancelled run: stopped before the end' ([bool]$first.Report.execution.cancelled)
    $second = Invoke-Cli @('execute', $Root, '--resume', '--json', '--check-rescan')
    Test-Execution $second 'resumed after cancel'
    Check 'resumed run: nothing left over' (-not $second.Report.execution.cancelled -and $second.Report.revalidation.dropped -eq 0)
    Test-Volume $manifest 'cancel and resume'
}

# The newest execution log written after a time.
function Get-NewestLog([datetime]$After) {
    return Get-ChildItem -LiteralPath $LogDir -Filter '*.jsonl' -ErrorAction SilentlyContinue |
        Where-Object { $_.LastWriteTime -ge $After } | Sort-Object LastWriteTime | Select-Object -Last 1
}

# Step 4: a run killed in the middle, then resumed.
function Test-KillResume([string]$Dir) {
    $manifest = Join-Path $Dir 'kill.json'
    Save-Manifest $manifest
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
    if (-not $killed) { Write-Host '    skip  kill and resume (the run ended before 15 moves)'; Test-Volume $manifest 'short run'; return }
    Write-Host '    killed icd-cli execute after 15 moves'
    $resumed = Invoke-Cli @('execute', $Root, '--resume', '--json', '--check-rescan')
    Test-Execution $resumed 'resumed after kill'
    Check 'resumed run: started where the killed one stopped' ($resumed.Report.plan.resumed -and $resumed.Report.plan.movesTried -lt $resumed.Report.plan.moves)
    Test-Volume $manifest 'kill and resume'
}

# Whether a run lies inside any of a set of runs; returns the clusters it has inside.
function Measure-Inside([uint64]$Start, [uint64]$Length, $Runs) {
    [uint64]$inside = 0
    foreach ($g in $Runs) {
        $a = [math]::Max($Start, [uint64]$g[0]); $b = [math]::Min($Start + $Length, [uint64]$g[0] + [uint64]$g[1])
        if ($b -gt $a) { $inside += $b - $a }
    }
    return $inside
}

# Step 5 (dev): where NTFS allocates new build output after a development layout.
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

if (-not (Test-Path $Cli) -or -not (Test-Path $Gen)) { throw "Build $Config first (build.bat $Config)." }
foreach ($scenario in $Scenarios) {
    $preset = if ($PresetOf.ContainsKey($scenario)) { $PresetOf[$scenario] } else { 'mixed' }
    $dir = Join-Path $Work $scenario
    Remove-Item -LiteralPath $dir -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Path $dir -Force | Out-Null
    Write-Host "[$scenario] preset $preset"
    Invoke-MakeVol @('-Action', 'Create', '-Name', "exec-$scenario", '-SizeGB', $SizeGB, '-Letter', $Letter, '-Force')
    try {
        & $Gen $scenario $Letter | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "gen-scenario $scenario failed" }
        fsutil usn createjournal m=33554432 a=4194304 "${Letter}:" | Out-Null
        Test-ChangedPlan $preset $dir
        $main = Test-MainRun $preset $dir
        Test-PauseCancelResume $dir
        Test-KillResume $dir
        if ($scenario -eq 'dev' -and $main) { Measure-ChurnGap $main }
    }
    finally {
        Invoke-MakeVol @('-Action', 'Remove', '-Name', "exec-$scenario")
    }
}

if ($script:Failures -eq 0) { Write-Host 'All execution checks passed.'; exit 0 }
Write-Host "$script:Failures execution check(s) FAILED." -ForegroundColor Red
exit 1
