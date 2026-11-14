<#
.SYNOPSIS
  Compares the drive start of two builds side by side: time to the placed car, the longest frame,
  frames over 0.5 s, the traffic-model line, frame p95, and the phases of the start.

.DESCRIPTION
  For each build folder (-Builds, in order) and each place: one short untimed launch to warm the
  shaders, then one timed launch (scripts\launch-review.ps1 of that build, off screen, Ultra 75 %,
  no cockpit test, -Seconds). The phases come from the game log's times: the start, the origin
  tile, the first and last tile built, the traffic models built, the car placed, the drive start.
  Writes start-compare.md in -OutRoot.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\start-compare.ps1 -Builds D:\sc\lane-rc,D:\sc\lane-rc2
#>
param(
    [Parameter(Mandatory = $true)][string[]]$Builds,
    [string]$OutRoot = '',
    [int]$Seconds = 60,
    [switch]$NoWarm,
    [int]$QuietWaitMinutes = 30
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'EditorLock.ps1')
Set-GpuBusy 'start-compare (timed game launches)' 40
try {
$Builds = @($Builds | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
if (-not $OutRoot) { $OutRoot = Join-Path (Split-Path -Parent $PSScriptRoot) "Saved\Review\$(Get-Date -Format 'yyyyMMdd-HHmm')-start-compare" }
New-Item -ItemType Directory -Force $OutRoot | Out-Null
$Places = @(
    @{ Name = 'dragon'; Args = @('-Road', 'dragon'); Car = 'nb2001_lowered'; Utc = '2026-11-12T17:00:00Z' },
    @{ Name = 'detroit'; Args = @('-Start', '42.3500,-83.0600'); Car = 'nb2001_lowered'; Utc = '2026-11-12T17:32:14Z' }
)

function Get-T([string]$Line) {
    if ($Line -match '^\[(\d{4})\.(\d\d)\.(\d\d)-(\d\d)\.(\d\d)\.(\d\d):(\d{3})') {
        return [datetime]::new([int]$Matches[1], [int]$Matches[2], [int]$Matches[3], [int]$Matches[4], [int]$Matches[5], [int]$Matches[6], [int]$Matches[7], [DateTimeKind]::Utc)
    }
    return $null
}
function First([string]$Log, [string]$Pattern) { $L = Select-String -Path $Log -Pattern $Pattern | Select-Object -First 1; if ($L) { return $L } return $null }
function Last([string]$Log, [string]$Pattern) { $L = Select-String -Path $Log -Pattern $Pattern | Select-Object -Last 1; if ($L) { return $L } return $null }

. (Join-Path $PSScriptRoot 'LoadWatch.ps1')
# Wait for a quiet PC (no other Unreal build or test), up to -QuietWaitMinutes; then run anyway, marked NOT QUIET.
function Wait-Quiet {
    $Until = (Get-Date).AddMinutes($QuietWaitMinutes)
    while ($true) {
        $S = Get-LoadSample $OutRoot
        if (-not $S.OtherProcesses) { return 'quiet' }
        if ((Get-Date) -gt $Until) { return "NOT QUIET ($($S.OtherProcesses))" }
        Write-Host "Waiting for a quiet PC: $($S.OtherProcesses)"
        Start-Sleep -Seconds 30
    }
}

$Rows = @()
foreach ($B in $Builds) {
    $Hash = (& git -C $B rev-parse --short HEAD).Trim()
    $Launch = Join-Path $B 'scripts\launch-review.ps1'
    foreach ($P in $Places) {
        $Common = @('-ExecutionPolicy', 'Bypass', '-File', $Launch, '-Car', $P.Car, '-Utc', $P.Utc, '-NoCockpit', '-OutRoot', (Join-Path $OutRoot $Hash)) + $P.Args
        if (-not $NoWarm) { & powershell @($Common + @('-Seconds', 15, '-Name', "$($P.Name)-warm")) | Out-Null }
        $Quiet = Wait-Quiet
        $Job = Start-LoadWatch (Join-Path $OutRoot "$Hash\$($P.Name)-load") 10
        & powershell @($Common + @('-Seconds', $Seconds, '-Name', $P.Name)) | Out-Null
        $Load = Stop-LoadWatch $Job (Join-Path $OutRoot "$Hash\$($P.Name)-load")
        if ($Quiet -eq 'quiet' -and -not $Load.Quiet) { $Quiet = "NOT QUIET ($($Load.OtherProcesses))" }
        $Dir = Join-Path $OutRoot "$Hash\$($P.Name)"
        $Log = Join-Path $Dir 'game.log'
        if (-not (Test-Path $Log)) { $Rows += "| $($P.Name) | $Hash | no log | | | | | | |"; continue }
        $T0 = Get-T (First $Log '^\[\d{4}\.\d\d\.\d\d-').Line
        $Origin = First $Log 'origin tile'
        $TileFirst = First $Log 'LogSimCruiseWorld: Display: Tile \d+_\d+: '
        $TileLast = Last $Log 'LogSimCruiseWorld: Display: Tile \d+_\d+: '
        $Model = First $Log 'Traffic model .*built before the drive in ([\d.]+) s'
        $Placed = First $Log 'Car placed.*after ([\d.]+) s'
        $ModelsFirst = First $Log 'LogSimCruiseCarAssets'
        $ModelsLast = Last $Log 'LogSimCruiseCarAssets'
        $Pre = if (Test-Path (Join-Path $Dir 'preflight.json')) { Get-Content (Join-Path $Dir 'preflight.json') -Raw | ConvertFrom-Json } else { $null }
        $Rev = if (Test-Path (Join-Path $Dir 'review.json')) { Get-Content (Join-Path $Dir 'review.json') -Raw | ConvertFrom-Json } else { $null }
        $Worst = Last $Log 'PREFLIGHT FRAMES .*worst=(\d+)ms'
        $Hitches = @(if ($Rev) { $Rev.events | Where-Object { $_ -match '^([\d.]+) s hitch-(\d+)-ms' } | ForEach-Object { [pscustomobject]@{ T = [double]$Matches[1]; Ms = [int]$Matches[2] } } })
        $Longest = $Hitches | Sort-Object Ms -Descending | Select-Object -First 1
        $Over = @($Hitches | Where-Object { $_.Ms -gt 500 }).Count
        $At = { param($L) if ($L -and $T0) { '{0:N1}' -f ((Get-T $L.Line) - $T0).TotalSeconds } else { '-' } }
        $Rows += "| $($P.Name) | $Hash | $(if ($Placed) { $Placed.Matches[0].Groups[1].Value } else { '-' }) | $(if ($Longest) { "$($Longest.Ms) ms at $($Longest.T) s" } elseif ($Worst) { "$($Worst.Matches[0].Groups[1].Value) ms (time not logged)" } else { '-' }) | $Over | $(if ($Model) { ($Model.Line -replace '^\[.*?\]\[.*?\]\w+: (Display: )?', '').Trim() } else { 'no line' }) | $(if ($Pre) { $Pre.frame_p95_ms } else { '-' }) | origin $(& $At $Origin) s, first tile $(& $At $TileFirst) s, last tile $(& $At $TileLast) s, car models $(& $At $ModelsFirst)-$(& $At $ModelsLast) s, traffic line $(& $At $Model) s, placed $(& $At $Placed) s | $(if ($Pre) { "$($Pre.result) $($Pre.reasons)" } else { '-' }) | $Quiet |"
        Write-Host $Rows[-1]
    }
}
$Md = @('# Start compare', '', "Builds in order: $(($Builds | ForEach-Object { (& git -C $_ rev-parse --short HEAD).Trim() }) -join ', '). Off screen, Ultra 75 %, $Seconds s of drive, shaders warmed first, the 2001 roadster.", '',
    '| Place | Build | Launch to car placed (s) | Longest frame in the drive | Frames over 0.5 s | Traffic model line | Frame p95 (ms) | Start phases (s after the log start) | Drive judge | Load |',
    '|---|---|---|---|---|---|---|---|---|---|') + $Rows
$Md | Set-Content -Encoding utf8 (Join-Path $OutRoot 'start-compare.md')
Write-Host "START COMPARE -> $(Join-Path $OutRoot 'start-compare.md')"
}
finally { Clear-GpuBusy }
