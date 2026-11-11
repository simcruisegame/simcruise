<#
.SYNOPSIS
  Builds, cooks and stages a Windows build of SimCruise in Saved\Package\Windows.

.DESCRIPTION
  Saves the code-built materials first (a cooked game cannot compile them), runs
  BuildCookRun for Win64 Development, then copies in the files the game reads
  from disk rather than from its .pak: data\ (car definitions, bundled tiles and
  art), the car presentation profile and calibration overlays, and the map
  helper (tools\atlas.exe) when it is present. The result runs with
  Saved\Package\Windows\SimCruise.exe and starts on the bundled Miami Beach streets.

.PARAMETER Configuration
  Development (default) or Shipping.

.PARAMETER SkipMaterials
  Do not resave the materials (Content\*\Materials must be current).

.PARAMETER EngineDir
  The Unreal Engine 5.8 directory (default: $env:UE_ROOT, else C:\Program Files\Epic Games\UE_5.8).

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\package.ps1
#>
param(
    [ValidateSet('Development', 'Shipping')]
    [string]$Configuration = 'Development',
    [switch]$SkipMaterials,
    [string]$EngineDir = $(if ($env:UE_ROOT) { $env:UE_ROOT } else { 'C:\Program Files\Epic Games\UE_5.8' })
)
$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
$Project = Join-Path $Root 'SimCruise.uproject'
$Out = Join-Path $Root 'Saved\Package'

if (-not $SkipMaterials) {
    Write-Host 'Saving the generated materials'
    & (Join-Path $EngineDir 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe') $Project -run=SimCruiseBuildWorldMaterials -unattended -nop4 -nosplash | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "SimCruiseBuildWorldMaterials failed ($LASTEXITCODE)" }
}

Write-Host "BuildCookRun Win64 $Configuration"
& (Join-Path $EngineDir 'Engine\Build\BatchFiles\RunUAT.bat') BuildCookRun "-project=$Project" -noP4 -platform=Win64 `
    "-clientconfig=$Configuration" -build -cook -map=/Engine/Maps/Entry -stage -pak -archive "-archivedirectory=$Out" -unattended -utf8output
if ($LASTEXITCODE -ne 0) { throw "BuildCookRun failed ($LASTEXITCODE)" }

$Game = Join-Path $Out 'Windows\SimCruise'
if (-not (Test-Path $Game)) { throw "No staged game at $Game" }
Write-Host 'Copying the loose data the game reads from disk'
robocopy (Join-Path $Root 'data') (Join-Path $Game 'data') /MIR /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) { throw "Copying data failed ($LASTEXITCODE)" }
New-Item -ItemType Directory -Force (Join-Path $Game 'Content\Vehicles') | Out-Null
Copy-Item (Join-Path $Root 'Content\Vehicles\CarPresentation.json') (Join-Path $Game 'Content\Vehicles') -Force
robocopy (Join-Path $Root 'Content\Vehicle\Calibration') (Join-Path $Game 'Content\Vehicle\Calibration') /MIR /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) { throw "Copying calibration failed ($LASTEXITCODE)" }
$Atlas = Join-Path $Root 'tools\atlas.exe'
if (Test-Path $Atlas) {
    New-Item -ItemType Directory -Force (Join-Path $Game 'tools') | Out-Null
    Copy-Item $Atlas (Join-Path $Game 'tools') -Force
} else {
    Write-Warning 'tools\atlas.exe is missing: the package drives only the bundled streets'
}
$global:LASTEXITCODE = 0
Write-Host "PACKAGED: $(Join-Path $Out 'Windows\SimCruise.exe')"
