# Generate-WinRT.ps1
# Automatically runs cppwinrt.exe with all relevant metadata inputs.

# --- Configuration ---
$CppWinRtExe = "C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\cppwinrt.exe"
$WindowsSDKUnion = "C:\Program Files (x86)\Windows Kits\10\UnionMetadata"
$WindowsSDKContracts = @(
    "C:\Program Files (x86)\Windows Kits\10\References\10.0.26100.0\Windows.Foundation.UniversalApiContract\19.0.0.0",
    "C:\Program Files (x86)\Windows Kits\10\References\10.0.26100.0\Windows.Foundation.FoundationContract\4.0.0.0"
)
$ExternalRoot = "external"
$OutputDir = "generated"

# --- Discover all .winmd locations under external/ ---
Write-Host "Scanning $ExternalRoot for .winmd metadata..."
$winmdDirs = Get-ChildItem -Path $ExternalRoot -Recurse -Filter *.winmd |
    ForEach-Object { $_.Directory.FullName } |
    Sort-Object -Unique

if ($winmdDirs.Count -eq 0) {
    Write-Host "No .winmd files found under $ExternalRoot. Exiting."
    exit 1
}

Write-Host "Found $($winmdDirs.Count) metadata directories:`n$($winmdDirs -join "`n")`n"

# --- Construct input arguments ---
# --- SDK paths ---
$SDKVersion = "10.0.26100.0"
$WindowsReferencesRoot = "C:\Program Files (x86)\Windows Kits\10\References\$SDKVersion"

# Add every subdirectory that contains .winmd
$WindowsContracts = Get-ChildItem -Path $WindowsReferencesRoot -Recurse -Filter *.winmd |
    ForEach-Object { $_.Directory.FullName } |
    Sort-Object -Unique

# Add to input arguments
$inputArgs = @("-input", $WindowsSDKUnion)
foreach ($contractDir in $WindowsContracts) {
    $inputArgs += @("-input", $contractDir)
}

foreach ($dir in $winmdDirs) {
    $inputArgs += @("-input", $dir)
}

# Ensure output directory exists
if (-not (Test-Path $OutputDir)) {
    New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
}

# Add output argument
$inputArgs += @("-output", $OutputDir)

# --- Show command line ---
$pretty = "`"$CppWinRtExe`" " + ($inputArgs | ForEach-Object { "`"$_`"" }) -join ' '
Write-Host "Running cppwinrt with:`n$pretty`n"

# --- Run cppwinrt safely with proper quoting ---
& $CppWinRtExe @inputArgs
$exitCode = $LASTEXITCODE

if ($LASTEXITCODE -eq 0) {
    Write-Host "`n+++ cppwinrt completed successfully. Headers generated in '$OutputDir\winrt\'."
} else {
    Write-Host "`nxxx cppwinrt failed with exit code $LASTEXITCODE"
}
