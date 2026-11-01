<#
.SYNOPSIS
    Captures raw MFT records (update sequence fixups still applied on disk) from a test volume as test data for the
    MFT record parser (T060).

.DESCRIPTION
    Reads $MFT records straight from the volume device, the way the MFT scanner does, and writes each as
    <label>.bin. Extension records of a captured file (records whose base reference names it) are written as
    <label>-ext<N>.bin. Only for ICD-TEST volumes made by make-testvol.ps1 + gen-scenario: the records hold
    file names. The script reads the first $MFT extent only, which on a fresh test volume holds every record;
    each captured record must carry its own record number, otherwise the script stops.

    Needs administrator rights.

.EXAMPLE
    pwsh -File tools/testvol/capture-mft-records.ps1 -Drive T -OutDir tests/data/ntfs `
        -Records 'mft=0','root=5','resident=40','fragmented=257'
#>
[CmdletBinding()]
param(
    # Drive letter of the mounted test volume.
    [Parameter(Mandatory)] [string]$Drive,

    # label=record pairs to capture.
    [Parameter(Mandatory)] [string[]]$Records,

    # Where the .bin files go.
    [Parameter(Mandatory)] [string]$OutDir,

    # Records searched for extension records of the captured files.
    [int]$ScanRecords = 16384
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$Records = $Records -split ',' # "pwsh -File" passes a comma list as one string.
$label = (Get-Volume -DriveLetter $Drive).FileSystemLabel
if (-not $label.StartsWith('ICD-TEST')) { throw "$Drive`: is not an ICD-TEST volume" }

# "Mft Start Lcn :  0x0000000000040000" and friends from fsutil.
function Get-NtfsValue([string]$Name) {
    $line = fsutil fsinfo ntfsinfo "${Drive}:" | Select-String "^$Name\s*:" | Select-Object -First 1
    $text = (($line.Line -split ':', 2)[1].Trim() -split '\s+')[0]
    if ($text.StartsWith('0x')) { return [Convert]::ToUInt64($text.Substring(2), 16) }
    return [uint64]($text.Replace(',', ''))
}

$cluster = Get-NtfsValue 'Bytes Per Cluster'
$recordSize = Get-NtfsValue 'Bytes Per FileRecord Segment'
$mftStart = (Get-NtfsValue 'Mft Start Lcn') * $cluster
New-Item -ItemType Directory -Force $OutDir | Out-Null

$volume = [IO.File]::Open("\\.\${Drive}:", 'Open', 'Read', 'ReadWrite')
try {
    # Raw record n of the first $MFT extent.
    function Read-Record([uint64]$Number) {
        $bytes = New-Object byte[] $recordSize
        $volume.Position = $mftStart + $Number * $recordSize
        if ($volume.Read($bytes, 0, $recordSize) -ne $recordSize) { throw "short read of record $Number" }
        return , $bytes
    }

    # Records searched for extension records: fsutil prints the MFT length rounded ("3.25 MB"), so a fixed range of
    # the first extent is searched instead, ending early at a record that is not where the extent should hold it.
    $valid = [uint64]$ScanRecords
    $bases = @{}
    foreach ($pair in $Records) {
        $name, $number = $pair -split '=', 2
        $raw = Read-Record ([uint64]$number)
        if ([BitConverter]::ToUInt32($raw, 0x2C) -ne [uint32]$number) { throw "record $number is not in the first MFT extent" }
        [IO.File]::WriteAllBytes((Join-Path $OutDir "$name.bin"), $raw)
        $bases[[uint64]$number] = $name
        Write-Host "record $number -> $name.bin"
    }
    $count = @{}
    for ($n = [uint64]16; $n -lt $valid; $n++) {
        $raw = Read-Record $n
        $isFile = [BitConverter]::ToUInt32($raw, 0) -eq 0x454C4946
        if ($isFile -and [BitConverter]::ToUInt32($raw, 0x2C) -ne [uint32]$n) { break } # Past the first extent.
        $base = [uint64]([BitConverter]::ToUInt64($raw, 0x20) -band 0x0000FFFFFFFFFFFF)
        if ($isFile -and $base -ne 0 -and $bases.ContainsKey($base)) {
            $name = $bases[$base]; $count[$name] = 1 + ($count[$name] ?? 0)
            [IO.File]::WriteAllBytes((Join-Path $OutDir "$name-ext$($count[$name]).bin"), $raw)
            Write-Host "record $n -> $name-ext$($count[$name]).bin (extension of $base)"
        }
    }
}
finally { $volume.Dispose() }
