<#
.SYNOPSIS
  Fits each car's TMeasy tyre grip to its published braking and skidpad figures.

.DESCRIPTION
  For each car it sets "tyre_model": "tmeasy" (if not yet), runs the CarTrack bench
  (scripts\check.ps1, under the editor lock), and scales the grip levels until the bench's
  70-0 distance and skidpad match the car file's target_70_0_ft and target_skidpad_g within
  Tolerance: the side grip (tmeasy_peak_mu_y, tmeasy_slide_mu_y) by target over measured
  skidpad g, the braking grip (tmeasy_peak_mu_x, tmeasy_slide_mu_x) by measured over target
  distance. Only the tyre_model and tmeasy_* keys of data/cars/<id>.json change; the curve
  shape stays Chrono's passenger-tyre set, or a bias-ply shape with -BiasPly ids.
  The editor binaries must be built.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\fit-tyres.ps1 -Cars crv25,model3
#>
param(
    [Parameter(Mandatory = $true)][string[]]$Cars,
    [string[]]$BiasPly = @('mustang65'),
    [int]$MaxRounds = 4,
    [double]$Tolerance = 0.015
)

$ErrorActionPreference = 'Stop'
# powershell -File passes "a,b,c" as one string.
$Cars = @($Cars | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
$Root = Split-Path -Parent $PSScriptRoot
$Utf8 = [Text.UTF8Encoding]::new($false)

function Set-Key([string]$Text, [string]$Key, [string]$Value) {
    $Pattern = '"' + [regex]::Escape($Key) + '"\s*:\s*("[^"]*"|[-\d.eE+]+)'
    if ($Text -match $Pattern) { return [regex]::Replace($Text, $Pattern, ('"' + $Key + '": ' + $Value), 1) }
    # New keys go after "lateral_grip_scale", which every car file has.
    $Anchor = [regex]::Match($Text, '"lateral_grip_scale"\s*:\s*[-\d.eE+]+,')
    if (-not $Anchor.Success) { throw "no lateral_grip_scale to anchor $Key" }
    $Indent = ([regex]::Match($Text.Substring(0, $Anchor.Index), '[ \t]*$')).Value
    $Nl = if ($Text.Contains("`r`n")) { "`r`n" } else { "`n" }
    return $Text.Insert($Anchor.Index + $Anchor.Length, "$Nl$Indent`"$Key`": $Value,")
}

function Fmt([double]$V) { return $V.ToString('0.####', [Globalization.CultureInfo]::InvariantCulture) }

$State = @{}
foreach ($Car in $Cars) {
    $Path = Join-Path $Root "data\cars\$Car.json"
    $J = Get-Content $Path -Raw | ConvertFrom-Json
    $Mu = [double]$J.static_mu
    $Lat = if ($J.lateral_grip_scale) { [double]$J.lateral_grip_scale } else { 1.0 }
    $S = @{
        Path = $Path
        PeakX = if ($J.tmeasy_peak_mu_x) { [double]$J.tmeasy_peak_mu_x } else { $Mu * $(if ($J.abs_enabled) { 1.0 } else { 1.14 }) }
        SlideX = if ($J.tmeasy_slide_mu_x) { [double]$J.tmeasy_slide_mu_x } else { $Mu * $(if ($J.abs_enabled) { 0.8 } else { 0.95 }) }
        PeakY = if ($J.tmeasy_peak_mu_y) { [double]$J.tmeasy_peak_mu_y } else { $Mu * $Lat }
        SlideY = if ($J.tmeasy_slide_mu_y) { [double]$J.tmeasy_slide_mu_y } else { $Mu * $Lat * 0.833 }
        Target70 = [double]$J.target_70_0_ft
        TargetG = [double]$J.target_skidpad_g
        Done = $false
    }
    $State[$Car] = $S
}

for ($Round = 1; $Round -le $MaxRounds; $Round++) {
    $Pending = @($Cars | Where-Object { -not $State[$_].Done })
    if ($Pending.Count -eq 0) { break }
    foreach ($Car in $Pending) {
        $S = $State[$Car]
        $T = [IO.File]::ReadAllText($S.Path, $Utf8)
        $T = Set-Key $T 'tyre_model' '"tmeasy"'
        $T = Set-Key $T 'tmeasy_peak_mu_x' (Fmt $S.PeakX)
        $T = Set-Key $T 'tmeasy_slide_mu_x' (Fmt $S.SlideX)
        $T = Set-Key $T 'tmeasy_peak_mu_y' (Fmt $S.PeakY)
        $T = Set-Key $T 'tmeasy_slide_mu_y' (Fmt $S.SlideY)
        if ($BiasPly -contains $Car) {
            # A bias-ply tyre: a soft carcass builds side force slowly and peaks late
            # (about 9 per rad and 11-13 deg, against a radial's 16 and 8.5).
            $T = Set-Key $T 'tmeasy_stiffness_y' '9.5'
            $T = Set-Key $T 'tmeasy_peak_slip_y' '0.21'
            $T = Set-Key $T 'tmeasy_stiffness_x' '12.0'
            $T = Set-Key $T 'tmeasy_peak_slip_x' '0.18'
        }
        [IO.File]::WriteAllText($S.Path, $T, $Utf8)
    }
    $Filter = ($Pending | ForEach-Object { "SimCruise.Vehicle.CarTrack.$_" }) -join '+'
    Write-Host "Round $Round : $($Pending -join ', ')"
    & powershell -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'check.ps1') -NoBuild -Filter $Filter | Out-Null
    $Log = Join-Path $Root 'Saved\Logs\SimCruiseChecks.log'
    foreach ($Car in $Pending) {
        $S = $State[$Car]
        $Line = Select-String -Path $Log -Pattern ("CARTRACK " + [regex]::Escape($Car) + "\s+0-60 ([\d.]+) s .*70-0 ([\d.]+) ft .*skidpad ([\d.]+) g") | Select-Object -Last 1
        if (-not $Line) { Write-Host "  $Car : no CARTRACK line"; $S.Done = $true; continue }
        $G = $Line.Matches[0].Groups
        $Ft = [double]$G[2].Value; $Gs = [double]$G[3].Value
        Write-Host ("  {0,-16} 0-60 {1} s  70-0 {2} ft (target {3})  skidpad {4} g (target {5})  mu_x {6}/{7} mu_y {8}/{9}" -f $Car, $G[1].Value, $Ft, $S.Target70, $Gs, $S.TargetG, (Fmt $S.PeakX), (Fmt $S.SlideX), (Fmt $S.PeakY), (Fmt $S.SlideY))
        $ErrX = $Ft / $S.Target70; $ErrY = $S.TargetG / $Gs
        if ([Math]::Abs($ErrX - 1) -le $Tolerance -and [Math]::Abs($ErrY - 1) -le $Tolerance) { $S.Done = $true; continue }
        $S.PeakX *= $ErrX; $S.SlideX *= $ErrX
        $S.PeakY *= $ErrY; $S.SlideY *= $ErrY
    }
}
foreach ($Car in $Cars) { if (-not $State[$Car].Done) { Write-Host "NOT CONVERGED: $Car (last values written)" } }
