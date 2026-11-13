<#
.SYNOPSIS
  The view check gate: starts the game at fixed places, lets ASimCruiseViewCheck look at
  the player's view, and fails when a view that must pass fails (or the other way round).

.DESCRIPTION
  Each case runs the editor binaries as a game (-game, Ultra graphics, 1280x720, the
  one-editor lock of scripts\EditorLock.ps1) with -SimCruiseViewCheck -SimCruiseViewCheckQuit,
  offline (-SimCruiseNoLive -SimCruiseNoNeural) on the bundled Miami Beach tiles. The game
  writes one line:

    VIEWCHECK PASS|FAIL place="..." road=..% building=..% ... shot=<png> reason=<...>

  Cases:
    ocean-drive  the bundled Ocean Drive start                      must PASS
    collins-ave  a second start on the bundled tiles (Collins Ave)   must PASS
    test-pad     the drive robot's test pad (-SimCruiseDriveTestPad) must FAIL

  A missing VIEWCHECK line fails the gate. Each run copies the screenshots and their JSON
  statistics to Saved\ViewCheck\<date-time>\ (the last 10 runs are kept) and to
  Saved\ViewCheck\latest\ as <case>-<pass|fail>.png and .json. Look at them after each run.

.PARAMETER Cases
  The cases to run (default all).

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\viewcheck.ps1
  powershell -ExecutionPolicy Bypass -File scripts\viewcheck.ps1 -Cases ocean-drive
#>
param(
    [string[]]$Cases = @('ocean-drive', 'collins-ave', 'test-pad'),
    [string]$EngineDir = $(if ($env:UE_ROOT) { $env:UE_ROOT } else { 'C:\Program Files\Epic Games\UE_5.8' }),
    [int]$TimeoutMinutes = 15,
    [int]$LockTimeoutMinutes = 60,
    [int]$Keep = 10
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'EditorLock.ps1')
$Root = Split-Path -Parent $PSScriptRoot
$Project = Join-Path $Root 'SimCruise.uproject'
$Editor = Join-Path $EngineDir 'Engine\Binaries\Win64\UnrealEditor.exe'
if (-not (Test-Path $Editor)) { Write-Host "No Unreal Editor at $Editor (set -EngineDir or UE_ROOT)."; exit 2 }

$Common = @('-game', '-SimCruiseWorld', '-SimCruiseNoLive', '-SimCruiseNoNeural', '-SimCruiseGraphics=ultra',
    '-SimCruiseViewCheck', '-SimCruiseViewCheckQuit', '-NoLiveCoding', '-windowed', '-ResX=1280', '-ResY=720', '-nosplash', '-unattended', '-log')
$Defs = @{
    'ocean-drive' = @{ Expect = 'PASS'; Args = @('-SimCruiseBundledStart') }
    'collins-ave' = @{ Expect = 'PASS'; Args = @('-SimCruiseStart=25.7850,-80.1318', '"-SimCruiseStartRoad=Collins Avenue"') }
    'test-pad'    = @{ Expect = 'FAIL'; Args = @('-SimCruiseBundledStart', '-SimCruiseDriveTest', '-SimCruiseDriveTestPad') }
}

# A game run needs no platform SDK check: skip the start-up call to Unreal Build Tool,
# which waits while another checkout builds.
$env:UE_SKIP_UBT_SDK_SETUP = '1'
$Base = Join-Path $Root 'Saved\ViewCheck'
$RunDir = Join-Path $Base (Get-Date -Format 'yyyyMMdd-HHmmss')
$Latest = Join-Path $Base 'latest'
New-Item -ItemType Directory -Force $RunDir | Out-Null
New-Item -ItemType Directory -Force $Latest | Out-Null

$Failed = @()
# "powershell -File" hands "a,b" over as one string.
$Cases = @($Cases | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
foreach ($Case in $Cases) {
    $Def = $Defs[$Case]
    if (-not $Def) { Write-Host "Unknown case $Case"; $Failed += "$Case (unknown)"; continue }
    # Only this case's old files leave latest\; the other cases' stay.
    Get-ChildItem $Latest -File | Where-Object { $_.Name -like "$Case-*" -or $_.Name -eq "$Case.log" } | Remove-Item -Force
    $Log = Join-Path $RunDir "$Case.log"
    $ArgLine = (@("`"$Project`"") + $Common + $Def.Args + @("`"-abslog=$Log`"")) -join ' '
    if (-not (Enter-EditorLock -TimeoutMinutes $LockTimeoutMinutes -Why "view check $Case on $Root")) { exit 1 }
    try {
        Write-Host "View check: $Case (must $($Def.Expect))"
        $P = Start-Process -FilePath $Editor -ArgumentList $ArgLine -PassThru
        if (-not $P.WaitForExit($TimeoutMinutes * 60 * 1000)) {
            Stop-ProcessTree -ProcessId $P.Id
            Write-Host "  TIMED OUT after $TimeoutMinutes min (log: $Log)"
        }
    }
    finally { Exit-EditorLock }
    $Line = if (Test-Path $Log) { Select-String -Path $Log -Pattern 'VIEWCHECK (PASS|FAIL) .*' | Select-Object -Last 1 } else { $null }
    if (-not $Line) { Write-Host "  no VIEWCHECK line in $Log"; $Failed += "$Case (no VIEWCHECK line)"; continue }
    $Text = $Line.Matches[0].Value
    $Result = $Line.Matches[0].Groups[1].Value
    Write-Host "  $Text"
    if ($Text -match 'shot=(.+?\.png)') {
        $Shot = $Matches[1]
        $Name = "$Case-$($Result.ToLower())"
        if (Test-Path $Shot) {
            Copy-Item $Shot (Join-Path $RunDir "$Name.png")
            Copy-Item $Shot (Join-Path $Latest "$Name.png")
        }
        $Json = [IO.Path]::ChangeExtension($Shot, 'json')
        if (Test-Path $Json) {
            Copy-Item $Json (Join-Path $RunDir "$Name.json")
            Copy-Item $Json (Join-Path $Latest "$Name.json")
        }
    }
    Copy-Item $Log (Join-Path $Latest "$Case.log")
    if ($Result -ne $Def.Expect) { $Failed += "$Case ($Result, must be $($Def.Expect))" }
}

# Keep the last $Keep dated runs.
Get-ChildItem $Base -Directory | Where-Object { $_.Name -match '^\d{8}-\d{6}$' } | Sort-Object Name -Descending |
    Select-Object -Skip $Keep | ForEach-Object { Remove-Item -Recurse -Force $_.FullName }

Write-Host "Screenshots: $Latest"
if ($Failed.Count) { Write-Host "VIEW CHECK FAILED: $($Failed -join '; ')"; exit 1 }
Write-Host 'VIEW CHECK PASSED'
exit 0
