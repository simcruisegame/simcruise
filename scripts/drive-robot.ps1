<#
.SYNOPSIS
  The scripted gamepad driver as a regression robot: drives the limit manoeuvres on the
  flat test pad for each car and fails if a car spins where a real one stays in control.

.DESCRIPTION
  For each car (default nb2001, nb2001_aaron, mustang65 and meridian_z) it builds nothing, takes the PC-wide editor
  lock (scripts\editor.ps1), runs -SimCruiseDriveTest -SimCruiseDriveTestPad, and reads the
  DRIVETEST summary lines (peak rear-axle slip and yaw rate per manoeuvre). Each run is
  appended to Saved\DriveTest\history.csv with the commit, so a change in feel shows as a
  change in numbers.

  Manoeuvres a real car drives through in control (part-trigger junction turns, a turning
  launch at 70 % trigger, a 50 km/h circle with a lift, a full-stick 50 km/h turn-in and an
  80 km/h lane change) fail the run if the rear axle slips past 15 deg. Full stick with full
  trigger in 1st or 2nd gear spins a real NB too; those are recorded, not judged.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\drive-robot.ps1
  powershell -ExecutionPolicy Bypass -File scripts\drive-robot.ps1 -Cars nb2001 -LowFps
#>
param(
    # Show the game's window (default: off screen, -RenderOffScreen, nothing on the desktop).
    [switch]$ShowWindow,
    [string[]]$Cars = @('nb2001', 'nb2001_aaron', 'mustang65', 'meridian_z'),
    # Cap the frame rate at 15 fps as well, to catch frame-rate dependence.
    [switch]$LowFps
)

$ErrorActionPreference = 'Stop'
# powershell -File passes "a,b,c" as one string.
$Cars = @($Cars | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
$Root = Split-Path -Parent $PSScriptRoot
$Out = Join-Path $Root 'Saved\DriveTest'
New-Item -ItemType Directory -Force $Out | Out-Null
$History = Join-Path $Out 'history.csv'
if (-not (Test-Path $History)) { 'date,commit,car,fps_cap,manoeuvre,peak_kph,rear_slip_deg,yaw_dps,spun' | Set-Content -Encoding utf8 $History }
$Commit = (git -C $Root rev-parse --short HEAD).Trim()
$Stable = @('launch_turn_trig70', 'junction20_trig70', 'junction30_trig70', 'circle50_lift', 'turnin50_full', 'lanechange80')
# Per car, known issues recorded but not judged until they are fixed (docs/release/bugs.md).
# 1965 Mustang: the full-lock 70 % launch spins (rear slip about 41 deg); the open differential
# splits the torque equally while the inside wheel flares, so the real car should not step out
# this far (bug R4, physics).
$Expected = @{ 'mustang65' = @('launch_turn_trig70') }
$Failed = @()

$Runs = @(@{ Cap = 0 })
if ($LowFps) { $Runs += @{ Cap = 15 } }
foreach ($Car in $Cars) {
    foreach ($Run in $Runs) {
        $Tag = if ($Run.Cap -gt 0) { "$Car-$($Run.Cap)fps" } else { $Car }
        $Log = Join-Path $Out "robot-$Tag.log"
        $Csv = (Join-Path $Out "robot-$Tag.csv") -replace '\\', '/'
        $Args = @('-game', '-SimCruiseWorld', '-SimCruiseBundledStart', "-SimCruiseCar=$Car", '-NoLiveCoding', '-SimCruiseNoLive', '-SimCruiseNoNeural',
            '-SimCruiseDriveTest', '-SimCruiseDriveTestPad', "-SimCruiseDriveTestOut=$Csv", $(if ($ShowWindow) { '-windowed' } else { '-RenderOffScreen' }), '-ResX=1280', '-ResY=720', '-log', "-abslog=$Log")
        if ($Run.Cap -gt 0) { $Args += "-SimCruiseDriveTestMaxFps=$($Run.Cap)" }
        Write-Host "Drive robot: $Tag"
        if (Test-Path $Log) { Remove-Item $Log -Force }
        # A hung or crashed run is stopped after 15 min (editor.ps1 adds -unattended to -game runs).
        $Started = Get-Date
        & powershell -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'editor.ps1') -TimeoutMinutes 15 @Args | Out-Null
        $RunExit = $LASTEXITCODE
        # Evidence for a review (scripts\ReviewLib.ps1): the log, command line, GPU and a crash's dump.
        . (Join-Path $PSScriptRoot 'ReviewLib.ps1')
        $Evidence = Save-ReviewEvidence -RunDir (Join-Path (Get-ReviewRoot $Root) "robot-$Tag") -Root $Root -Log $Log -CommandLine ("editor.ps1 " + ($Args -join ' ')) -Started $Started -TimedOut ($RunExit -eq 4) -ExitCode $RunExit
        Write-ReviewSheet -RunDir (Join-Path (Get-ReviewRoot $Root) "robot-$Tag") -Kind 'robot' -Title "drive robot $Tag" -Result $(if (Test-Path $Log) { 'DONE' } else { '' }) -Reasons '' -Evidence $Evidence
        $LASTEXITCODE = $RunExit
        if ($LASTEXITCODE -eq 4) { Write-Host "  $Tag timed out after 15 min and was stopped"; $Failed += "$Tag (timed out)"; continue }
        $Lines = Select-String -Path $Log -Pattern 'DRIVETEST ([a-z0-9_]+): peak (\d+) km/h, peak rear-axle slip ([\d.]+) deg, peak yaw rate ([\d.]+) deg/s' -AllMatches
        if (-not $Lines) { Write-Host "  no DRIVETEST lines in $Log"; $Failed += "$Tag (no result)"; continue }
        foreach ($L in $Lines) {
            $M = $L.Matches[0].Groups
            $Name = $M[1].Value
            if ($Name -like 'approach_*') { continue }
            $Slip = [double]$M[3].Value
            $Spun = $Slip -gt 15.0
            "$(Get-Date -Format s),$Commit,$Car,$($Run.Cap),$Name,$($M[2].Value),$Slip,$($M[4].Value),$Spun" | Add-Content -Encoding utf8 $History
            $Judged = ($Stable -contains $Name) -and -not ($Expected.ContainsKey($Car) -and $Expected[$Car] -contains $Name)
            $Mark = if ($Spun -and $Judged) { 'FAIL'; $Failed += "$Tag $Name ($Slip deg)" } elseif ($Spun) { 'spun (expected)' } else { 'ok' }
            Write-Host ("  {0,-20} {1,5} km/h  rear slip {2,5:N1} deg  yaw {3,6} deg/s  {4}" -f $Name, $M[2].Value, $Slip, $M[4].Value, $Mark)
        }
    }
}
if ($Failed.Count) { Write-Host "DRIVE ROBOT FAILED: $($Failed -join '; ')"; exit 1 }
Write-Host 'DRIVE ROBOT PASSED'
exit 0
