<#
.SYNOPSIS
    Windows schedule check (T120, plan.md 10.4): a test volume is taken out of Windows' scheduled optimization and put
    back, through "icd-cli schedule", in the forms the Optimize Drives dialog writes.

.DESCRIPTION
    Creates a 1 GB test VHDX and checks:
      - it starts covered by the schedule (this machine schedules every drive; the check stops otherwise);
      - "--remove": the ScheduledDefrag task's arguments become "<its GUID path> -e <flags>", TaskSettings holds
        fAllVolumes 0, fExclude 1 and Volumes = its GUID path, and icd-cli reports "off";
      - "--restore": the arguments are again exactly what they were and icd-cli reports "on".
    At the end the TaskSettings values are put back as they were (values that did not exist are removed again) and the
    VHDX is removed, also after a failure. With -Pause the script waits after --remove, so the unchecked drive can be
    looked at in Optimize Drives > Change settings > Choose. Needs administrator rights and a Release (or -Config)
    build of icd-cli.

.EXAMPLE
    pwsh -File tests/integration/schedule/check-schedule.ps1
#>
[CmdletBinding()]
param(
    # Drive letter for the test volume.
    [string]$Letter = 'Q',

    # Build configuration whose icd-cli is used.
    [string]$Config = 'Release',

    # Wait for Enter after the drive was taken out, to look at Optimize Drives.
    [switch]$Pause
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$Repo = Resolve-Path (Join-Path $PSScriptRoot '..\..\..')
$Cli = Join-Path $Repo "build\bin\$Config\icd-cli.exe"
$MakeVol = Join-Path $Repo 'tools\testvol\make-testvol.ps1'
$Key = 'HKLM:\SOFTWARE\Microsoft\Dfrg\TaskSettings'
$Written = @('fAllVolumes', 'fExclude', 'Volumes')
$Name = 'schedule-check'
$script:Failures = 0

# Records one check result.
function Check([string]$What, [bool]$Ok, [string]$Detail = '') {
    if ($Ok) { Write-Host "    ok    $What" }
    else { Write-Host "    FAIL  $What $Detail" -ForegroundColor Red; $script:Failures++ }
}

# The defrag task's arguments.
function Get-TaskArguments { (Get-ScheduledTask -TaskName ScheduledDefrag -TaskPath '\Microsoft\Windows\Defrag\').Actions[0].Arguments }

# Runs icd-cli schedule and returns its state word ("on", "off", "unknown"); empty when it failed.
function Invoke-Schedule([string[]]$Flags) {
    $out = & $Cli schedule $Letter @Flags 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0) { Write-Host $out.Trim(); return '' }
    if ($out -match ': (on|off|unknown)\s*$') { return $Matches[1] }
    return ''
}

# The TaskSettings values this check may write, as they are now (absent ones are missing from the table).
function Get-WrittenValues {
    $values = @{}
    $item = Get-Item $Key -ErrorAction SilentlyContinue
    if ($item) { foreach ($n in $Written) { if ($item.GetValueNames() -contains $n) { $values[$n] = $item.GetValue($n) } } }
    return $values
}

# Puts the TaskSettings values back as they were; values that did not exist are removed.
function Restore-WrittenValues($Before) {
    foreach ($n in $Written) {
        if ($Before.ContainsKey($n)) {
            $kind = if ($n -eq 'Volumes') { 'String' } else { 'DWord' }
            Set-ItemProperty -Path $Key -Name $n -Value $Before[$n] -Type $kind
        }
        elseif ((Get-Item $Key).GetValueNames() -contains $n) { Remove-ItemProperty -Path $Key -Name $n }
    }
}

if (-not (Test-Path $Cli)) { throw "Build $Config first (build.bat $Config)." }
$argsBefore = Get-TaskArguments
$valuesBefore = Get-WrittenValues
pwsh -NoProfile -File $MakeVol -Action Create -Name $Name -SizeGB 1 -Letter $Letter -Force | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'make-testvol.ps1 Create failed' }
try {
    $guid = (Get-Volume -DriveLetter $Letter).Path
    Write-Host "[schedule] ${Letter}: $guid, task arguments '$argsBefore'"
    if ((Invoke-Schedule @()) -ne 'on') { throw "${Letter}: is not covered by the schedule now; this check needs it on" }
    Check 'remove: reported off' ((Invoke-Schedule @('--remove')) -eq 'off')
    $argsOut = Get-TaskArguments
    Check 'remove: task arguments leave the volume out' ($argsOut -like "*$guid*-e *") "('$argsOut')"
    $settings = Get-ItemProperty $Key
    Check 'remove: TaskSettings as the dialog writes them' ($settings.fAllVolumes -eq 0 -and $settings.fExclude -eq 1 -and
        $settings.Volumes -like "*$guid*") "($($settings.fAllVolumes) $($settings.fExclude) '$($settings.Volumes)')"
    if ($Pause) { Read-Host "${Letter}: is out of the schedule; look at Optimize Drives, then press Enter" | Out-Null }
    Check 'restore: reported on' ((Invoke-Schedule @('--restore')) -eq 'on')
    $argsBack = Get-TaskArguments
    Check 'restore: task arguments as before' ($argsBack -eq $argsBefore) "('$argsBack')"
}
finally {
    Restore-WrittenValues $valuesBefore
    pwsh -NoProfile -File $MakeVol -Action Remove -Name $Name | Out-Null
}

if ($script:Failures -eq 0) { Write-Host 'All schedule checks passed.'; exit 0 }
Write-Host "$script:Failures schedule check(s) FAILED." -ForegroundColor Red
exit 1
