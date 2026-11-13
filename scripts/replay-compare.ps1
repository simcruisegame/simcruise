<#
.SYNOPSIS
  Replays one recording of the driver's controls on two builds or two cars and prints the differences.

.DESCRIPTION
  Each run is a game (-game, started at the tape's recorded start: the place a tape names, else
  the bundled Ocean Drive start for an older tape) started with
  scripts\editor.ps1 -TimeoutMinutes from its checkout, so it takes the PC-wide editor lock
  and is stopped if it hangs. The game puts the tape's car (or -CarA / -CarB) at the tape's
  start at rest, or on the drive robot's test pad with -Pad, feeds the recorded controls
  through the same shaper and vehicle as a pad, writes speed, yaw rate, slip and lap time to a
  CSV and logs one REPLAY line. This script compares the two REPLAY lines and the two CSVs
  (the largest speed, yaw-rate and slip differences at the same time).

  Record a tape in any game with -SimCruiseRecordInput or F9 (Saved\DriveTest\input-<time>.csv).

.PARAMETER Tape
  The recording (CSV from -SimCruiseRecordInput or F9).
.PARAMETER RootA
  Checkout for run A (default: this checkout). Its editor binaries must be built.
.PARAMETER RootB
  Checkout for run B (default: RootA).
.PARAMETER CarA
  Car for run A (default: the tape's car).
.PARAMETER CarB
  Car for run B (default: CarA).
.PARAMETER Pad
  Replay on the drive robot's flat test pad instead of at the recorded start.
.PARAMETER Traffic
  Keep the moving traffic. It is off by default: the replay drives blind, so a traffic car in
  the way of one run and not the other makes the runs differ.
.PARAMETER TimeoutMinutes
  Stop a run that has not quit after this many minutes (default 15).

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\replay-compare.ps1 -Tape Saved\DriveTest\input-20261113-101500.csv -CarB nb2001_aaron
  powershell -ExecutionPolicy Bypass -File scripts\replay-compare.ps1 -Tape lap.csv -RootB D:\sc\lane2 -Pad
#>
param(
    [Parameter(Mandatory = $true)][string]$Tape,
    [string]$RootA = '',
    [string]$RootB = '',
    [string]$CarA = '',
    [string]$CarB = '',
    [switch]$Pad,
    # Keep the moving traffic (off by default: a replay does not react, so a car ahead makes runs differ).
    [switch]$Traffic,
    [int]$TimeoutMinutes = 15
)

$ErrorActionPreference = 'Stop'
$Here = Split-Path -Parent $PSScriptRoot
if (-not $RootA) { $RootA = $Here }
if (-not $RootB) { $RootB = $RootA }
if (-not $CarB) { $CarB = $CarA }
$Tape = (Resolve-Path $Tape).Path
# Start the game where the tape was recorded: its '# lonlat=' line (newer tapes also name the place).
# An older tape without it starts at the bundled Ocean Drive start, as before.
$Start = @('-SimCruiseBundledStart')
$LonLat = Select-String -Path $Tape -Pattern '^# lonlat=([-0-9.]+),([-0-9.]+)' | Select-Object -First 1
$HasPlace = [bool](Select-String -Path $Tape -Pattern '^# place=' -Quiet)
if ($LonLat -and $HasPlace) {
    $Start = @("-SimCruiseStart=$($LonLat.Matches[0].Groups[2].Value),$($LonLat.Matches[0].Groups[1].Value)")
    Write-Host "Tape start: $($LonLat.Matches[0].Groups[2].Value), $($LonLat.Matches[0].Groups[1].Value)"
} else {
    Write-Host 'WARNING: the tape names no place (an older recording); it replays at the bundled Ocean Drive start.'
}
$Out = Join-Path $Here ("Saved\DriveTest\compare-" + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Force $Out | Out-Null

function Invoke-Replay([string]$Name, [string]$Root, [string]$Car) {
    $Log = Join-Path $Out "$Name.log"
    $Csv = (Join-Path $Out "$Name.csv") -replace '\\', '/'
    $Args = @('-game', '-SimCruiseWorld') + $Start + @('-SimCruiseNoLive', '-SimCruiseNoNeural', '-SimCruiseRealCar',
        "-SimCruiseReplayInput=$($Tape -replace '\\', '/')", "-SimCruiseReplayOut=$Csv", '-windowed', '-ResX=1280', '-ResY=720', '-log', "-abslog=$Log")
    if ($Car) { $Args += "-SimCruiseReplayCar=$Car"; $Args += "-SimCruiseCar=$Car" }
    if ($Pad) { $Args += '-SimCruiseReplayPad' }
    if (-not $Traffic) { $Args += '-DPCVars=simcruise.Traffic.Cars=0' }
    $Commit = (git -C $Root rev-parse --short HEAD).Trim()
    Write-Host "Replay $Name on $Root ($Commit)$(if ($Car) { ", car $Car" })"
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $Root 'scripts\editor.ps1') -TimeoutMinutes $TimeoutMinutes @Args | Out-Null
    if ($LASTEXITCODE -eq 4) { throw "Replay $Name timed out after $TimeoutMinutes min (log: $Log)" }
    $Line = if (Test-Path $Log) { Select-String -Path $Log -Pattern 'REPLAY (FAILED .*|car=.*)' | Select-Object -Last 1 } else { $null }
    if (-not $Line) { throw "Replay $Name wrote no REPLAY line (log: $Log)" }
    $Text = $Line.Matches[0].Value
    if ($Text -like 'REPLAY FAILED*') { throw "Replay ${Name}: $Text" }
    $Values = [ordered]@{}
    foreach ($M in [regex]::Matches($Text, '(\w+)=(\S+)')) { $Values[$M.Groups[1].Value] = $M.Groups[2].Value }
    [pscustomobject]@{ Name = $Name; Root = $Root; Commit = $Commit; Values = $Values; Csv = ($Csv -replace '/', '\'); Line = $Text }
}

$A = Invoke-Replay 'A' $RootA $CarA
$B = Invoke-Replay 'B' $RootB $CarB

Write-Host ''
Write-Host ("{0,-18} {1,14} {2,14} {3,12}" -f 'measure', "A ($($A.Values['car']))", "B ($($B.Values['car']))", 'B - A')
foreach ($Key in @('seconds', 'distance_m', 'lap_time_s', 'max_kph', 'mean_kph', 'max_yaw_dps', 'max_beta_deg', 'max_rear_slip_deg')) {
    $Va = [double]$A.Values[$Key]; $Vb = [double]$B.Values[$Key]
    Write-Host ("{0,-18} {1,14:N2} {2,14:N2} {3,12:N2}" -f $Key, $Va, $Vb, ($Vb - $Va))
}

# Sample by sample at the same tape time (the nearest row of B to each row of A).
$RowsA = @(Import-Csv $A.Csv)
$RowsB = @(Import-Csv $B.Csv)
if ($RowsA.Count -and $RowsB.Count) {
    $J = 0
    $Worst = @{ speed_kph = @(0.0, 0.0); yaw_rate_dps = @(0.0, 0.0); beta_deg = @(0.0, 0.0); rear_slip_deg = @(0.0, 0.0) }
    foreach ($Ra in $RowsA) {
        $T = [double]$Ra.t
        while ($J -lt $RowsB.Count - 1 -and [math]::Abs([double]$RowsB[$J + 1].t - $T) -le [math]::Abs([double]$RowsB[$J].t - $T)) { $J++ }
        $Rb = $RowsB[$J]
        foreach ($Col in @($Worst.Keys)) {
            $D = [math]::Abs([double]$Rb.$Col - [double]$Ra.$Col)
            if ($D -gt $Worst[$Col][0]) { $Worst[$Col] = @($D, $T) }
        }
    }
    Write-Host ''
    foreach ($Col in @('speed_kph', 'yaw_rate_dps', 'beta_deg', 'rear_slip_deg')) {
        Write-Host ("largest {0,-14} difference {1,8:N2} at t = {2:N2} s" -f $Col, $Worst[$Col][0], $Worst[$Col][1])
    }
}
Write-Host ''
Write-Host "A: $($A.Line)"
Write-Host "B: $($B.Line)"
Write-Host "Logs and CSVs: $Out"
exit 0
