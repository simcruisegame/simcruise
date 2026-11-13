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

.PARAMETER Slots
  How many headless test runs may run at once on this PC (1 to 3). Default: $env:SIMCRUISE_TEST_SLOTS,
  else what the free memory allows (free GB minus 8, divided by one test editor's peak).
  Test runs start without the MCP plugin and with -nullrhi, so they use neither port 8000
  nor the GPU and do not wait for the one-editor lock that play tests and MCP sessions hold.

.PARAMETER Tier
  full (the default; run it before a merge) runs every test that matches -Filter. fast skips
  the slow tests, the ones tagged [Slow] in Config\Tests\Tags.ini, and ends in about 1.5 min.
  A test that is not in that list runs in both tiers.

.PARAMETER NoBuild
  Skip the build (the editor binaries must be current). Without it the build is still
  skipped when no file under Source\ and no .uproject changed since the last good build
  of this checkout (Saved\CheckBuild.stamp).

.PARAMETER ForceBuild
  Run Build.bat even when the sources did not change.

.PARAMETER EngineDir
  The Unreal Engine 5.8 directory (default: $env:UE_ROOT, else C:\Program Files\Epic Games\UE_5.8).

.PARAMETER TimeoutMinutes
  Stop the test run (the editor and its children) after this many minutes and fail (default 20).

.PARAMETER LockTimeoutMinutes
  How long to wait for a free headless test slot on this PC (default 60).
  Builds queue on Unreal Build Tool's own mutex (-WaitMutex); the test run takes one of
  the headless slots in scripts\EditorLock.ps1, so checks from several worktrees share them.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\check.ps1
  powershell -ExecutionPolicy Bypass -File scripts\check.ps1 -Filter SimCruise.Audio -NoBuild
#>
param(
    [string]$Filter = 'SimCruise',
    [int]$Slots = 0,
    [ValidateSet('fast', 'full')][string]$Tier = 'full',
    [switch]$ForceBuild,
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

# Every run appends one line to Saved\CheckTimes.csv, so we can see where the time goes
# (waiting for the lock, building, testing) and how much memory one test editor needs.
$TimesCsv = Join-Path $Root 'Saved\CheckTimes.csv'
$Timing = @{ Tier = 'full'; Slots = ''; LockWait = 0; Build = 0; Test = 0; PeakGB = 0; Passed = 0; Failed = 0 }
function Write-CheckTime([string]$Result) {
    try {
        $Commit = (& git -C $Root rev-parse --short HEAD 2>$null)
        New-Item -ItemType Directory -Force (Split-Path $TimesCsv) | Out-Null
        if (-not (Test-Path $TimesCsv)) { 'date,commit,tier,filter,slots,lock_wait_s,build_s,test_s,peak_ram_gb,passed,failed,result' | Set-Content -Encoding utf8 $TimesCsv }
        $Line = '{0},{1},{2},{3},{4},{5},{6},{7},{8},{9},{10},{11}' -f (Get-Date -Format s), $Commit, $Timing.Tier, $Filter, $Timing.Slots,
            [int]$Timing.LockWait, [int]$Timing.Build, [int]$Timing.Test, $Timing.PeakGB, $Timing.Passed, $Timing.Failed, $Result
        Add-Content -Encoding utf8 $TimesCsv $Line
        Write-Host ("Times: lock wait {0} s, build {1} s, tests {2} s, editor peak {3} GB ({4})" -f [int]$Timing.LockWait, [int]$Timing.Build, [int]$Timing.Test, $Timing.PeakGB, $TimesCsv)
    }
    catch { Write-Host "Could not write $TimesCsv`: $_" }
}

$Timing.Tier = $Tier

# The build is skipped when nothing it compiles changed since the last good build here: the
# stamp is a hash of every file's path, size and time under Source\ (and Plugins\), the
# .uproject and the engine's build version, written after Build.bat succeeds.
$BuildStamp = Join-Path $Root 'Saved\CheckBuild.stamp'
function Get-BuildStamp {
    $Files = @(Get-ChildItem (Join-Path $Root 'Source') -Recurse -File)
    $Files += @(Get-ChildItem $Root -Filter '*.uproject' -File)
    $PluginDir = Join-Path $Root 'Plugins'
    if (Test-Path $PluginDir) { $Files += @(Get-ChildItem $PluginDir -Recurse -File | Where-Object { $_.FullName -notmatch '\\(Binaries|Intermediate)\\' }) }
    $EngineVersion = Join-Path $EngineDir 'Engine\Build\Build.version'
    if (Test-Path $EngineVersion) { $Files += Get-Item $EngineVersion }
    $Text = ($Files | Sort-Object FullName | ForEach-Object { '{0}|{1}|{2}' -f $_.FullName, $_.Length, $_.LastWriteTimeUtc.Ticks }) -join "`n"
    $Sha = [System.Security.Cryptography.SHA256]::Create()
    return [BitConverter]::ToString($Sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($Text))) -replace '-', ''
}

$Started = Get-Date
if (-not $NoBuild) {
    $Stamp = Get-BuildStamp
    $EditorDll = Join-Path $Root 'Binaries\Win64\UnrealEditor-SimCruise.dll'
    if (-not $ForceBuild -and (Test-Path $EditorDll) -and (Test-Path $BuildStamp) -and ((Get-Content $BuildStamp -Raw).Trim() -eq $Stamp)) {
        Write-Host 'Build skipped: nothing under Source\ or in the .uproject changed since the last good build (-ForceBuild builds anyway).'
    }
    else {
        Write-Host 'Building SimCruiseEditor (Win64 Development)...'
        & $BuildBat SimCruiseEditor Win64 Development "-Project=$Project" -WaitMutex -NoHotReloadFromIDE | Out-Host
        $Timing.Build = ((Get-Date) - $Started).TotalSeconds
        if ($LASTEXITCODE -ne 0) { Write-Host 'BUILD FAILED'; Remove-Item $BuildStamp -ErrorAction SilentlyContinue; Write-CheckTime 'build-failed'; exit 1 }
        # Hash again after the build, in case a file changed while it ran (then the next run builds).
        New-Item -ItemType Directory -Force (Split-Path $BuildStamp) | Out-Null
        if ((Get-BuildStamp) -eq $Stamp) { Set-Content -Encoding ascii $BuildStamp $Stamp }
    }
}

$LogName = 'SimCruiseChecks.log'
$Log = Join-Path $Root "Saved\Logs\$LogName"
if (Test-Path $Log) { Remove-Item $Log -Force }
$LockStart = Get-Date
$Timing.Slots = Get-HeadlessSlotCount -Requested $Slots
if (-not (Enter-HeadlessSlot -Slots $Timing.Slots -TimeoutMinutes $LockTimeoutMinutes -Why "tests on $Root")) { $Timing.LockWait = ((Get-Date) - $LockStart).TotalSeconds; Write-CheckTime 'lock-timeout'; exit 1 }
$Timing.LockWait = ((Get-Date) - $LockStart).TotalSeconds
$TestStart = Get-Date
try {
    Write-Host "Running automation tests: $Filter ($Tier tier)"
    # The fast tier filters on the [Slow] tags in Config\Tests\Tags.ini (Unreal's own tag filter).
    $RunCmds = if ($Tier -eq 'fast') { "Automation SetTagFilter NOT Slow;RunTests $Filter;Quit" } else { "Automation RunTests $Filter;Quit" }
    # Without the MCP plugins the test editor never listens on port 8000 (the main checkout's
    # user settings auto-start the server), so it can run beside an MCP session.
    $ArgLine = "`"$Project`" `"-ExecCmds=$RunCmds`" -unattended -nopause -nullrhi -nosplash -DisablePlugins=ModelContextProtocol `"-log=$LogName`" `"-testexit=Automation Test Queue Empty`""
    # Skip the editor's start-up call to Unreal Build Tool (-Mode=ValidatePlatforms): it queues on
    # UBT's PC-wide mutex, so a build in another checkout held a test start up to 84 s. Tests only
    # need Win64, so the platform SDK check has nothing to tell them.
    $env:UE_SKIP_UBT_SDK_SETUP = '1'
    $P = Start-Process -FilePath $Editor -ArgumentList $ArgLine -PassThru -WindowStyle Hidden
    # Wait in short steps to sample the editor's peak memory (working set) on the way.
    $Deadline = $TestStart.AddMinutes($TimeoutMinutes)
    $Peak = 0
    while (-not $P.WaitForExit(2000)) {
        try { $P.Refresh(); if ($P.PeakWorkingSet64 -gt $Peak) { $Peak = $P.PeakWorkingSet64 } } catch { }
        if ((Get-Date) -gt $Deadline) {
            Stop-ProcessTree -ProcessId $P.Id
            $Timing.Test = ((Get-Date) - $TestStart).TotalSeconds; $Timing.PeakGB = [math]::Round($Peak / 1GB, 1)
            Write-Host "TIMED OUT: the test run did not quit in $TimeoutMinutes min and was stopped (log: $Log)."
            Write-CheckTime 'timeout'
            exit 1
        }
    }
    $Timing.Test = ((Get-Date) - $TestStart).TotalSeconds
    $Timing.PeakGB = [math]::Round($Peak / 1GB, 1)
}
finally { Exit-HeadlessSlot }
if (-not (Test-Path $Log)) { Write-Host "No log at ${Log}: the editor did not run."; Write-CheckTime 'no-log'; exit 1 }

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
$Timing.Passed = $Passed; $Timing.Failed = $Failures.Count
if ($Results.Count -eq 0) { Write-Host "No test results in $Log (no test matched '$Filter'?)."; Write-CheckTime 'no-results'; exit 1 }
Write-CheckTime $(if ($Failures.Count -gt 0 -or @($Logged).Count -gt 0) { 'failed' } else { 'passed' })
Write-Host ("{0} passed, {1} failed, {2} logged errors in {3} s (log: {4})" -f $Passed, $Failures.Count, @($Logged).Count, $Seconds, $Log)
if ($Failures.Count -gt 0 -or @($Logged).Count -gt 0) { Write-Host 'CHECKS FAILED'; exit 1 }
Write-Host 'CHECKS PASSED'
exit 0
