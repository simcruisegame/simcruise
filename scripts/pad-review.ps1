<#
.SYNOPSIS
  The controller menu test: can an Xbox controller reach every control of the menus?

.DESCRIPTION
  Two parts, each logging every control as PADNAV REACHED or PADNAV UNREACHABLE:
    1. Headless: the automation test SimCruise.UI.PadNavigation (scripts\check.ps1 -Filter). The
       menu sits in a hidden Slate window with the game's focus (SetUserFocus, user 0); pad keys go
       through FSlateApplication.
    2. In the game (-SimCruisePadReview, ASimCruisePadReview): the real game at Ocean Drive, off
       screen, the menus opened by the player controller as the player opens them, pad keys
       through FSlateApplication, the pad's focus logged after every press (PADWALK), frames of
       each screen. On the road atlas and "Where to?" the frames before and after the D-pad are
       compared: no change on screen means the D-pad does nothing there.
  report.md lists every control. The test FAILS (exit 1) on any UNREACHABLE control.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\pad-review.ps1
  powershell -ExecutionPolicy Bypass -File scripts\pad-review.ps1 -NoGame
#>
param(
    [string]$OutDir = '',
    [switch]$ShowWindow,
    [switch]$NoBuild,
    # Only the headless part.
    [switch]$NoGame,
    [string]$EngineDir = $(if ($env:UE_ROOT) { $env:UE_ROOT } else { 'C:\Program Files\Epic Games\UE_5.8' }),
    [int]$TimeoutMinutes = 8,
    [int]$LockTimeoutMinutes = 120
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'EditorLock.ps1')
. (Join-Path $PSScriptRoot 'ReviewLib.ps1')
$Root = Split-Path -Parent $PSScriptRoot
if (-not $OutDir) { $OutDir = Join-Path $Root "Saved\Review\$(Get-Date -Format 'yyyyMMdd-HHmm')-pad-review" }
New-Item -ItemType Directory -Force $OutDir | Out-Null
$Pattern = 'PADNAV (REACHED|UNREACHABLE) screen="([^"]*)" control="([^"]*)" how="([^"]*)"'
$Rows = @()
$Warnings = @()
function Add-Rows([string]$Log, [string]$Part) {
    $Seen = @{}
    if (-not (Test-Path $Log)) { return }
    foreach ($L in (Select-String -Path $Log -Pattern $Pattern)) {
        $G = $L.Matches[0].Groups
        $Key = "$($G[2].Value)|$($G[3].Value)"
        if ($Seen.ContainsKey($Key)) { continue }
        $Seen[$Key] = $true
        $script:Rows += [pscustomobject]@{ Part = $Part; State = $G[1].Value; Screen = $G[2].Value; Control = $G[3].Value; How = $G[4].Value }
    }
}

# 1. Headless.
$CheckArgs = @('-ExecutionPolicy', 'Bypass', '-File', (Join-Path $PSScriptRoot 'check.ps1'), '-Filter', 'SimCruise.UI.PadNavigation', '-NoViewCheck')
if ($NoBuild) { $CheckArgs += '-NoBuild' }
& powershell @CheckArgs | Out-File -Encoding utf8 (Join-Path $OutDir 'check.txt')
$TestLog = Join-Path $OutDir 'test.log'
$CheckLog = Join-Path $Root 'Saved\Logs\SimCruiseChecks.log'
if (Test-Path $CheckLog) { Copy-Item $CheckLog $TestLog -Force }
Add-Rows $TestLog 'headless'

