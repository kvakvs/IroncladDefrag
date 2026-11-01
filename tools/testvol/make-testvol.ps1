<#
.SYNOPSIS
    Creates, mounts, dismounts and removes VHDX test volumes for IroncladDefrag (plan.md section 12).

.DESCRIPTION
    Test volumes are disposable dynamic VHDX files formatted NTFS with a chosen cluster size. Every
    move and every analysis check runs on them before it is allowed near a real drive.
    All disk work goes through diskpart, which is present on every Windows installation (the Hyper-V
    cmdlets are not). Needs administrator rights.

    VHDX files live in $env:ICD_TESTVOL_DIR, or %LOCALAPPDATA%\IroncladDefrag\testvol by default.
    -Name picks a file there; -Path names any file.

.EXAMPLE
    pwsh -File tools/testvol/make-testvol.ps1 -Action Create -Name t4 -SizeGB 4 -ClusterSize 4096 -Letter T
.EXAMPLE
    pwsh -File tools/testvol/make-testvol.ps1 -Action Mount -Name t4 -ReadOnly
.EXAMPLE
    pwsh -File tools/testvol/make-testvol.ps1 -Action Dismount -Name t4
#>
[CmdletBinding()]
param(
    # What to do with the volume.
    [Parameter(Mandatory)]
    [ValidateSet('Create', 'Mount', 'Dismount', 'Remove', 'Info')]
    [string]$Action,

    # Base name of the VHDX in the test volume directory (without extension).
    [string]$Name = 'testvol',

    # Full VHDX path; overrides -Name.
    [string]$Path,

    # Volume size for Create (4-64 GB per plan.md; larger is allowed for scale tests).
    [ValidateRange(1, 65536)]
    [int]$SizeGB = 4,

    # NTFS cluster size in bytes for Create.
    [ValidateSet(512, 1024, 2048, 4096, 8192, 16384, 32768, 65536)]
    [int]$ClusterSize = 4096,

    # Drive letter to assign; a free one from T downwards is chosen when empty.
    [ValidatePattern('^[A-Za-z]?$')]
    [string]$Letter = '',

    # Volume label for Create.
    [string]$Label = 'ICD-TEST',

    # Mount read-only (analysis checks: proves nothing is written).
    [switch]$ReadOnly,

    # Create: replace an existing VHDX of the same name.
    [switch]$Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Default directory for test VHDX files; kept out of the repository.
function Get-TestVolDir {
    if ($env:ICD_TESTVOL_DIR) { return $env:ICD_TESTVOL_DIR }
    return Join-Path $env:LOCALAPPDATA 'IroncladDefrag\testvol'
}

# Resolves -Path / -Name to the VHDX file path.
function Resolve-VhdxPath {
    if ($Path) { return [System.IO.Path]::GetFullPath($Path) }
    return Join-Path (Get-TestVolDir) "$Name.vhdx"
}

# Stops early with a clear message: diskpart and volume access need an elevated shell.
function Assert-Admin {
    $principal = [Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'make-testvol.ps1 needs administrator rights (diskpart).'
    }
}

# Runs a diskpart script and throws with its output when it fails.
function Invoke-Diskpart([string[]]$Lines) {
    $script = New-TemporaryFile
    try {
        Set-Content -LiteralPath $script -Value $Lines -Encoding ascii
        $output = & diskpart.exe /s $script.FullName 2>&1 | Out-String
        if ($LASTEXITCODE -ne 0) {
            throw "diskpart failed (exit $LASTEXITCODE):`n$($Lines -join "`n")`n$output"
        }
        Write-Verbose $output
    }
    finally {
        Remove-Item -LiteralPath $script -Force
    }
}

# Picks a drive letter that is not in use, from T downwards (then upwards), so tests never clash
# with real drives.
function Get-FreeLetter {
    $used = [System.IO.DriveInfo]::GetDrives() | ForEach-Object { $_.Name.Substring(0, 1).ToUpper() }
    foreach ($c in [char[]]'TSRQPONMLKJIHVWY') {
        if ($used -notcontains [string]$c) { return [string]$c }
    }
    throw 'No free drive letter.'
}

# The attached disk image's partition holding the test volume, or $null when not attached.
function Get-TestPartition([string]$Vhdx) {
    $image = Get-DiskImage -ImagePath $Vhdx -ErrorAction SilentlyContinue
    if (-not $image -or -not $image.Attached) { return $null }
    $disk = $image | Get-Disk
    # GPT disks also carry a Microsoft reserved partition; the data partition is the "Basic" one.
    return $disk | Get-Partition | Where-Object { $_.Type -eq 'Basic' } | Select-Object -First 1
}

# Makes sure the attached volume has the wanted (or any free) drive letter.
function Set-TestLetter([string]$Vhdx, [string]$Wanted) {
    $partition = $null
    # The volume shows up a moment after attach.
    for ($i = 0; $i -lt 50 -and -not $partition; $i++) {
        $partition = Get-TestPartition $Vhdx
        if (-not $partition) { Start-Sleep -Milliseconds 200 }
    }
    if (-not $partition) { throw "No partition found on $Vhdx after attach." }
    $current = [string]$partition.DriveLetter
    if ($current -and $current -ne "`0" -and (-not $Wanted -or $current -eq $Wanted.ToUpper())) {
        return $current
    }
    $target = if ($Wanted) { $Wanted.ToUpper() } else { Get-FreeLetter }
    $partition | Set-Partition -NewDriveLetter $target
    return $target
}

# Summary object printed by every action, so callers can pipe it (Letter, DiskNumber).
function Get-TestVolInfo([string]$Vhdx) {
    $image = Get-DiskImage -ImagePath $Vhdx -ErrorAction SilentlyContinue
    $attached = [bool]($image -and $image.Attached)
    $partition = if ($attached) { Get-TestPartition $Vhdx } else { $null }
    $letter = if ($partition -and $partition.DriveLetter -and $partition.DriveLetter -ne "`0") { [string]$partition.DriveLetter } else { '' }
    $volume = if ($letter) { Get-Volume -DriveLetter $letter } else { $null }
    [pscustomobject]@{
        Path        = $Vhdx
        Exists      = Test-Path -LiteralPath $Vhdx
        Attached    = $attached
        DiskNumber  = if ($attached) { ($image | Get-Disk).Number } else { $null }
        Letter      = $letter
        FileSystem  = if ($volume) { $volume.FileSystem } else { '' }
        ClusterSize = if ($volume) { $volume.AllocationUnitSize } else { 0 }
        SizeBytes   = if ($volume) { $volume.Size } else { 0 }
        FreeBytes   = if ($volume) { $volume.SizeRemaining } else { 0 }
    }
}

# Creates the VHDX, partitions it (GPT, one partition), formats NTFS and assigns a letter.
function New-TestVol([string]$Vhdx) {
    if (Test-Path -LiteralPath $Vhdx) {
        if (-not $Force) { throw "$Vhdx exists; use -Force to replace it." }
        Remove-TestVol $Vhdx
    }
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Vhdx) | Out-Null
    $letter = if ($Letter) { $Letter.ToUpper() } else { Get-FreeLetter }
    Invoke-Diskpart @(
        "create vdisk file=`"$Vhdx`" maximum=$($SizeGB * 1024) type=expandable",
        "select vdisk file=`"$Vhdx`"",
        'attach vdisk',
        'convert gpt',
        'create partition primary',
        "format fs=ntfs unit=$ClusterSize quick label=`"$Label`"",
        "assign letter=$letter"
    )
}

# Attaches an existing VHDX (optionally read-only) and makes sure it has a drive letter.
function Mount-TestVol([string]$Vhdx) {
    if (-not (Test-Path -LiteralPath $Vhdx)) { throw "$Vhdx does not exist." }
    $image = Get-DiskImage -ImagePath $Vhdx
    if (-not $image.Attached) {
        $attach = if ($ReadOnly) { 'attach vdisk readonly' } else { 'attach vdisk' }
        Invoke-Diskpart @("select vdisk file=`"$Vhdx`"", $attach)
    }
    Set-TestLetter $Vhdx $Letter | Out-Null
}

# Detaches the VHDX if it is attached; the letter is released with it.
function Dismount-TestVol([string]$Vhdx) {
    $image = Get-DiskImage -ImagePath $Vhdx -ErrorAction SilentlyContinue
    if ($image -and $image.Attached) {
        Invoke-Diskpart @("select vdisk file=`"$Vhdx`"", 'detach vdisk')
    }
}

# Detaches and deletes the VHDX file.
function Remove-TestVol([string]$Vhdx) {
    if (Test-Path -LiteralPath $Vhdx) {
        Dismount-TestVol $Vhdx
        Remove-Item -LiteralPath $Vhdx -Force
    }
}

$vhdx = Resolve-VhdxPath
if ($Action -ne 'Info') { Assert-Admin }
switch ($Action) {
    'Create'   { New-TestVol $vhdx }
    'Mount'    { Mount-TestVol $vhdx }
    'Dismount' { Dismount-TestVol $vhdx }
    'Remove'   { Remove-TestVol $vhdx }
    'Info'     { }
}
Get-TestVolInfo $vhdx
