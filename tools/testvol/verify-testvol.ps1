<#
.SYNOPSIS
    Hashes every file of a test volume and verifies it later, plus chkdsk (plan.md section 12).

.DESCRIPTION
    Hash   writes a manifest: every file and directory (relative path, size, attributes, SHA-256 of the
           main stream and of each alternate data stream; reparse points record their target instead of
           being followed).
    Verify hashes again, compares with the manifest and runs "chkdsk <drive> /scan". Exit code 0 only
           when both are clean.
    Chkdsk runs only the chkdsk part.

    Moves must never change file contents, so a manifest taken before a run must match after it.
    Needs administrator rights for chkdsk and for files only administrators can read.

.EXAMPLE
    pwsh -File tools/testvol/verify-testvol.ps1 -Action Hash -Drive T: -Manifest before.json
    pwsh -File tools/testvol/verify-testvol.ps1 -Action Verify -Drive T: -Manifest before.json
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidateSet('Hash', 'Verify', 'Chkdsk')]
    [string]$Action,

    # Volume to check, as "T:" or "T".
    [Parameter(Mandatory)]
    [string]$Drive,

    # Manifest file written by Hash and read by Verify.
    [string]$Manifest = '',

    # Verify: skip chkdsk (for read-only mounts or quick checks).
    [switch]$NoChkdsk,

    # Parallel hashing threads.
    [int]$ThrottleLimit = 8
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = $Drive.Substring(0, 1).ToUpper() + ':\'

# NTFS keeps these per-volume system folders; they change on their own and are not test data.
$Skipped = @('System Volume Information', '$RECYCLE.BIN')

# Every file system entry under the root except the system folders. Reparse points are listed but
# never entered (Get-ChildItem in PowerShell 7 does not follow links without -FollowSymlink).
function Get-Entries {
    Get-ChildItem -LiteralPath $root -Force | Where-Object { $Skipped -notcontains $_.Name } | ForEach-Object {
        $_
        if ($_.PSIsContainer -and -not ($_.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            Get-ChildItem -LiteralPath $_.FullName -Force -Recurse
        }
    }
}

# Builds one manifest row per entry, hashing in parallel. Entries go to the runspaces in batches:
# ForEach-Object -Parallel costs milliseconds per input, which dominated volumes with many small files.
function Get-ManifestRows {
    $rootLength = $root.Length
    $entries = @(Get-Entries)
    $batches = for ($i = 0; $i -lt $entries.Count; $i += 500) { , $entries[$i..([math]::Min($i + 499, $entries.Count - 1))] }
    $batches | ForEach-Object -ThrottleLimit $ThrottleLimit -Parallel {
        # SHA-256 of one stream ("file" or "file:stream"), read with .NET so ADS paths work.
        function Get-StreamHash([string]$StreamPath) {
            $stream = [System.IO.File]::Open($StreamPath, 'Open', 'Read', 'ReadWrite')
            try { return [Convert]::ToHexString([System.Security.Cryptography.SHA256]::HashData($stream)) }
            finally { $stream.Dispose() }
        }
        foreach ($item in $_) {
            $isLink = [bool]($item.Attributes -band [IO.FileAttributes]::ReparsePoint)
            $row = [ordered]@{
                Path       = $item.FullName.Substring($using:rootLength)
                Directory  = $item.PSIsContainer
                Size       = if ($item.PSIsContainer) { 0 } else { $item.Length }
                Attributes = [string]($item.Attributes -band -bnot [IO.FileAttributes]::Archive)
                Link       = if ($isLink) { [string]$item.LinkTarget } else { '' }
                Hash       = ''
                Streams    = ''
            }
            if (-not $item.PSIsContainer -and -not $isLink) { $row.Hash = Get-StreamHash $item.FullName }
            if (-not $isLink) {
                # Alternate data streams as "name=hash" pairs, sorted, so one string compares them all.
                # The \\?\ prefix lets Get-Item reach paths over 260 characters.
                $row.Streams = (Get-Item -LiteralPath "\\?\$($item.FullName)" -Stream * -Force -ErrorAction SilentlyContinue |
                    Where-Object { $_.Stream -ne ':$DATA' -and $_.Stream -ne '' } |
                    Sort-Object Stream | ForEach-Object {
                        "$($_.Stream)=$(Get-StreamHash "$($item.FullName):$($_.Stream)")"
                    }) -join ','
            }
            [pscustomobject]$row
        }
    } | Sort-Object Path
}

# Rows as one comparable line each, so differences show as added/removed lines.
function ConvertTo-Lines($Rows) {
    foreach ($r in $Rows) {
        "$($r.Path)|$($r.Directory)|$($r.Size)|$($r.Attributes)|$($r.Link)|$($r.Hash)|$($r.Streams)"
    }
}

# Writes the manifest of the volume as JSON.
function Save-Manifest {
    if (-not $Manifest) { throw '-Manifest is required.' }
    $rows = @(Get-ManifestRows)
    $doc = [ordered]@{ Drive = $root; Created = (Get-Date).ToString('o'); Entries = $rows }
    $doc | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $Manifest -Encoding utf8
    $files = @($rows | Where-Object { -not $_.Directory }).Count
    $bytes = [uint64]0
    foreach ($r in $rows) { $bytes += [uint64]$r.Size }
    Write-Host "Hashed $files files, $($rows.Count - $files) directories, $bytes bytes -> $Manifest"
}

# Compares the volume with the manifest; returns the number of differences.
function Compare-Manifest {
    if (-not $Manifest) { throw '-Manifest is required.' }
    $saved = Get-Content -LiteralPath $Manifest -Raw | ConvertFrom-Json
    $expected = @(ConvertTo-Lines $saved.Entries)
    $actual = @(ConvertTo-Lines @(Get-ManifestRows))
    $diff = @(Compare-Object -ReferenceObject $expected -DifferenceObject $actual)
    foreach ($d in $diff | Select-Object -First 40) {
        $side = if ($d.SideIndicator -eq '<=') { 'manifest only' } else { 'volume only  ' }
        Write-Host "  $side $($d.InputObject)"
    }
    Write-Host "Compared $($expected.Count) entries: $($diff.Count) differences."
    return $diff.Count
}

# Runs chkdsk's online scan; returns its exit code (0 = no problems found).
function Invoke-Chkdsk {
    $output = & chkdsk.exe $root.TrimEnd('\') /scan 2>&1 | Out-String
    $code = $LASTEXITCODE
    Write-Verbose $output
    Write-Host "chkdsk $($root.TrimEnd('\')) /scan: exit $code"
    if ($code -ne 0) { Write-Host $output }
    return $code
}

switch ($Action) {
    'Hash' { Save-Manifest; exit 0 }
    'Chkdsk' { exit (Invoke-Chkdsk) }
    'Verify' {
        $differences = Compare-Manifest
        $chk = if ($NoChkdsk) { 0 } else { Invoke-Chkdsk }
        if ($differences -ne 0 -or $chk -ne 0) { exit 1 }
        exit 0
    }
}
