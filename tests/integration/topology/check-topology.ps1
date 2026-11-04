<#
.SYNOPSIS
    Topology check (T122, Phase 9 exit): a volume on a Storage Spaces disk is analysed and planned but never executed.

.DESCRIPTION
    Builds a temporary Storage Spaces pool from two test VHDX files, creates a simple space with an NTFS volume on it,
    fills it with a small scenario and checks that:
      - "icd-cli plan" still plans the volume (analysis and plans keep working);
      - "icd-cli execute" refuses it before any move and names Storage Spaces as the reason.
    Everything is removed at the end (volume, space, pool, VHDX files), also after a failure. Of the other setups,
    dynamic disks cannot be created on current Windows ("convert dynamic" is not supported), and RAID, removable media
    and BitLocker need hardware or system changes; unit tests on the drive facts cover them. Needs administrator rights
    and a Release (or -Config) build of icd-cli and gen-scenario.

.EXAMPLE
    pwsh -File tests/integration/topology/check-topology.ps1
#>
[CmdletBinding()]
param(
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
$Dir = if ($env:ICD_TESTVOL_DIR) { $env:ICD_TESTVOL_DIR } else { Join-Path $env:LOCALAPPDATA 'IroncladDefrag\testvol' }
$Pool = 'icd-topology'
$Space = 'icd-topology-space'
$Members = @(1, 2) | ForEach-Object { Join-Path $Dir "topology-member$_.vhdx" }
$script:Failures = 0

# Records one check result.
function Check([string]$What, [bool]$Ok, [string]$Detail = '') {
    if ($Ok) { Write-Host "    ok    $What" }
    else { Write-Host "    FAIL  $What $Detail" -ForegroundColor Red; $script:Failures++ }
}

# Runs a diskpart script; throws with its output when it fails (or, with -Quiet, ignores the failure).
function Invoke-Diskpart([string[]]$Lines, [switch]$Quiet) {
    $file = New-TemporaryFile
    try {
        Set-Content -LiteralPath $file -Value $Lines -Encoding ascii
        $output = & diskpart.exe /s $file.FullName 2>&1 | Out-String
        if ($LASTEXITCODE -ne 0 -and -not $Quiet) { throw "diskpart failed:`n$($Lines -join "`n")`n$output" }
    }
    finally {
        Remove-Item -LiteralPath $file -Force
    }
}

# Two attached, empty 5 GB VHDX disks for the pool (Storage Spaces takes only raw disks of 4 GB or more).
function New-Members {
    New-Item -ItemType Directory -Force -Path $Dir | Out-Null
    foreach ($vhdx in $Members) {
        if (Test-Path -LiteralPath $vhdx) { throw "$vhdx exists; remove it first (a previous run did not clean up)" }
        Invoke-Diskpart @("create vdisk file=`"$vhdx`" maximum=5120 type=expandable", "select vdisk file=`"$vhdx`"",
            'attach vdisk')
    }
    Start-Sleep -Seconds 2 # Storage management sees the new disks.
    return $Members | ForEach-Object { $n = (Get-DiskImage -ImagePath $_ | Get-Disk).Number; Get-PhysicalDisk | Where-Object DeviceId -EQ "$n" }
}

# The pool, a 3 GB simple space on it and an NTFS volume with the label ICD-TEST at $Letter.
function New-SpaceVolume($Disks) {
    $subsystem = Get-StorageSubSystem -FriendlyName 'Windows Storage*' | Select-Object -First 1
    New-StoragePool -FriendlyName $Pool -StorageSubSystemUniqueId $subsystem.UniqueId -PhysicalDisks $Disks | Out-Null
    $disk = New-VirtualDisk -StoragePoolFriendlyName $Pool -FriendlyName $Space -ResiliencySettingName Simple `
        -ProvisioningType Fixed -Size 3GB | Get-Disk
    Initialize-Disk -Number $disk.Number -PartitionStyle GPT
    New-Partition -DiskNumber $disk.Number -UseMaximumSize -DriveLetter $Letter |
        Format-Volume -FileSystem NTFS -NewFileSystemLabel 'ICD-TEST' -Confirm:$false | Out-Null
}

# Removes whatever of the space, pool and member VHDX files exists.
function Remove-All {
    if (Get-VirtualDisk -FriendlyName $Space -ErrorAction SilentlyContinue) {
        Remove-VirtualDisk -FriendlyName $Space -Confirm:$false
    }
    if (Get-StoragePool -FriendlyName $Pool -ErrorAction SilentlyContinue) {
        Remove-StoragePool -FriendlyName $Pool -Confirm:$false
    }
    foreach ($vhdx in $Members) {
        if (Test-Path -LiteralPath $vhdx) {
            Invoke-Diskpart @("select vdisk file=`"$vhdx`"", 'detach vdisk') -Quiet
            Remove-Item -LiteralPath $vhdx -Force
        }
    }
}

$principal = [Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Needs administrator rights.' }
if (-not (Test-Path $Cli) -or -not (Test-Path $Gen)) { throw "Build $Config first (build.bat $Config)." }
try {
    New-SpaceVolume (New-Members)
    & $Gen mixed $Letter | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'gen-scenario mixed failed' }
    Write-Host "[storage-spaces] ${Letter}: on a Storage Spaces disk"
    & $Cli plan $Letter --strategy mixed --json | Out-Null
    Check 'plan: the volume is still planned' ($LASTEXITCODE -eq 0) "(exit $LASTEXITCODE)"
    $execute = & $Cli execute $Letter --strategy mixed --level fast 2>&1 | Out-String
    Check 'execute: refused' ($LASTEXITCODE -ne 0) "(exit $LASTEXITCODE)"
    Check 'execute: names Storage Spaces' ($execute -match 'Moves are blocked on this drive: it is a Storage Spaces disk') $execute.Trim()
}
finally {
    Remove-All
}

if ($script:Failures -eq 0) { Write-Host 'All topology checks passed.'; exit 0 }
Write-Host "$script:Failures topology check(s) FAILED." -ForegroundColor Red
exit 1
