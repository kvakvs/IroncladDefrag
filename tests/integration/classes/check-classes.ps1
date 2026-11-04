<#
.SYNOPSIS
    Class check (T074, Phase 5 exit): on the mixed scenario every file gets the class its place in the scenario calls
    for, and the per-class bytes of the summary equal the files' clusters.

.DESCRIPTION
    Creates a test volume with the mixed scenario (dev repositories, photos, archives, video), mounts it read-only and
    runs "icd-cli analyze --json --dump-extents". Each dumped file below \mixed is checked against the class the
    scenario's layout and plan.md 8.3-8.6 give it, written out here independently of the rule engine (sources,
    dependencies, build output, temp, repository packs, photos, the catalog, archives, disc images, VM disks, video of
    any size (.ts from 10 MB), unknown). The summary's bytes per class must equal the sum of the dumped files' runs of that class,
    and its file counts must cover every file. The class colors themselves are checked by ColorModesTest (tiles in the
    class mode show exactly the legend's colors).

    Needs administrator rights and a Release build. Exit code 0 when every check passes.

.EXAMPLE
    pwsh -File tests/integration/classes/check-classes.ps1
#>
[CmdletBinding()]
param(
    # Size of the test volume.
    [int]$SizeGB = 2,

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

# The class a file of the mixed scenario should have (paths as the report prints them).
function Get-ExpectedClass([string]$Path, [uint64]$Size) {
    $ext = [IO.Path]::GetExtension($Path).TrimStart('.').ToLowerInvariant()
    switch -Regex ($Path) {
        '\\\.git\\objects\\pack\\[^\\]+\.(pack|idx)$' { return 'repository' }
        '\\node_modules\\' { return 'dependencies' }
        '\\build\\' { return 'build' }
        '\\\.cache\\' { return 'temp' }
        '\\src\\' { return 'source' }
        '\\photos\\catalog\.lrcat$' { return 'catalogs' }
        '\\photos\\' { return 'photos' }
    }
    if ($Path -match '\\video\\') {
        if ($ext -ne 'ts' -or $Size -ge 10MB) { return 'video' } # Videos of any size; .ts from 10 MB.
        return 'source' # A small .ts is TypeScript.
    }
    $byExtension = @{ iso = 'disc-images'; bin = 'disc-images'; zip = 'archives'; '7z' = 'archives'; vhdx = 'vm-disks' }
    if ($byExtension.ContainsKey($ext)) { return $byExtension[$ext] }
    return 'unknown'
}

if (-not (Test-Path $Cli) -or -not (Test-Path $Gen)) { throw "Build $Config first (build.bat $Config)." }
Invoke-MakeVol @('-Action', 'Create', '-Name', 'classes', '-SizeGB', $SizeGB, '-Letter', $Letter, '-Force')
try {
    & $Gen mixed $Letter | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'gen-scenario mixed failed' }
    Invoke-MakeVol @('-Action', 'Dismount', '-Name', 'classes')
    Invoke-MakeVol @('-Action', 'Mount', '-Name', 'classes', '-ReadOnly')
    $report = & $Cli analyze $Root --json --dump-extents 2>$null | ConvertFrom-Json
    if ($LASTEXITCODE -ne 0) { throw 'icd-cli analyze failed' }
    $cluster = [uint64]$report.volume.bytesPerCluster
    $files = @($report.files)
    $mine = @($files | Where-Object { $_.path -like '\mixed\*' })
    $expectedOf = { param($f) if ($f.flags -contains 'directory') { 'directories' } else { Get-ExpectedClass $f.path ([uint64]$f.size) } }
    $wrong = @($mine | Where-Object { $_.class -ne (& $expectedOf $_) })
    $classes = @($report.summary.classes)
    Write-Host "[mixed] $($report.summary.files) files, $($mine.Count) with clusters checked, classes: $(($classes | ForEach-Object { "$($_.id) $($_.files)" }) -join ', ')"
    $sample = ($wrong | Select-Object -First 5 | ForEach-Object { "$($_.path) = $($_.class)" }) -join '; '
    Check 'every file of the scenario has its class' ($wrong.Count -eq 0) "($($wrong.Count) wrong: $sample)"
    $seen = @($mine | Group-Object class | ForEach-Object { $_.Name })
    $expectedSeen = @('directories', 'source', 'repository', 'dependencies', 'build', 'temp', 'photos', 'catalogs', 'archives', 'disc-images', 'vm-disks')
    # Videos are video at any size, but a volume with only small .ts files would have none.
    if (@($mine | Where-Object { $_.path -like '\mixed\video\*' -and (& $expectedOf $_) -eq 'video' }).Count -gt 0) { $expectedSeen += 'video' }
    $missing = @($expectedSeen | Where-Object { $_ -notin $seen })
    Check 'the scenario covers its classes' ($missing.Count -eq 0) ($missing -join ', ')
    $byClass = @{}
    foreach ($f in $files) {
        $clusters = 0
        foreach ($run in $f.runs) { $clusters += [uint64]$run[1] }
        $byClass[$f.class] = [uint64]($byClass[$f.class] + $clusters * $cluster)
    }
    $bytesOff = @($classes | Where-Object { [uint64]$_.bytes -ne [uint64]$byClass[$_.id] })
    Check 'summary bytes per class = the files'' clusters' ($bytesOff.Count -eq 0) (($bytesOff | ForEach-Object { "$($_.id): $($_.bytes) vs $($byClass[$_.id])" }) -join '; ')
    # The summary's file count leaves out directories and NTFS metadata, which have classes of their own.
    $counted = ($classes | Where-Object { $_.id -notin @('directories', 'metadata') } | Measure-Object -Property files -Sum).Sum
    Check 'class file counts cover every file' ($counted -eq $report.summary.files) "($counted of $($report.summary.files))"
}
finally {
    Invoke-MakeVol @('-Action', 'Remove', '-Name', 'classes')
}

if ($script:Failures -eq 0) { Write-Host 'All class checks passed.'; exit 0 }
Write-Host "$script:Failures class check(s) FAILED." -ForegroundColor Red
exit 1
