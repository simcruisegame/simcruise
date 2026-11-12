<#
.SYNOPSIS
  Realism score: photographs the game at the pose of licensed street photographs and scores
  how close it comes (lab/realism/README.md).

.DESCRIPTION
  Builds the editor target, fetches each pose's photograph into Saved\Realism\photos (never
  committed), opens the game once per pose under the PC-wide editor lock with
  -SimCruiseDriveCapture -SimCruiseRealismPose at the photograph's time and size (moving traffic
  off), then runs lab\realism\score.py. One REALISM line per pose; the rows are appended to
  Saved\Realism\history.csv with the commit as the label. Exit 1 if a pose could not be scored.

.PARAMETER Pose
  Only this pose (a name from lab\realism\poses.json).

.PARAMETER NoBuild
  Skip the build (the editor binaries must be current).

.PARAMETER Graphics
  Graphics preset for the captures (default ultra).

.PARAMETER TimeoutMinutes
  Stop one capture run after this many minutes (default 15).

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\realism.ps1
  powershell -ExecutionPolicy Bypass -File scripts\realism.ps1 -Pose lummus -NoBuild
#>
param(
    [string]$Pose = '',
    [switch]$NoBuild,
    [string]$Graphics = 'ultra',
    [int]$TimeoutMinutes = 15,
    [int]$LockTimeoutMinutes = 60,
    # Extra editor arguments for an A/B, e.g. '-DPCVars=r.DefaultFeature.LocalExposure.HighlightContrastScale=0.7'.
    [string]$ExtraArgs = '',
    [string]$EngineDir = $(if ($env:UE_ROOT) { $env:UE_ROOT } else { 'C:\Program Files\Epic Games\UE_5.8' })
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'EditorLock.ps1')
$Root = Split-Path -Parent $PSScriptRoot
$Project = Join-Path $Root 'SimCruise.uproject'
$Editor = Join-Path $EngineDir 'Engine\Binaries\Win64\UnrealEditor.exe'
$BuildBat = Join-Path $EngineDir 'Engine\Build\BatchFiles\Build.bat'
if (-not (Test-Path $Editor)) { Write-Host "No Unreal Editor at $Editor (set -EngineDir or UE_ROOT)."; exit 2 }

$Poses = @((Get-Content -Raw (Join-Path $Root 'lab\realism\poses.json') | ConvertFrom-Json).poses)
if ($Pose) { $Poses = @($Poses | Where-Object { $_.name -eq $Pose }) }
if ($Poses.Count -eq 0) { Write-Host "No pose named '$Pose' in lab\realism\poses.json."; exit 2 }

$Work = Join-Path $Root 'Saved\Realism'
$Photos = Join-Path $Work 'photos'
$Stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$Captures = Join-Path $Work "captures\$Stamp"
New-Item -ItemType Directory -Force $Photos, $Captures | Out-Null

# 1. The reference photographs (cached; links only in git).
foreach ($P in $Poses) {
    $Photo = Join-Path $Photos "$($P.name).jpg"
    if (Test-Path $Photo) { continue }
    if ($P.photo.source -ne 'panoramax') { Write-Host "Put the photograph for $($P.name) at $Photo"; continue }
    if (-not (Get-Command bun -ErrorAction SilentlyContinue)) { Write-Host 'Bun is needed to fetch the photographs (https://bun.com).'; exit 2 }
    Write-Host "Fetching Panoramax $($P.photo.id) for $($P.name)"
    & bun (Join-Path $Root 'lab\taste\scripts\panoramax-near.mjs') get $P.photo.id $Photos | Out-Null
    $Fetched = Join-Path $Photos "pano-$($P.photo.id).jpg"
    if (-not (Test-Path $Fetched)) { Write-Host "Could not fetch the photograph for $($P.name)."; exit 1 }
    Move-Item -Force $Fetched $Photo
}

# 2. The build.
if (-not $NoBuild) {
    Write-Host 'Building SimCruiseEditor (Win64 Development)...'
    & $BuildBat SimCruiseEditor Win64 Development "-Project=$Project" -WaitMutex -NoHotReloadFromIDE | Out-Host
    if ($LASTEXITCODE -ne 0) { Write-Host 'BUILD FAILED'; exit 1 }
}

# 3. One game run per pose, so each pose gets its own window size, time and start.
foreach ($P in $Poses) {
    $PoseArg = "$($P.name),$($P.lat),$($P.lon),$($P.heading),$($P.hfov)"
    $Start = if ($P.world -eq 'bundled') { '-SimCruiseWorld' } else { "-SimCruiseStart=$($P.lat),$($P.lon)" }
    $ArgLine = "`"$Project`" -game -windowed -ResX=$($P.width) -ResY=$($P.height) -nosplash -unattended $Start " +
        "-SimCruiseDriveCapture `"-SimCruiseDriveCaptureOut=$Captures`" -SimCruiseRealismPose=$PoseArg " +
        "-SimCruiseUtc=$($P.utc) -SimCruiseGraphics=$Graphics `"-ExecCmds=simcruise.Traffic.Cars 0`" -log=SimCruiseRealism.log -NoLiveCoding $ExtraArgs"
    if (-not (Enter-EditorLock -TimeoutMinutes $LockTimeoutMinutes -Why "realism capture $($P.name) on $Root")) { exit 1 }
    try {
        Write-Host "Capturing $($P.name)"
        $Proc = Start-Process -FilePath $Editor -ArgumentList $ArgLine -PassThru
        if (-not $Proc.WaitForExit($TimeoutMinutes * 60 * 1000)) {
            Stop-ProcessTree -ProcessId $Proc.Id
            Write-Host "TIMED OUT: the capture of $($P.name) did not finish in $TimeoutMinutes min and was stopped."
        }
    }
    finally { Exit-EditorLock }
}

# 4. The score.
$Label = (& git -C $Root rev-parse --short HEAD).Trim()
if (& git -C $Root status --porcelain --untracked-files=no) { $Label += '+changes' }
$ScoreArgs = @((Join-Path $Root 'lab\realism\score.py'), '--poses', (Join-Path $Root 'lab\realism\poses.json'),
    '--captures', $Captures, '--photos', $Photos, '--history', (Join-Path $Work 'history.csv'), '--label', $Label)
if ($Pose) {
    # Score only the pose that was captured.
    $One = Join-Path $Captures 'poses.json'
    @{ poses = $Poses } | ConvertTo-Json -Depth 5 | Set-Content -Encoding utf8 $One
    $ScoreArgs[2] = $One
}
if (Get-Command uv -ErrorAction SilentlyContinue) {
    & uv run --quiet --no-project --with numpy --with pillow python @ScoreArgs
} else {
    & python @ScoreArgs
}
$Code = $LASTEXITCODE
Write-Host "Captures: $Captures"
exit $Code
