<#
.SYNOPSIS
  Measures frame times and stalls while the pre-flight robot drives fast on a real road.

.DESCRIPTION
  The same robot as scripts\preflight.ps1, but with no screenshots (they cost frame time) and at a
  set speed instead of the road's limit, on a chosen graphics preset and window size. The game
  writes one line per run:
    PREFLIGHT FRAMES road=<road> car=<car> mean=<m/s> frames=<n> p50=<ms> p99=<ms> worst=<ms> over33=<n> over50=<n> over100=<n>
  and one "stall <ms> at <m>" line for each frame over 50 ms. This script prints them and writes
  Saved\FrameCheck\<time>\report.md. It does not count as a pre-flight (no index entry).

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\framecheck.ps1 -Roads nebraska,big-sur -Kmh 130 -Graphics ultra
#>
param(
    [string[]]$Roads = @('nebraska', 'big-sur', 'dragon'),
    [string]$Car = 'mustang65',
    [int]$Kmh = 130,
    [int]$Seconds = 60,
    [string]$Graphics = 'ultra',
    [int]$ResX = 1920,
    [int]$ResY = 1080,
    [string]$Utc = '2026-11-12T17:00:00Z',
    [string]$ExtraArgs = '',
    # A packaged game (scripts\package.ps1) instead of the editor: the editor runs 2-3 times slower.
    [string]$Exe = '',
    [string]$EngineDir = $(if ($env:UE_ROOT) { $env:UE_ROOT } else { 'C:\Program Files\Epic Games\UE_5.8' }),
    [int]$TimeoutMinutes = 15,
    [int]$LockTimeoutMinutes = 120
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'EditorLock.ps1')
$Root = Split-Path -Parent $PSScriptRoot
$Project = Join-Path $Root 'SimCruise.uproject'
$Editor = Join-Path $EngineDir 'Engine\Binaries\Win64\UnrealEditor.exe'
if ($Exe) { $Editor = (Resolve-Path $Exe).Path }
if (-not (Test-Path $Editor)) { Write-Host "No Unreal Editor at $Editor (set -EngineDir or UE_ROOT)."; exit 2 }
$Roads = @($Roads | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
# The same starts as scripts\preflight.ps1.
$Defs = @{
    'ocean-drive' = @('-SimCruiseBundledStart')
    'dragon'      = @('-SimCruiseStart=35.4792763,-83.9327265', '"-SimCruiseStartRoad=Calderwood Highway"')
    'nebraska'    = @('-SimCruiseStart=40.56724,-96.5909', '"-SimCruiseStartRoad=Princeton Road"')
    'big-sur'     = @('-SimCruiseStart=36.363197,-121.900041', '"-SimCruiseStartRoad=Cabrillo Highway"')
}
$Stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$RunDir = Join-Path $Root "Saved\FrameCheck\$Stamp"
New-Item -ItemType Directory -Force $RunDir | Out-Null
$Md = @("# Frame check $Stamp", '', "$(if ($Exe) { "Packaged $Exe. " })Commit ``$(git -C $Root rev-parse --short HEAD)``, $Car at $Kmh km/h, $Graphics, ${ResX}x$ResY, $Seconds s, no screenshots. $ExtraArgs", '')
foreach ($Road in $Roads) {
    if (-not $Defs.ContainsKey($Road)) { Write-Host "Unknown road $Road"; continue }
    $Out = Join-Path $RunDir $Road
    New-Item -ItemType Directory -Force $Out | Out-Null
    $Log = Join-Path $Out 'game.log'
    $Args = @("`"$Project`"", '-game', '-SimCruiseWorld', '-SimCruiseNoLive', '-SimCruiseNoNeural', "-SimCruiseGraphics=$Graphics", "-SimCruiseUtc=$Utc",
        "-SimCruiseCar=$Car", '-SimCruisePreflight', '-SimCruisePreflightNoFrames', "-SimCruisePreflightKmh=$Kmh", "-SimCruisePreflightRoad=$Road",
        "-SimCruisePreflightSeconds=$Seconds", "`"-SimCruisePreflightOut=$Out`"", '-NoLiveCoding', '-windowed', "-ResX=$ResX", "-ResY=$ResY",
        '-nosplash', '-unattended', '-log', "`"-abslog=$Log`"") + $Defs[$Road]
    if ($Exe) { $Args = $Args[2..($Args.Count - 1)] + '-SimCruiseWorld' }
    if ($ExtraArgs) { $Args += $ExtraArgs }
    if (-not (Enter-EditorLock -TimeoutMinutes $LockTimeoutMinutes -Why "frame check $Road on $Root")) { exit 1 }
    try {
        Write-Host "Frame check: $Road"
        $P = Start-Process -FilePath $Editor -ArgumentList ($Args -join ' ') -PassThru
        if (-not $P.WaitForExit($TimeoutMinutes * 60 * 1000)) { Stop-ProcessTree -ProcessId $P.Id; Write-Host '  timed out' }
    }
    finally { Exit-EditorLock }
    $Lines = if (Test-Path $Log) { @(Select-String -Path $Log -Pattern 'PREFLIGHT FRAMES .*|Pre-flight: stall .*' | ForEach-Object { $_.Matches[0].Value }) } else { @('no log') }
    $Lines | ForEach-Object { Write-Host "  $_" }
    $Md += "## $Road"; $Md += ''; $Md += ($Lines | ForEach-Object { "    $_" }); $Md += ''
}
$Md | Set-Content -Encoding utf8 (Join-Path $RunDir 'report.md')
Write-Host "Report: $(Join-Path $RunDir 'report.md')"
