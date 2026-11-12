<#
.SYNOPSIS
  Builds SimCruise and runs its automation tests; exits 1 if anything failed.

.DESCRIPTION
  The Unreal successor of the Godot client's scripts/check.sh and ScriptErrorGuard:
  one command that builds the editor target, runs the SimCruise.* automation tests
  headless, and fails on any failed test, on a test that never reported, or on an
  error the game itself logged (an ensure, a fatal error, or an Error line from a
  SimCruise log category), so a check cannot pass after an abandoned function.
  The benches' calibration bands live in the tests themselves.

.PARAMETER Filter
  Test path prefix (default SimCruise; for example SimCruise.Vehicle or SimCruise.Audio).

.PARAMETER NoBuild
  Skip the build (the editor binaries must be current).

.PARAMETER EngineDir
  The Unreal Engine 5.8 directory (default: $env:UE_ROOT, else C:\Program Files\Epic Games\UE_5.8).

.PARAMETER TimeoutMinutes
  Stop the test run (the editor and its children) after this many minutes and fail (default 20).

.PARAMETER LockTimeoutMinutes
  How long to wait for another SimCruise editor run on this PC to finish (default 60).
  Builds queue on Unreal Build Tool's own mutex (-WaitMutex); the test run takes the
  one-editor lock in scripts\EditorLock.ps1, so checks from several worktrees take turns.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\check.ps1
  powershell -ExecutionPolicy Bypass -File scripts\check.ps1 -Filter SimCruise.Audio -NoBuild
#>
param(
    [string]$Filter = 'SimCruise',
    [switch]$NoBuild,
    [string]$EngineDir = $(if ($env:UE_ROOT) { $env:UE_ROOT } else { 'C:\Program Files\Epic Games\UE_5.8' }),
    [int]$TimeoutMinutes = 20,
    [int]$LockTimeoutMinutes = 60
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'EditorLock.ps1')
$Root = Split-Path -Parent $PSScriptRoot
$Project = Join-Path $Root 'SimCruise.uproject'
$Editor = Join-Path $EngineDir 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$BuildBat = Join-Path $EngineDir 'Engine\Build\BatchFiles\Build.bat'
if (-not (Test-Path $Editor)) { Write-Host "No Unreal Editor at $Editor (set -EngineDir or UE_ROOT)."; exit 2 }

$Started = Get-Date
if (-not $NoBuild) {
    Write-Host 'Building SimCruiseEditor (Win64 Development)...'
    & $BuildBat SimCruiseEditor Win64 Development "-Project=$Project" -WaitMutex -NoHotReloadFromIDE | Out-Host
    if ($LASTEXITCODE -ne 0) { Write-Host 'BUILD FAILED'; exit 1 }
}

$LogName = 'SimCruiseChecks.log'
$Log = Join-Path $Root "Saved\Logs\$LogName"
if (Test-Path $Log) { Remove-Item $Log -Force }
if (-not (Enter-EditorLock -TimeoutMinutes $LockTimeoutMinutes -Why "tests on $Root")) { exit 1 }
try {
    Write-Host "Running automation tests: $Filter"
    $ArgLine = "`"$Project`" `"-ExecCmds=Automation RunTests $Filter;Quit`" -unattended -nopause -nullrhi -nosplash `"-log=$LogName`" `"-testexit=Automation Test Queue Empty`""
    $P = Start-Process -FilePath $Editor -ArgumentList $ArgLine -PassThru -WindowStyle Hidden
    if (-not $P.WaitForExit($TimeoutMinutes * 60 * 1000)) {
        Stop-ProcessTree -ProcessId $P.Id
        Write-Host "TIMED OUT: the test run did not quit in $TimeoutMinutes min and was stopped (log: $Log)."
        exit 1
    }
}
finally { Exit-EditorLock }
if (-not (Test-Path $Log)) { Write-Host "No log at ${Log}: the editor did not run."; exit 1 }

$Lines = Get-Content $Log
$Results = @()
$Failures = @()
$Current = $null
foreach ($Line in $Lines) {
    if ($Line -match 'Test Completed\. Result=\{(\w+)\} Name=\{([^}]*)\} Path=\{([^}]*)\}') {
        $Results += [pscustomobject]@{ Result = $Matches[1]; Path = $Matches[3] }
        if ($Matches[1] -ne 'Success') { $Current = $Matches[3]; $Failures += [pscustomobject]@{ Path = $Matches[3]; Errors = @() } }
        else { $Current = $null }
    }
    elseif ($Current -and $Line -match 'LogAutomationController: Error: (.*)') {
        $Failures[-1].Errors += $Matches[1]
    }
}
# Errors the game logged outside a failed expectation (the ScriptErrorGuard principle).
$Logged = $Lines | Where-Object { $_ -match 'Ensure condition failed|Fatal error|LogSimCruise\w*: Error:' }

$Passed = ($Results | Where-Object { $_.Result -eq 'Success' }).Count
$Seconds = [int]((Get-Date) - $Started).TotalSeconds
foreach ($F in $Failures) {
    Write-Host "FAILED $($F.Path)"
    foreach ($E in ($F.Errors | Select-Object -First 3)) { Write-Host "    $E" }
}
foreach ($L in ($Logged | Select-Object -First 10)) { Write-Host "LOGGED ERROR $L" }
if ($Results.Count -eq 0) { Write-Host "No test results in $Log (no test matched '$Filter'?)."; exit 1 }
Write-Host ("{0} passed, {1} failed, {2} logged errors in {3} s (log: {4})" -f $Passed, $Failures.Count, @($Logged).Count, $Seconds, $Log)
if ($Failures.Count -gt 0 -or @($Logged).Count -gt 0) { Write-Host 'CHECKS FAILED'; exit 1 }
Write-Host 'CHECKS PASSED'
exit 0
