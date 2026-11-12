<#
.SYNOPSIS
  Opens the Unreal Editor on this checkout while it holds the one-editor lock.

.DESCRIPTION
  Use it for play tests and MCP sessions when several people or agents share the PC:
  it waits until no other SimCruise editor run (scripts\check.ps1 or another
  scripts\editor.ps1) is open, opens the editor, and frees the lock when the editor closes.
  Extra arguments go to the editor, for example -game or -SimCruiseDriveCapture.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\editor.ps1
  powershell -ExecutionPolicy Bypass -File scripts\editor.ps1 -game -SimCruiseDriveTest
#>
param(
    [string]$EngineDir = $(if ($env:UE_ROOT) { $env:UE_ROOT } else { 'C:\Program Files\Epic Games\UE_5.8' }),
    [int]$LockTimeoutMinutes = 60,
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
    $Extra = @($EditorArgs)
    if (-not ($Extra -contains '-NoLiveCoding')) { $Extra += '-NoLiveCoding' }
    $ArgLine = (@("`"$Project`"") + $Extra) -join ' '
    $P = Start-Process -FilePath $Editor -ArgumentList $ArgLine -PassThru
    $P.WaitForExit()
    exit $P.ExitCode
}
finally { Exit-EditorLock }
