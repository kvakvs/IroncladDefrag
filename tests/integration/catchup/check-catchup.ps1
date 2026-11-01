<#
.SYNOPSIS
    USN catch-up check (T065): after scripted changes on a test volume, catching the saved snapshot up from the change
    journal gives exactly the snapshot a full rescan gives.

.DESCRIPTION
    Creates a test volume with a scenario and a USN journal, analyses it ("icd-cli analyze" saves the snapshot), then
    in rounds makes seeded changes (new small and large files, appends, interleaved appends that fragment, truncation,
    deletions, renames and moves of files and a directory, a big new directory, named streams, hard links, a deleted
    tree), flushes the volume and runs "icd-cli check-catchup", which catches the snapshot up and compares it with a
    full MFT scan. The analysis between rounds catches up and saves again, so chained catch-ups are covered. Finally
    the journal is deleted and recreated, which must force a rescan.

    Needs administrator rights and a Release build. Exit code 0 when every check passes.

.EXAMPLE
    pwsh -File tests/integration/catchup/check-catchup.ps1
#>
[CmdletBinding()]
param(
    # Scenario the volume starts with.
    [string]$Scenario = 'mixed',

    # Size of the test volume.
    [int]$SizeGB = 2,

    # Drive letter for the test volume.
    [string]$Letter = 'Q',

    # Rounds of changes.
    [int]$Rounds = 2,

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
$Random = [Random]::new(20261101)
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

# Random bytes.
function Get-Bytes([int]$Count) { $b = [byte[]]::new($Count); $Random.NextBytes($b); return , $b }

# Files of the volume outside the system folders.
function Get-UserFiles { Get-ChildItem -LiteralPath $Root -Recurse -File -ErrorAction SilentlyContinue | Where-Object { $_.FullName -notmatch 'System Volume Information' } }

# One round of seeded changes.
function Invoke-Changes([int]$Round) {
    $dir = Join-Path $Root "changes-$Round"
    New-Item -ItemType Directory -Force $dir | Out-Null
    foreach ($i in 0..29) { [IO.File]::WriteAllBytes((Join-Path $dir "small-$i.bin"), (Get-Bytes ($Random.Next(1, 600)))) }
    foreach ($i in 0..5) { [IO.File]::WriteAllBytes((Join-Path $dir "large-$i.bin"), (Get-Bytes ($Random.Next(1, 8) * 1MB))) }
    # Interleaved appends: these files fragment.
    $streams = 0..3 | ForEach-Object { [IO.File]::Open((Join-Path $dir "frag-$_.bin"), 'Create', 'Write') }
    foreach ($step in 0..15) { foreach ($s in $streams) { $chunk = Get-Bytes 65536; $s.Write($chunk, 0, $chunk.Length); $s.Flush() } }
    $streams | ForEach-Object { $_.Dispose() }
    $files = @(Get-UserFiles | Where-Object { $_.FullName -notlike "$dir*" })
    foreach ($k in 0..9) {
        if ($files.Count -eq 0) { break }
        $f = $files[$Random.Next($files.Count)]
        if (-not (Test-Path -LiteralPath $f.FullName)) { continue }
        switch ($k % 5) {
            0 { [IO.File]::AppendAllText($f.FullName, ('x' * $Random.Next(1, 200000))) }
            1 { Remove-Item -LiteralPath $f.FullName -Force }
            2 { Move-Item -LiteralPath $f.FullName -Destination (Join-Path $dir "moved-$k$($f.Extension)") -Force }
            3 { $s = [IO.File]::Open($f.FullName, 'Open', 'ReadWrite'); $s.SetLength([long]($s.Length / 2)); $s.Dispose() }
            4 { Set-Content -LiteralPath $f.FullName -Stream "tag$Round" -Value 'catch-up' }
        }
    }
    New-Item -ItemType HardLink -Path (Join-Path $dir 'link.bin') -Target (Join-Path $dir 'large-0.bin') | Out-Null
    $big = Join-Path $dir 'bigdir'
    New-Item -ItemType Directory -Force $big | Out-Null
    foreach ($i in 0..399) { [IO.File]::WriteAllText((Join-Path $big ("entry-{0:d4}-with-a-longer-name.txt" -f $i)), "$i") }
    if ($Round -gt 0) {
        Rename-Item -LiteralPath (Join-Path $Root "changes-$($Round - 1)") -NewName "renamed-$($Round - 1)"
        Remove-Item -LiteralPath (Join-Path $Root "renamed-$($Round - 1)\bigdir") -Recurse -Force
    }
}

# Flushes the volume so raw MFT reads see every change.
function Sync-Volume { Write-VolumeCache -DriveLetter $Letter }

# Analyses (saving the snapshot) and returns the analysis's report.
function Invoke-Analysis {
    $out = & $Cli analyze $Root --json 2>$null | ConvertFrom-Json
    if ($LASTEXITCODE -ne 0) { throw 'icd-cli analyze failed' }
    return $out
}

# Runs check-catchup; returns its exit code and shows its lines.
function Invoke-CheckCatchUp {
    $lines = & $Cli check-catchup $Root 2>&1
    $code = $LASTEXITCODE
    $lines | ForEach-Object { Write-Host "      $_" }
    return $code, ($lines -join "`n")
}

if (-not (Test-Path $Cli) -or -not (Test-Path $Gen)) { throw "Build $Config first (build.bat $Config)." }
Invoke-MakeVol @('-Action', 'Create', '-Name', 'catchup', '-SizeGB', $SizeGB, '-Letter', $Letter, '-Force')
& $Gen $Scenario $Letter | Out-Null
if ($LASTEXITCODE -ne 0) { throw "gen-scenario $Scenario failed" }
fsutil usn createjournal m=67108864 a=8388608 "${Letter}:" | Out-Null
Sync-Volume
$first = Invoke-Analysis
Write-Host "[first analysis] $($first.summary.files) files, saved snapshot: $($first.scan.savedSnapshot)"
Check 'first analysis is a full scan' (-not $first.scan.caughtUp)

for ($round = 0; $round -lt $Rounds; $round++) {
    Write-Host "[round $round]"
    Invoke-Changes $round
    Sync-Volume
    $code, $text = Invoke-CheckCatchUp
    Check 'catch-up equals a full rescan' ($code -eq 0)
    $report = Invoke-Analysis
    Check 'analysis catches up and saves' ($report.scan.caughtUp) "($($report.scan.savedSnapshot))"
}

Write-Host '[nothing changed]'
Sync-Volume
$code, $text = Invoke-CheckCatchUp
Check 'unchanged volume is up to date' ($code -eq 0 -and $text -match 'up to date')

Write-Host '[journal recreated]'
fsutil usn deletejournal /d "${Letter}:" | Out-Null
# Deleting runs in the background; creating fails until it is done.
foreach ($try in 1..60) {
    fsutil usn createjournal m=67108864 a=8388608 "${Letter}:" 2>&1 | Out-Null
    fsutil usn queryjournal "${Letter}:" 2>&1 | Out-Null
    if ($LASTEXITCODE -eq 0) { break }
    Start-Sleep -Milliseconds 500
}
$code, $text = Invoke-CheckCatchUp
Check 'a recreated journal forces a rescan' ($code -ne 0 -and $text -match 'recreated')

Invoke-MakeVol @('-Action', 'Remove', '-Name', 'catchup')
if ($script:Failures -eq 0) { Write-Host 'All catch-up checks passed.' -ForegroundColor Green; exit 0 }
Write-Host "$script:Failures check(s) failed." -ForegroundColor Red
exit 1
