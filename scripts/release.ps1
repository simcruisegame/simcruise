<#
.SYNOPSIS
  Builds a complete Windows release in one step: Shipping package and zip, the map
  helper, the installer, SHA-256 checksums, and a smoke test of the packaged game.

.DESCRIPTION
  Follow docs\release\RELEASE-CHECKLIST.md; this script is its step 3.
  1. scripts\package.ps1 -Configuration Shipping -Zip (it builds tools\atlas.exe from
     helper\atlas.mjs when Bun is installed), under the PC-wide editor lock.
  2. Checks the zip: no .pdb, no Saved folder, the helper inside.
  3. scripts\installer.ps1: Inno Setup 6 installer and SHA256SUMS.txt.
  4. Starts the packaged game with -SimCruiseShellSelfTest and needs exit code 0.
  Ends with "RELEASE PACKAGE OK" and exit code 0, or names the step that failed.
  The files are in Saved\Package. Publishing (tag, gh release) stays a manual step.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\release.ps1
  powershell -ExecutionPolicy Bypass -File scripts\release.ps1 -NoBuild   # installer, checksums and smoke test only
#>
param(
    [switch]$NoBuild,
    [int]$LockTimeoutMinutes = 120
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'EditorLock.ps1')
$Root = Split-Path -Parent $PSScriptRoot
$Out = Join-Path $Root 'Saved\Package'
$Version = ((Get-Content (Join-Path $Root 'Config\DefaultGame.ini')) -match '^ProjectVersion=' | Select-Object -First 1).Split('=')[1]
$Zip = Join-Path $Out "SimCruise-$Version-windows-x64.zip"
# The game itself, not the SimCruise.exe launcher stub: the stub's exit code is not always
# the game's, and a Shipping game writes no log, so the self-test's exit code decides.
$Exe = Join-Path $Out 'Windows\SimCruise\Binaries\Win64\SimCruise-Win64-Shipping.exe'

if (-not $NoBuild) {
    if (-not (Enter-EditorLock -TimeoutMinutes $LockTimeoutMinutes -Why "release package of $Root")) { exit 1 }
    try {
        & powershell -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'package.ps1') -Configuration Shipping -Zip
        if ($LASTEXITCODE -ne 0) { Write-Host 'RELEASE FAILED: package'; exit 1 }
    }
    finally { Exit-EditorLock }
}

if (-not (Test-Path $Zip)) { Write-Host "RELEASE FAILED: no zip at $Zip"; exit 1 }
$Tar = Join-Path $env:SystemRoot 'System32\tar.exe'
$Listing = & $Tar -t -f $Zip
if ($Listing -match '\.pdb$') { Write-Host 'RELEASE FAILED: the zip has debug symbols'; exit 1 }
if ($Listing -match '^SimCruise/Saved/') { Write-Host 'RELEASE FAILED: the zip has a Saved folder'; exit 1 }
if (-not ($Listing -match 'SimCruise/tools/atlas\.exe$')) { Write-Host 'RELEASE FAILED: the zip has no map helper (tools\atlas.exe)'; exit 1 }

& powershell -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'installer.ps1')
if ($LASTEXITCODE -ne 0) { Write-Host 'RELEASE FAILED: installer'; exit 1 }

Write-Host 'Smoke test: the packaged game self-test'
if (-not (Enter-EditorLock -TimeoutMinutes $LockTimeoutMinutes -Why "release smoke test of $Root")) { exit 1 }
try {
    $Game = Start-Process -FilePath $Exe -ArgumentList @('-SimCruiseShellSelfTest', '-windowed', '-ResX=1280', '-ResY=720') -PassThru
    if (-not $Game.WaitForExit(15 * 60000)) { Stop-Process -Id $Game.Id -Force; Write-Host 'RELEASE FAILED: smoke test timed out'; exit 1 }
    if ($Game.ExitCode -ne 0) { Write-Host "RELEASE FAILED: smoke test exit code $($Game.ExitCode)"; exit 1 }
}
finally { Exit-EditorLock }

Get-ChildItem $Out -File -Filter "SimCruise-$Version*" | ForEach-Object { Write-Host ('{0}  {1:N1} MB' -f $_.Name, ($_.Length / 1MB)) }
Write-Host 'RELEASE PACKAGE OK'
exit 0
