<#
.SYNOPSIS
  Builds the Windows installer from the Shipping package and writes SHA-256 checksums.

.DESCRIPTION
  Run scripts\package.ps1 -Configuration Shipping -Zip first. This script reads the
  package in Saved\Package\Windows, compiles packaging\windows\simcruise.iss with
  Inno Setup 6 (ISCC.exe; install it with
  `winget install --id JRSoftware.InnoSetup -e --scope user`) into
  Saved\Package\SimCruise-<ProjectVersion>-windows-x64-setup.exe, and writes
  Saved\Package\SimCruise-<ProjectVersion>-SHA256SUMS.txt for the installer and the zip.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\installer.ps1
#>
param(
    [string]$Iscc = ''
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
$Out = Join-Path $Root 'Saved\Package'
$Source = Join-Path $Out 'Windows'
if (-not (Test-Path (Join-Path $Source 'SimCruise.exe'))) { throw "No package at $Source (run scripts\package.ps1 -Configuration Shipping -Zip)" }
if (-not $Iscc) {
    $Iscc = @(
        (Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe'),
        (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe'),
        (Join-Path $env:ProgramFiles 'Inno Setup 6\ISCC.exe')
    ) | Where-Object { Test-Path $_ } | Select-Object -First 1
}
if (-not $Iscc) { throw 'Inno Setup 6 is not installed (winget install --id JRSoftware.InnoSetup -e --scope user)' }

$Version = ((Get-Content (Join-Path $Root 'Config\DefaultGame.ini')) -match '^ProjectVersion=' | Select-Object -First 1).Split('=')[1]
# Windows file versions are numeric: 2.0.0-preview.1 -> 2.0.0.
$Numeric = ($Version -split '-')[0]
$Script = Join-Path $Root 'packaging\windows\simcruise.iss'
Write-Host "Inno Setup: SimCruise $Version from $Source"
& $Iscc "/DAppVersion=$Version" "/DNumericVersion=$Numeric" "/DSourceDir=$Source" "/DOutputDir=$Out" /Qp $Script
if ($LASTEXITCODE -ne 0) { throw "ISCC failed ($LASTEXITCODE)" }
$Setup = Join-Path $Out "SimCruise-$Version-windows-x64-setup.exe"
if (-not (Test-Path $Setup)) { throw "No installer at $Setup" }

$Sums = Join-Path $Out "SimCruise-$Version-SHA256SUMS.txt"
$Files = @($Setup, (Join-Path $Out "SimCruise-$Version-windows-x64.zip")) | Where-Object { Test-Path $_ }
$Lines = foreach ($F in $Files) { '{0}  {1}' -f (Get-FileHash -Algorithm SHA256 $F).Hash.ToLower(), (Split-Path -Leaf $F) }
Set-Content -Path $Sums -Value $Lines -Encoding ascii
foreach ($F in $Files) { Write-Host ('{0}  {1:N0} MB' -f (Split-Path -Leaf $F), ((Get-Item $F).Length / 1MB)) }
Write-Host "CHECKSUMS: $Sums"
