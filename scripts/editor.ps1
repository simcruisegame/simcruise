<#
.SYNOPSIS
  Opens the Unreal Editor on this checkout while it holds the one-editor lock.

.DESCRIPTION
  Use it for play tests and MCP sessions when several people or agents share the PC:
  it waits until no other SimCruise GPU editor run (another scripts\editor.ps1, a capture,
  a play-test build) is open, opens the editor, and frees the lock when the editor closes.
  Headless test runs (scripts\check.ps1) do not wait for it.
  Extra arguments go to the editor, for example -game or -SimCruiseDriveCapture.
  A -game run gets -unattended, so a crash ends the run instead of opening a crash window.

.PARAMETER TimeoutMinutes
  Stop the editor this script started (and everything it started) after this many minutes
  and exit 4. 0 (the default) means no limit, for an interactive editor. Scripts that start
  -game runs pass a limit, for example 15.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\editor.ps1
  powershell -ExecutionPolicy Bypass -File scripts\editor.ps1 -TimeoutMinutes 15 -game -SimCruiseDriveTest
#>
param(
    [string]$EngineDir = $(if ($env:UE_ROOT) { $env:UE_ROOT } else { 'C:\Program Files\Epic Games\UE_5.8' }),
    [int]$LockTimeoutMinutes = 60,
    [int]$TimeoutMinutes = 0,
    [Parameter(ValueFromRemainingArguments = $true)][string[]]$EditorArgs
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'EditorLock.ps1')
$Root = Split-Path -Parent $PSScriptRoot
$Project = Join-Path $Root 'SimCruise.uproject'
$Editor = Join-Path $EngineDir 'Engine\Binaries\Win64\UnrealEditor.exe'
if (-not (Test-Path $Editor)) { Write-Host "No Unreal Editor at $Editor (set -EngineDir or UE_ROOT)."; exit 2 }

if (-not (Enter-EditorLock -TimeoutMinutes $LockTimeoutMinutes -Why "editor on $Root")) { exit 3 }
try {
    # Live Coding off: while an editor with Live Coding runs, Unreal Build Tool refuses every
    # build on the PC, so other checkouts could not build.
    $Extra = @($EditorArgs | Where-Object { $_ })
    if (-not ($Extra -contains '-NoLiveCoding')) { $Extra += '-NoLiveCoding' }
    # A scripted game run must never wait for a person: with -unattended a crash is written to
    # the log and the crash reporter quits, instead of a dialog that holds the lock.
    if (($Extra -contains '-game') -and -not ($Extra -contains '-unattended')) { $Extra += '-unattended' }
    # A game run needs no platform SDK check; skipping it saves a start-up call to Unreal Build Tool
    # that waits while another checkout builds.
    if ($Extra -contains '-game') { $env:UE_SKIP_UBT_SDK_SETUP = '1' }
    $ArgLine = (@("`"$Project`"") + $Extra) -join ' '
    $P = Start-Process -FilePath $Editor -ArgumentList $ArgLine -PassThru
    if ($TimeoutMinutes -gt 0) {
        if (-not $P.WaitForExit($TimeoutMinutes * 60 * 1000)) {
            # Only the tree this script started: taskkill /T from our own process id.
            Stop-ProcessTree -ProcessId $P.Id
            Write-Host "TIMED OUT: the editor did not quit in $TimeoutMinutes min and was stopped (pid $($P.Id))."
            exit 4
        }
    }
    else { $P.WaitForExit() }
    exit $P.ExitCode
}
finally { Exit-EditorLock }
