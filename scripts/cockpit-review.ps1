<#
.SYNOPSIS
  The cockpit steer test for every car: the gauges stay where they are while the steering wheel
  turns, and the wheel turns about its column.

.DESCRIPTION
  For each car it starts one game (editor binaries, -game, Ultra, 1280x720, driver's seat, the
  bundled Ocean Drive start, offline, the one-editor lock) with -SimCruiseCockpitTest. With the car
  at rest, the stick goes to the centre, half and full lock both ways, each held 2 s. At each pose
  the game reads the transforms of the steering wheel, the dial faces, the needles and the body,
  and takes a frame as the driver sees it and one with the wheel hidden. It FAILs when the gauges
  or the dash move or turn with the steering, when a gauge hangs on the wheel or sits in the
  wheel's mesh, when the wheel turns off its column, its hub moves or its rim is tilted on the
  column, when the rim angle at full lock is not the car data's, or when the cluster's pixels
  change between the centre and full lock with the wheel hidden.

  Each car's folder Saved\Review\<yyyyMMdd-HHmm>-<commit>\cockpit-<car>\ holds cockpit\<pose>.png,
  cockpit\<pose>-nowheel.png, cockpit\diff-full-left.png and diff-full-right.png (the cluster's
  change, bright where it changed), cockpit\cockpit.json, the game log, the command line, the GPU,
  a crash's dump and review.md. The script prints one COCKPIT line per car and exits 1 when a car
  fails. Look at the frames before you call a fix done.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\cockpit-review.ps1
  powershell -ExecutionPolicy Bypass -File scripts\cockpit-review.ps1 -Cars nb2001_lowered,mustang65,meridian_z
#>
param(
    # Show the game's window (default: off screen, -RenderOffScreen, nothing on the desktop).
    [switch]$ShowWindow,
    [string[]]$Cars = @(),
    [string]$Graphics = 'ultra',
    [string]$EngineDir = $(if ($env:UE_ROOT) { $env:UE_ROOT } else { 'C:\Program Files\Epic Games\UE_5.8' }),
    [int]$TimeoutMinutes = 10,
    [int]$LockTimeoutMinutes = 120
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'EditorLock.ps1')
. (Join-Path $PSScriptRoot 'ReviewLib.ps1')
$Root = Split-Path -Parent $PSScriptRoot
$Project = Join-Path $Root 'SimCruise.uproject'
$Editor = Join-Path $EngineDir 'Engine\Binaries\Win64\UnrealEditor.exe'
if (-not (Test-Path $Editor)) { Write-Host "No Unreal Editor at $Editor (set -EngineDir or UE_ROOT)."; exit 2 }
# "powershell -File" hands "a,b" over as one string.
$Cars = @($Cars | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
if (-not $Cars.Count) { $Cars = @(Get-ChildItem (Join-Path $Root 'data\cars') -Filter '*.json' | ForEach-Object { $_.BaseName }) }
$env:UE_SKIP_UBT_SDK_SETUP = '1'
$OutRoot = Get-ReviewRoot $Root
$Failed = @()

foreach ($Car in $Cars) {
    $RunDir = Join-Path $OutRoot "cockpit-$Car"
    New-Item -ItemType Directory -Force $RunDir | Out-Null
    $Log = Join-Path $RunDir 'game.log'
    $GameArgs = @("`"$Project`"", '-game', '-SimCruiseWorld', '-SimCruiseBundledStart', '-SimCruiseNoLive', '-SimCruiseNoNeural', "-SimCruiseGraphics=$Graphics",
        '-SimCruiseUtc=2026-11-12T17:00:00Z', "-SimCruiseCar=$Car", '-SimCruiseCockpitTest', "`"-SimCruiseCockpitOut=$(Join-Path $RunDir 'cockpit')`"",
        '-DPCVars=simcruise.Traffic.Cars=0', '-NoLiveCoding', '-nosplash', '-unattended', '-log', "`"-abslog=$Log`"") + @(Get-TestWindowArgs $ShowWindow) + @(Get-NativeResolution)
    if (-not (Enter-EditorLock -GpuTest -TimeoutMinutes $LockTimeoutMinutes -Why "cockpit test $Car on $Root")) { exit 1 }
    $Started = Get-Date; $TimedOut = $false; $ExitCode = -1
    try {
        $P = Start-Process -FilePath $Editor -ArgumentList ($GameArgs -join ' ') -PassThru
        if (-not $P.WaitForExit($TimeoutMinutes * 60 * 1000)) { Stop-ProcessTree -ProcessId $P.Id; $TimedOut = $true } else { $ExitCode = $P.ExitCode }
    }
    finally { Exit-EditorLock }
    $Evidence = Save-ReviewEvidence -RunDir $RunDir -Root $Root -Log $Log -CommandLine ("`"$Editor`" " + ($GameArgs -join ' ')) -Started $Started -TimedOut $TimedOut -ExitCode $ExitCode
    $Line = if (Test-Path $Log) { Select-String -Path $Log -Pattern 'COCKPIT (PASS|FAIL) .*' | Select-Object -Last 1 } else { $null }
    $Result = if ($Line) { $Line.Matches[0].Groups[1].Value } else { 'FAIL' }
    if ($Evidence.Crashed -or $Evidence.Hung -or -not $Line) { $Result = 'FAIL' }
    Write-ReviewSheet -RunDir $RunDir -Kind 'cockpit' -Title "cockpit steer test $Car" -Result $Result -Reasons $(if ($Line -and $Line.Matches[0].Value -match 'reasons=(\S+)') { $Matches[1] } else { 'no COCKPIT line' }) -Evidence $Evidence
    $Text = if ($Line) { $Line.Matches[0].Value } else { "COCKPIT FAIL car=$Car reasons=no-COCKPIT-line$(if ($Evidence.Crashed) { ',crash' })$(if ($Evidence.Hung) { ',hang' })" }
    Write-Host $Text
    if ($Result -ne 'PASS') { $Failed += $Car }
}
Write-Host "Frames and review sheets: $OutRoot\cockpit-<car>\"
if ($Failed.Count) { Write-Host "COCKPIT TEST FAILED: $($Failed -join ', ')"; exit 1 }
Write-Host 'COCKPIT TEST PASSED'
exit 0