# 2. In the game.
$GameRan = $false
if (-not $NoGame) {
    $GameDir = Join-Path $OutDir 'game'
    New-Item -ItemType Directory -Force $GameDir | Out-Null
    $Log = Join-Path $GameDir 'game.log'
    $Editor = Join-Path $EngineDir 'Engine\Binaries\Win64\UnrealEditor.exe'
    $Project = Join-Path $Root 'SimCruise.uproject'
    $GameArgs = @("`"$Project`"", '-game', '-SimCruiseWorld', '-SimCruiseBundledStart', '-SimCruiseNoLive', '-SimCruiseNoNeural', '-SimCruiseGraphics=ultra',
        '-SimCruiseUtc=2026-11-12T17:00:00Z', "`"-SimCruisePadReview=$GameDir`"", '-DPCVars=simcruise.Traffic.Cars=0', '-NoLiveCoding', '-nosplash', '-unattended',
        '-log', "`"-abslog=$Log`"") + @(Get-TestWindowArgs $ShowWindow) + @(Get-NativeResolution)
    $env:UE_SKIP_UBT_SDK_SETUP = '1'
    if (Enter-EditorLock -GpuTest -TimeoutMinutes $LockTimeoutMinutes -Why "pad review on $Root") {
        $Started = Get-Date; $TimedOut = $false; $ExitCode = -1
        try {
            $P = Start-Process -FilePath $Editor -ArgumentList ($GameArgs -join ' ') -PassThru
            if (-not $P.WaitForExit($TimeoutMinutes * 60 * 1000)) { Stop-ProcessTree -ProcessId $P.Id; $TimedOut = $true } else { $ExitCode = $P.ExitCode }
        }
        finally { Exit-EditorLock }
        Save-ReviewEvidence -RunDir $GameDir -Root $Root -Log $Log -CommandLine ("`"$Editor`" " + ($GameArgs -join ' ')) -Started $Started -TimedOut $TimedOut -ExitCode $ExitCode | Out-Null
        $GameRan = Test-Path $Log
        Add-Rows $Log 'game'
        # The D-pad on screens whose state the walk cannot read: did the screen change?
        foreach ($S in @(@{ Name = 'whereto'; Screen = 'Where to?' }, @{ Name = 'atlas'; Screen = 'Road atlas' })) {
            $A = Join-Path $GameDir "pad-$($S.Name)-0.png"; $B = Join-Path $GameDir "pad-$($S.Name)-1.png"
            if (-not ((Test-Path $A) -and (Test-Path $B))) { $script:Rows += [pscustomobject]@{ Part = 'game'; State = 'UNREACHABLE'; Screen = $S.Screen; Control = 'D-pad'; How = 'no frames (the screen did not open or no screenshot)' }; continue }
            $C = & python (Join-Path $PSScriptRoot 'compare-frames.py') $A $B (Join-Path $GameDir "diff-$($S.Name).png")
            $Changed = if ("$C" -match 'changed_over_25=([\d.]+)%') { [double]$Matches[1] } else { 0 }
            $script:Rows += [pscustomobject]@{ Part = 'game'; State = $(if ($Changed -ge 0.2) { 'REACHED' } else { 'UNREACHABLE' }); Screen = $S.Screen; Control = 'D-pad'
                How = "frames before and after the D-pad: $Changed % of pixels changed (limit 0.2 %)" }
            # The left stick: a warning only (Slate's spatial navigation, its order can differ from the D-pad's).
            $S2 = Join-Path $GameDir "pad-$($S.Name)-2.png"
            if (Test-Path $S2) {
                $C2 = & python (Join-Path $PSScriptRoot 'compare-frames.py') $B $S2
                $Changed2 = if ("$C2" -match 'changed_over_25=([\d.]+)%') { [double]$Matches[1] } else { 0 }
                if ($Changed2 -lt 0.2) { $script:Warnings += "$($S.Screen): the left stick changed nothing on screen ($Changed2 % of pixels)" }
                else { $script:Warnings += "$($S.Screen): the left stick moves ($Changed2 % of pixels changed); its order follows Slate's spatial navigation and can differ from the D-pad's (known gap)" }
            }
        }
    }
}

$Bad = @($Rows | Where-Object { $_.State -eq 'UNREACHABLE' })
$Result = if (-not $Rows.Count) { 'FAIL' } elseif ($Bad.Count) { 'FAIL' } else { 'PASS' }
$Md = @('# Controller menu test', '', "Verdict: **$Result** ($($Rows.Count) controls, $($Bad.Count) unreachable). In the game: $(if ($NoGame) { 'NOT RUN (-NoGame)' } elseif ($GameRan) { 'ran' } else { 'FAILED TO RUN' }).", '')
if (-not $Rows.Count) { $Md += 'No PADNAV lines: the tests did not run (see check.txt, test.log and game\game.log).' }
$Md += @('| Part | State | Screen | Control | How |', '|---|---|---|---|---|')
foreach ($R in ($Rows | Sort-Object @{ Expression = { $_.State -eq 'REACHED' } }, Part, Screen)) { $Md += "| $($R.Part) | $($R.State) | $($R.Screen) | $($R.Control) | $($R.How) |" }
if ($Warnings.Count) { $Md += @('', '## Warnings (not failures)', '') + @($Warnings | ForEach-Object { "- $_" }) }
$Md += @('', 'Frames: game\pad-*.png (each screen), game\diff-*.png. The pad focus after every press: PADWALK lines in game\game.log.')
$Md | Set-Content -Encoding utf8 (Join-Path $OutDir 'report.md')
Write-Host "PAD REVIEW $Result controls=$($Rows.Count) unreachable=$($Bad.Count) -> $(Join-Path $OutDir 'report.md')"
foreach ($B in $Bad) { Write-Host "  UNREACHABLE [$($B.Part)] $($B.Screen): $($B.Control) ($($B.How))" }
if ($Result -eq 'PASS') { exit 0 } else { exit 1 }
