<#
.SYNOPSIS
    USN churn check (T073, Phase 5 exit): on a dev scenario written with a change journal, the build and temp
    directories are detected as churn and the source, dependency and repository directories are not.

.DESCRIPTION
    Creates a test volume, creates its USN journal first, writes the dev scenario (build/clean cycles below
    repo-N\build\obj, temp files created and deleted in repo-N\.cache), flushes the volume and runs
    "icd-cli churn --json", which scans the volume and reads the journal's retained window. Every directory below
    build\obj that holds files, and every .cache directory, must be reported as churn; no directory below src,
    node_modules or .git may be.

    Needs administrator rights and a Release build. Exit code 0 when every check passes.

.EXAMPLE
    pwsh -File tests/integration/churn/check-churn.ps1
#>
[CmdletBinding()]
param(
    # Size of the test volume.
    [int]$SizeGB = 2,

    # Drive letter for the test volume.
    [string]$Letter = 'Q',

    # Build configuration whose icd-cli and gen-scenario are used.
    [string]$Config = 'Release',

    # Keep the volume afterwards (for a look at it).
    [switch]$Keep
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

# Paths (as icd-cli prints them, from the volume root) of the directories directly holding files that match.
function Get-FileDirectories([string]$Filter) {
    Get-ChildItem -LiteralPath $Root -Recurse -File -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match $Filter } |
        ForEach-Object { $_.DirectoryName.Substring(2) } | Sort-Object -Unique
}

if (-not (Test-Path $Cli) -or -not (Test-Path $Gen)) { throw "Build $Config first (build.bat $Config)." }
Invoke-MakeVol @('-Action', 'Create', '-Name', 'churn', '-SizeGB', $SizeGB, '-Letter', $Letter, '-Force')
try {
    fsutil usn createjournal m=134217728 a=16777216 "${Letter}:" | Out-Null
    & $Gen dev $Letter | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'gen-scenario dev failed' }
    Write-VolumeCache -DriveLetter $Letter
    $report = & $Cli churn $Root --json 2>$null | ConvertFrom-Json
    if ($LASTEXITCODE -ne 0) { throw 'icd-cli churn failed' }
    $churn = @($report.churnDirectories)
    Write-Host "[dev] $($report.journalRecords) journal records, $($report.sessions) sessions, $($report.churnFiles) files flagged, $($churn.Count) churn directories"
    $build = @(Get-FileDirectories '\\build\\obj\\')
    $temp = @(Get-FileDirectories '\\\.cache\\')
    $missingBuild = @($build | Where-Object { $_ -notin $churn })
    $missingTemp = @($temp | Where-Object { $_ -notin $churn })
    Check "build output directories are churn ($($build.Count))" ($build.Count -gt 0 -and $missingBuild.Count -eq 0) ($missingBuild -join ', ')
    Check "temp directories are churn ($($temp.Count))" ($temp.Count -gt 0 -and $missingTemp.Count -eq 0) ($missingTemp -join ', ')
    $wrong = @($churn | Where-Object { $_ -match '\\(src|node_modules|\.git)(\\|$)' })
    Check 'no source, dependency or repository directory is churn' ($wrong.Count -eq 0) ($wrong -join ', ')
}
finally {
    if (-not $Keep) { Invoke-MakeVol @('-Action', 'Remove', '-Name', 'churn') }
}

if ($script:Failures -eq 0) { Write-Host 'All churn checks passed.'; exit 0 }
Write-Host "$script:Failures churn check(s) FAILED." -ForegroundColor Red
exit 1
