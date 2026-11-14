<#
.SYNOPSIS
  summary.md and one report per team for scripts\release-test.ps1 (dot-source this file).

.DESCRIPTION
  Each team report has the same four parts (docs\team\release-test.md): Verdict (PASS, FAIL or
  NOT RUN), Findings (each with severity, place, car, time, evidence path and what is wrong),
  Frames to look at, and Numbers vs limits. Severity: P0 stops the release (a crash, a hang, a
  start at the wrong place, a run that did not happen), P1 a player sees it at once (a spin, a
  run-off, slow frames, gauges or wheel wrong, an empty view), P2 a player may notice it.
#>

. (Join-Path $PSScriptRoot 'ViewerWatch.ps1')

$script:Teams = [ordered]@{
    driving = 'Driving (physics, the robot judge, steering ratio)'
    cockpit = 'Cockpit (steering wheel, gauges)'
    picture = 'Picture (exposure, frame times, input delay, graphics settings)'
    world   = 'World (terrain, trees, buildings, road gaps, random US places, detail at a stop, detail lead at speed)'
    ai      = 'AI (the detail engine''s workers: frame cost, detail lead at speed)'
    ui      = 'UI (controller menus)'
    release = 'Release (crashes, log errors, launch)'
}

function Read-Json([string]$Path) { if (Test-Path $Path) { return (Get-Content $Path -Raw | ConvertFrom-Json) } return $null }

<# The graphics a launch really had, from its "REVIEW SETTINGS" log line; @{} when the line is not there. #>
function Read-RunSettings([string]$Log) {
    $S = @{}
    if (-not (Test-Path $Log)) { return $S }
    $L = Select-String -Path $Log -Pattern 'REVIEW SETTINGS (.*)' | Select-Object -Last 1
    if ($L) { foreach ($M in [regex]::Matches($L.Matches[0].Groups[1].Value, '(\w[\w.]*)=(\S+)')) { $S[$M.Groups[1].Value] = $M.Groups[2].Value } }
    return $S
}

<#
  The standstill and speed cases (docs\team\release-test.md, "Test cases"). The game writes
  standstill\t000|t005|t015|t060|wall|ground.png (detail.json comes from scripts\detail-metrics.py)
  and speed\speed.json: {"runs": [{"road", "kmh", "ai", "lead_m", "lead_s", "holes",
  "popin_spikes", "frame_median_ms", "frame_p95_ms", "hitches_2x"}]}.
#>
function Add-CaseResults($Cases, $Findings, $Numbers, $Frames, $Ran) {
    foreach ($C in $Cases) {
        $Name = $C.Def.Name
        $Teams = switch ($C.Def.Case) { 'standstill' { @('world', 'picture') } 'fallback' { @('world') } 'cooked-drive' { @('driving', 'picture', 'release') } 'eye' { @('world', 'ai') } 'eyeoff' { @('ai') } default { @('world', 'ai', 'picture') } }
        $Car = 'nb2001_aaron'
        $Add = { param($Team, $Sev, $Time, $Ev, $What) $Findings[$Team] += [pscustomobject]@{ Sev = $Sev; Place = $Name; Car = $Car; Time = $Time; Evidence = $Ev; What = $What } }
        if (-not $C.Ran) { foreach ($T in $Teams) { $Numbers[$T] += "| $Name | test case | NOT RUN: $($C.Reason) | - | NOT RUN |" }; continue }
        foreach ($T in $Teams) { $Ran[$T] = $true }
        if ($C.Def.Case -eq 'cooked-drive') {
            $Log = Join-Path $C.Dir 'game.log'
            $Pre = Read-Json (Join-Path $C.Dir 'preflight.json')
            $Rev = Read-Json (Join-Path $C.Dir 'review.json')
            $Worst = if (Test-Path $Log) { Select-String -Path $Log -Pattern 'PREFLIGHT FRAMES .*worst=(\d+)ms' | Select-Object -Last 1 } else { $null }
            $WorstMs = if ($Worst) { [int]$Worst.Matches[0].Groups[1].Value } else { -1 }
            $Hitches = @(if ($Rev) { $Rev.events | Where-Object { $_ -match '^([\d.]+) s hitch-(\d+)-ms' } | ForEach-Object { [pscustomobject]@{ T = [double]$Matches[1]; Ms = [int]$Matches[2] } } })
            $Longest = $Hitches | Sort-Object Ms -Descending | Select-Object -First 1
            $Trace = Join-Path $C.Dir 'trace.utrace'
            $What = "longest frame $(if ($Longest) { "$($Longest.Ms) ms at drive second $($Longest.T)" } else { "$WorstMs ms (time not logged)" })"
            foreach ($T in 'driving', 'picture', 'release') { $Numbers[$T] += "| $Name | cooked package: longest frame in the first 60 s | $What | 500 ms | $(if ([Math]::Max($WorstMs, $(if ($Longest) { $Longest.Ms } else { 0 })) -gt 500) { 'FAIL' } else { 'ok' }) |" }
            if (-not $Pre) { & $Add 'release' 'P0' '-' "$Name\game.log" 'the cooked package did not drive (no result)' }
            elseif ([Math]::Max($WorstMs, $(if ($Longest) { $Longest.Ms } else { 0 })) -gt 500) {
                & $Add 'driving' 'P1' $(if ($Longest) { "$($Longest.T) s" } else { '-' }) "$Name\$(if (Test-Path $Trace) { 'trace.utrace' } else { 'game.log' })" "a $(if ($Longest) { $Longest.Ms } else { $WorstMs }) ms freeze in the cooked package (limit 500 ms); trace: $(if (Test-Path $Trace) { 'trace.utrace (Unreal Insights)' } else { 'none' })"
            }
            $Shaders = if (Test-Path $Log) { @(Select-String -Path $Log -Pattern 'Preparing Shaders').Count } else { 0 }
            if ($Shaders) { & $Add 'release' 'P1' '-' "$Name\game.log" "the cooked package showed 'Preparing Shaders' ($Shaders log lines)" }
            continue
        }
        if ($C.Def.Case -eq 'fallback') {
            $Log = Join-Path $C.Dir 'game.log'
            $Pre = Read-Json (Join-Path $C.Dir 'preflight.json')
            $Forced = if (Test-Path $Log) { Select-String -Path $Log -Pattern 'Forced tile failures: (\d+)' | Select-Object -First 1 } else { $null }
            $Failed = if (Test-Path $Log) { @(Select-String -Path $Log -Pattern 'forced failure \(-SimCruiseFailTiles\)|Tile \S+ failed').Count } else { 0 }
            $Ground = if (Test-Path $Log) { @(Select-String -Path $Log -Pattern 'Fallback ground under tile').Count } else { 0 }
            $Numbers['world'] += "| $Name | forced tile failures / failed tiles in the log / fallback ground laid | $(if ($Forced) { $Forced.Matches[0].Groups[1].Value } else { 0 }) / $Failed / $Ground | 2 / >0 / >0 | $(if ($Forced -and [int]$Forced.Matches[0].Groups[1].Value -ge 2 -and $Ground -gt 0) { 'ok' } else { 'FAIL' }) |"
            if (-not $Forced) { & $Add 'world' 'P1' '-' "$Name\game.log" 'the forced tile failure did not take effect (no "Forced tile failures" line)' }
            elseif ($Ground -eq 0) { & $Add 'world' 'P1' '-' "$Name\game.log" 'no fallback ground was laid under the failed tiles' }
            if (-not $Pre) { & $Add 'world' 'P1' '-' "$Name\game.log" 'no drive result with failed tiles' }
            elseif ($Pre.result -ne 'PASS') { & $Add 'world' 'P1' '-' "$Name\review\" "the drive over failed tiles failed: $($Pre.reasons)" }
            if ($Pre -and ([int]$Pre.road_gaps -gt 0 -or $Pre.fell_back)) { & $Add 'world' 'P1' '-' "$Name\review\" "holes or a fall-back start with failed tiles: road gaps $($Pre.road_gaps)" }
            foreach ($S in @(Get-ChildItem (Join-Path $C.Dir 'review') -Filter '*.png' -ErrorAction SilentlyContinue | Sort-Object Name | Select-Object -Last 4)) { $Frames['world'] += "$Name\review\$($S.Name) (failed tiles ahead: look for holes and voids)" }
            continue
        }
        if ($C.Def.Case -eq 'eyeoff') { continue }
        if ($C.Def.Case -eq 'eye') {
            $Log = Join-Path $C.Dir 'game.log'
            $On = Read-Json (Join-Path $C.Dir 'preflight.json')
            $OffCase = @($Cases | Where-Object { $_.Def.Case -eq 'eyeoff' -and $_.Ran }) | Select-Object -First 1
            $Off = if ($OffCase) { Read-Json (Join-Path $OffCase.Dir 'preflight.json') } else { $null }
            $Ready = if (Test-Path $Log) { Select-String -Path $Log -Pattern 'Scan eye ready: (\S+)' | Select-Object -First 1 } else { $null }
            $Tiles = if (Test-Path $Log) { @(Select-String -Path $Log -Pattern 'SCANEYE eye=\S+ tile=(\S+) ms=([\d.]+)') } else { @() }
            $MeanMs = if ($Tiles.Count) { [Math]::Round(($Tiles | ForEach-Object { [double]$_.Matches[0].Groups[2].Value } | Measure-Object -Average).Average, 0) } else { 0 }
            foreach ($T in 'world', 'ai') { $Numbers[$T] += "| $Name | scan eye ready / tiles read / mean ms per tile | $(if ($Ready) { $Ready.Matches[0].Groups[1].Value } else { 'no' }) / $($Tiles.Count) / $MeanMs | yes / >0 / - | $(if ($Ready -and $Tiles.Count) { 'ok' } else { 'FAIL' }) |" }
            if (-not $Ready) { & $Add 'ai' 'P1' '-' "$Name\game.log" 'the scan eye did not start (no "Scan eye ready" line)' }
            elseif (-not $Tiles.Count) { & $Add 'ai' 'P1' '-' "$Name\game.log" 'the scan eye read no tiles (no SCANEYE lines)' }
            if ($On -and $Off) {
                $Dp = [double]$On.frame_p95_ms - [double]$Off.frame_p95_ms
                $Dh = [int]$On.hitches_over_100ms - [int]$Off.hitches_over_100ms
                $Numbers['ai'] += "| $Name | frame p95 eye on - off / extra hitches | $('{0:N2}' -f $Dp) ms ($($On.frame_p95_ms) vs $($Off.frame_p95_ms)) / $Dh | 0.5 ms / 0 | $(if ($Dp -gt 0.5 -or $Dh -gt 0) { 'FAIL' } else { 'ok' }) |"
                if ($Dp -gt 0.5) { & $Add 'ai' 'P1' 'whole drive' "$Name\game.log" "the scan eye adds $('{0:N2}' -f $Dp) ms to the frame p95 (limit 0.5 ms)" }
                if ($Dh -gt 0) { & $Add 'ai' 'P2' 'whole drive' "$Name\game.log" "the scan eye adds $Dh hitches over 100 ms" }
            } else { $Numbers['ai'] += "| $Name | frame p95 eye on - off | no $(if (-not $On) { 'eye-on' } else { 'eye-off' }) result | 0.5 ms | FAIL |"; & $Add 'ai' 'P1' '-' "$Name\" 'no eye-on or eye-off drive result to compare' }
            # No ensure, assert or crash with the eye on.
            $Bad = if (Test-Path $Log) { @(Select-String -Path $Log -Pattern 'Ensure condition failed|ensure\(|Assertion failed|Fatal error|Unhandled Exception') } else { @() }
            $Crash = Test-Path (Join-Path $C.Dir 'crash')
            $Numbers['ai'] += "| $Name | ensures, asserts, crashes with the eye on | $($Bad.Count)$(if ($Crash) { ' + crash' }) | 0 | $(if ($Bad.Count -or $Crash) { 'FAIL' } else { 'ok' }) |"
            if ($Bad.Count -or $Crash) { & $Add 'ai' 'P0' '-' "$Name\game.log" "$($Bad.Count) ensure/assert lines$(if ($Crash) { ' and a crash' }) with the eye on, first: $(if ($Bad.Count) { ($Bad[0].Line -replace '^\[.*?\]\[.*?\]', '').Trim() } else { 'crash' })" }
            foreach ($S in @(Get-ChildItem (Join-Path $C.Dir 'review') -Filter '*timed*.png' -ErrorAction SilentlyContinue | Sort-Object Name | Select-Object -First 3)) { $Frames['world'] += "$Name\review\$($S.Name) (the eye's detail layer)"; $Frames['ai'] += "$Name\review\$($S.Name)" }
            continue
        }
        if ($C.Def.Case -eq 'standstill') {
            $D = Read-Json (Join-Path $C.Dir 'standstill\detail.json')
            if (-not $D) { & $Add 'world' 'P1' '-' "$Name\game.log" 'no standstill frames (t000..t060)'; continue }
            $Vals = (@($D.frames) | ForEach-Object { '{0:N4} / {1:N5}' -f $_.edge_density, $_.fine_energy }) -join ' -> '
            foreach ($T in 'world', 'picture') { $Numbers[$T] += "| $Name | edge density / fine-detail energy at 0, 5, 15, 60 s | $Vals | rises at each step | $(if ($D.rising) { 'ok' } else { 'FAIL' }) |" }
            if (-not $D.rising) { foreach ($T in 'world', 'picture') { & $Add $T 'P2' '0-60 s after the stop' "$Name\standstill\" 'detail does not rise at each step after the stop' } }
            foreach ($F in 't000', 't060', 'wall', 'ground', 'side-by-side') { if (Test-Path (Join-Path $C.Dir "standstill\$F.png")) { $Frames['world'] += "$Name\standstill\$F.png"; $Frames['picture'] += "$Name\standstill\$F.png" } }
            if (-not (Test-Path (Join-Path $C.Dir 'standstill\side-by-side.png'))) { $Numbers['world'] += "| $Name | reference photo | none in data\reference\ | - | info |" }
            continue
        }
        $S = Read-Json (Join-Path $C.Dir 'speed\speed.json')
        if (-not $S) { & $Add 'world' 'P1' '-' "$Name\game.log" 'no speed\speed.json'; continue }
        foreach ($R in @($S.runs)) {
            $Tag = "$($R.road) $($R.kmh) km/h AI $(if ($R.ai) { 'on' } else { 'off' })"
            $Numbers['world'] += "| $Name | $Tag lead / holes / road pop-in | $($R.lead_m) m, $($R.lead_s) s / $($R.holes) / $($R.popin_spikes) | - / 0 / 0 | $(if ([int]$R.holes + [int]$R.popin_spikes) { 'FAIL' } else { 'ok' }) |"
            $Numbers['picture'] += "| $Name | $Tag frame median / p95 / hitches over 2x median | $($R.frame_median_ms) / $($R.frame_p95_ms) ms / $($R.hitches_2x) | - / 50 ms / 0 | $(if ([double]$R.frame_p95_ms -gt 50 -or [int]$R.hitches_2x -gt 0) { 'FAIL' } else { 'ok' }) |"
            if ([int]$R.holes) { & $Add 'world' 'P1' $Tag "$Name\review\" "$($R.holes) holes in the world ahead" }
            if ([int]$R.popin_spikes) { & $Add 'world' 'P2' $Tag "$Name\review\" "$($R.popin_spikes) pop-in spikes on the road ahead" }
            if ([int]$R.hitches_2x) { & $Add 'picture' 'P2' $Tag "$Name\game.log" "$($R.hitches_2x) frames over 2x the median" }
            if ([double]$R.frame_p95_ms -gt 50) { & $Add 'picture' 'P1' $Tag "$Name\game.log" "frame p95 $($R.frame_p95_ms) ms" }
        }
        # AI workers on against off on the same road and speed: p95 no more than 0.5 ms higher on the Dragon, no extra hitches.
        foreach ($G in (@($S.runs) | Group-Object { "$($_.road) $($_.kmh)" })) {
            $On = @($G.Group | Where-Object { $_.ai }) | Select-Object -First 1
            $Off = @($G.Group | Where-Object { -not $_.ai }) | Select-Object -First 1
            if (-not ($On -and $Off)) { continue }
            $Dp = [double]$On.frame_p95_ms - [double]$Off.frame_p95_ms
            $Dh = [int]$On.hitches_2x - [int]$Off.hitches_2x
            $Lim = if ($On.road -match 'dragon') { 0.5 } else { 999 }
            $Numbers['ai'] += "| $Name | $($G.Name) km/h: p95 AI on - off / extra hitches | $('{0:N2}' -f $Dp) ms / $Dh | $(if ($Lim -lt 999) { '0.5 ms' } else { 'info' }) / 0 | $(if ($Dp -gt $Lim -or $Dh -gt 0) { 'FAIL' } else { 'ok' }) |"
            $Numbers['ai'] += "| $Name | $($G.Name) km/h: detail lead AI on / off | $($On.lead_m) / $($Off.lead_m) m | - | info |"
            if ($Dp -gt $Lim) { & $Add 'ai' 'P1' "$($G.Name) km/h" "$Name\speed\speed.json" "AI workers add $('{0:N2}' -f $Dp) ms to the frame p95 (limit 0.5 ms)" }
            if ($Dh -gt 0) { & $Add 'ai' 'P2' "$($G.Name) km/h" "$Name\speed\speed.json" "AI workers add $Dh hitches over 2x the median" }
        }
    }
}

function Write-ReleaseReports {
    param([string]$RunDir, [object[]]$Rows, [string]$Commit, [int]$Seed, [string]$PadResult, [string]$PadDir, [object[]]$Cases = @(), $Load = $null, [int]$Minutes)
    $Findings = @{}; foreach ($T in $script:Teams.Keys) { $Findings[$T] = @() }
    $Numbers = @{}; foreach ($T in $script:Teams.Keys) { $Numbers[$T] = @() }
    $Frames = @{}; foreach ($T in $script:Teams.Keys) { $Frames[$T] = @() }
    $Ran = @{}; foreach ($T in $script:Teams.Keys) { $Ran[$T] = $false }
    $Table = @()
    $Invalid = @()
    function Add-Finding($Team, $Sev, $Row, $Time, $Evidence, $What) {
        $Findings[$Team] += [pscustomobject]@{ Sev = $Sev; Place = $Row.Plan.Name; Car = $Row.Plan.Car; Time = $Time; Evidence = $Evidence; What = $What }
    }
    foreach ($R in $Rows) {
        $D = $R.Dir
        $Rel = Split-Path -Leaf $D
        $Log = Join-Path $D 'game.log'
        $Pre = Read-Json (Join-Path $D 'preflight.json')
        $Rev = Read-Json (Join-Path $D 'review.json')
        $Ck = Read-Json (Join-Path $D 'cockpit\cockpit.json')
        $Set = Read-RunSettings $Log
        $Disturb = if (Test-Path $Log) { @(Select-String -Path $Log -Pattern 'REVIEW (FOCUS|USERINPUT) t=([\d.]+) (\S+)') } else { @() }
        $GameDisturbCount = $Disturb.Count
        # Seen from outside the game (viewer.csv), for builds that do not log it themselves.
        $Outside = if ($R.FromUtc -and (Test-Path (Join-Path $RunDir 'viewer.csv'))) { @(Get-ViewerEvents $RunDir $R.FromUtc.AddSeconds(20) $R.ToUtc) } else { @() }
        if ($Outside.Count -and -not $Disturb.Count) { $Numbers['picture'] += "| $Rel | DISTURBED (viewer.csv): $($Outside.Count) events, first $($Outside[0].event) $($Outside[0].detail) at $($Outside[0].utc) | - | none | DISTURBED |" }
        if ($Outside.Count) { $Disturb = @($Disturb) + @($Outside) }
        # Other lanes' Unreal processes while this place ran (load.csv): its frame times do not count either.
        $LoadRows = if ($R.FromUtc -and (Test-Path (Join-Path $RunDir 'load.csv'))) { @(Import-Csv (Join-Path $RunDir 'load.csv') | Where-Object { $_.other_processes -and ([timespan]$_.time) -ge $R.FromUtc.AddSeconds(20).TimeOfDay -and ([timespan]$_.time) -le $R.ToUtc.TimeOfDay }) } else { @() }
        if ($LoadRows.Count) {
            $Numbers['picture'] += "| $Rel | DISTURBED (other lane load): $($LoadRows.Count) samples with $((@($LoadRows | ForEach-Object { $_.other_processes -split ' ' } | Select-Object -Unique)) -join ', ') | - | none | DISTURBED |"
            $Disturb = @($Disturb) + @($LoadRows)
        }
        $R | Add-Member -NotePropertyName Disturbed -NotePropertyValue ($Disturb.Count -gt 0) -Force
        $Visible = (Test-Path (Join-Path $D 'cmdline.txt')) -and ((Get-Content (Join-Path $D 'cmdline.txt') -Raw) -match '-windowed|-FULLSCREEN')
        if ($GameDisturbCount) { $Numbers['picture'] += "| $Rel | DISTURBED: $GameDisturbCount focus changes or viewer inputs logged by the game | - | none | DISTURBED |" }
        $View = if (Test-Path $Log) { Select-String -Path $Log -Pattern 'VIEWCHECK (PASS|FAIL) .*?reason=(\S+)' | Select-Object -Last 1 } else { $null }
        $Placed = if (Test-Path $Log) { Select-String -Path $Log -Pattern 'Car placed.*after ([\d.]+) s' | Select-Object -First 1 } else { $null }
        $Engine = if (Test-Path $Log) { @(Select-String -Path $Log -Pattern 'Ensure condition failed|Fatal error|LogSimCruise\w*: Error:') } else { @() }
        $Crash = Test-Path (Join-Path $D 'crash')
        $CookedRun = (Test-Path (Join-Path $D 'cmdline.txt')) -and -not ((Get-Content (Join-Path $D 'cmdline.txt') -Raw) -match 'UnrealEditor')
        $ShaderLines = if (Test-Path $Log) { @(Select-String -Path $Log -Pattern 'Preparing Shaders').Count } else { 0 }
        $RevEv = Read-Json (Join-Path $D 'review.json')
        $BigFreeze = @(if ($RevEv) { $RevEv.events | Where-Object { $_ -match 'hitch-(\d+)-ms' -and [int]$Matches[1] -gt 1000 } })
        $RunTxt = if (Test-Path (Join-Path $D 'run.txt')) { Get-Content (Join-Path $D 'run.txt') -Raw } else { '' }
        $Hung = $RunTxt -match 'timed_out True'
        foreach ($T in @('driving', 'picture', 'world', 'release')) { $Ran[$T] = $true }
        if ($Ck) { $Ran['cockpit'] = $true }

        # Release: the launch itself.
        if ($Crash) { Add-Finding 'release' 'P0' $R '-' "$Rel\crash\" 'the game crashed' }
        if ($Hung) { Add-Finding 'release' 'P0' $R '-' "$Rel\game.log" 'the game hung and was stopped' }
        if (-not $Pre) { Add-Finding 'release' 'P0' $R '-' "$Rel\game.log" 'no result: the drive did not happen' }
        if ($Engine.Count) { Add-Finding 'release' 'P1' $R '-' "$Rel\game.log" "$($Engine.Count) engine errors, first: $(($Engine[0].Line -replace '^\[.*?\]\[.*?\]', '').Trim())" }

        if ($ShaderLines -or $BigFreeze.Count) {
            if ($CookedRun) { Add-Finding 'release' 'P1' $R '-' "$Rel\game.log" "cooked package: $(if ($ShaderLines) { "'Preparing Shaders' ($ShaderLines log lines)" }) $(if ($BigFreeze.Count) { "freeze over 1 s: $($BigFreeze -join '; ')" })" }
            else {
                $FreezeShot = @(Get-ChildItem (Join-Path $D 'review') -Filter '*hitch*.png' -ErrorAction SilentlyContinue | Sort-Object Name | Select-Object -First 1)
                Add-Finding 'picture' 'P3' $R $(if ($BigFreeze.Count) { ($BigFreeze[0] -split ' s ')[0] + ' s' } else { '-' }) $(if ($FreezeShot.Count) { "$Rel\review\$($FreezeShot[0].Name)" } else { "$Rel\game.log" }) "TEST ENV if the frame shows 'Preparing Shaders': the editor build compiles shaders during a drive ($($BigFreeze -join '; ')); the cooked run judges it"
            }
        }
        # Graphics: what the launch really had.
        $Valid = $Set.ContainsKey('valid') -and $Set['valid'] -eq 'yes'
        if (-not $Valid) { $Invalid += $Rel; Add-Finding 'picture' 'P1' $R '-' "$Rel\game.log" "NOT VALID FOR SIGN-OFF: $(if ($Set.Count) { "preset $($Set['preset']), $($Set['resolution']), screen $($Set['screen_pct']) % (needs $($Set['needs']))" } else { 'no REVIEW SETTINGS line' })" }
        # Dynamic resolution can hold the 3D picture far below the preset's percentage on this GPU.
        if ($Set.ContainsKey('mean_actual_pct') -and [double]$Set['mean_actual_pct'] -lt [double]$Set['preset_pct'] - 5) {
            Add-Finding 'picture' 'P1' $R 'whole drive' "$Rel\review\" "the 3D picture was drawn at a mean $($Set['mean_actual_pct']) % (min $($Set['actual_min']) %), below the preset's $($Set['preset_pct']) % (dynamic resolution $($Set['dynres'])): soft trees and mirrors"
            $Numbers['picture'] += "| $Rel | mean actual screen percentage | $($Set['mean_actual_pct']) % (dynamic $($Set['min'])-$($Set['max']) %) | $($Set['preset_pct']) % | FAIL |"
        }        if ($Set.Count) { $Numbers['picture'] += "| $Rel | graphics | preset $($Set['preset']), $($Set['resolution']), screen $($Set['screen_pct']) %, dynamic resolution $($Set['dynres']) | top preset at the screen's resolution | $(if ($Valid) { 'valid' } else { 'NOT VALID' }) |" }

        if ($Pre) {
            $Reasons = [string]$Pre.reasons
            # Driving: the robot judge.
            if ($Reasons -match 'spin-[^,]*') { Add-Finding 'driving' 'P1' $R '-' "$Rel\review\" $Matches[0] }
            if ($Reasons -match 'ran-off-[^,]*|left-the-road-[^,]*') { Add-Finding 'driving' 'P1' $R '-' "$Rel\review\" $Matches[0] }
            if ($Reasons -match 'robot-moved-only-[^,]*') { Add-Finding 'driving' 'P1' $R '-' "$Rel\video.mp4" $Matches[0] }
            $Numbers['driving'] += "| $Rel ($($R.Plan.Car)) | rear slip / backwards | $($Pre.peak_rear_slip_deg) deg / $($Pre.backwards_seconds) s | 15 deg / 0.3 s | $(if ([double]$Pre.peak_rear_slip_deg -gt 15) { 'FAIL' } else { 'ok' }) |"
            # Picture: frame times and the sky.
            if ($Reasons -match 'slow-frames-[^,]*|hitches-[^,]*') { Add-Finding 'picture' 'P1' $R '-' "$Rel\game.log" $Matches[0] }
            if ($Reasons -match 'white-sky-[^,]*|flash-frames-[^,]*') { Add-Finding 'picture' 'P2' $R '-' "$Rel\video.mp4" $Matches[0] }
            $Numbers['picture'] += "| $Rel | frame p95 / hitches (display: $(if ($Visible) { 'windowed (visible), about +1 ms against off screen' } else { 'off screen' })) | $($Pre.frame_p95_ms) ms / $($Pre.hitches_over_100ms) | 50 ms / 5 | $(if ([double]$Pre.frame_p95_ms -gt 50 -or [int]$Pre.hitches_over_100ms -gt 5) { 'FAIL' } else { 'ok' })$(if ($Disturb.Count) { ', DISTURBED' }) |"
            # World: the road, pop-in, the start.
            if ($Reasons -match 'road-steps-[^,]*|road-gaps-[^,]*|jolts-at-tile-borders-[^,]*') { Add-Finding 'world' 'P2' $R '-' "$Rel\review\" $Matches[0] }
            if ($Reasons -match 'tile-pop-in-[^,]*|car-pop-in-[^,]*') { Add-Finding 'world' 'P2' $R '-' "$Rel\review\" $Matches[0] }
            if ($Reasons -match 'stuck-traffic-[^,]*') { Add-Finding 'world' 'P2' $R '-' "$Rel\game.log" $Matches[0] }
            if ($Pre.fell_back) { Add-Finding 'world' 'P0' $R '-' "$Rel\review\000-*.png" 'the start fell back to the bundled place' }
            $Numbers['world'] += "| $Rel | road steps / gaps / tile pop-in | $($Pre.road_steps) (max $($Pre.max_step_cm) cm) / $($Pre.road_gaps) / $($Pre.tile_popins) | 0 / 0 / 0 | $(if ([int]$Pre.road_steps + [int]$Pre.road_gaps + [int]$Pre.tile_popins) { 'FAIL' } else { 'ok' }) |"
        }
        if ($View -and $View.Matches[0].Groups[1].Value -eq 'FAIL') { Add-Finding 'world' 'P1' $R '-' "$Rel\game.log" "view check: $($View.Matches[0].Groups[2].Value)" }
        if ($Placed) {
            $PlacedS = [double]$Placed.Matches[0].Groups[1].Value
            $Numbers['world'] += "| $Rel | car placed after | $PlacedS s | 60 s | $(if ($PlacedS -gt 60) { 'FAIL' } else { 'ok' }) |"
            if ($PlacedS -gt 60) { Add-Finding 'world' 'P2' $R '0 s' "$Rel\game.log" "the car was placed $PlacedS s after the start" }
        }
        # Steering as the driver sees it (the drive), and the cockpit test at rest.
        if ($Rev) {
            $St = $Rev.steering
            $Numbers['driving'] += "| $Rel ($($R.Plan.Car)) | steering drawn: ratio, lock to lock, fastest rim | $($St.rim_deg_per_road_wheel_deg) : 1, $($St.lock_to_lock_turns) turns, $($St.max_rim_speed_dps) deg/s | car data $($St.car_data_ratio) : 1, $($St.car_data_lock_to_lock_turns) turns | info |"
            $G = $Rev.gauges
            if ([int]$G.speed_wrong_samples + [int]$G.rpm_wrong_samples -gt 0.05 * 2 * [Math]::Max(1, [int]$G.samples)) { Add-Finding 'cockpit' 'P2' $R '-' "$Rel\review\crops\" "gauges off the true value: speed wrong in $($G.speed_wrong_samples), rpm in $($G.rpm_wrong_samples) of $($G.samples) steady samples" }
        }
        if ($Ck) {
            $Numbers['cockpit'] += "| $Rel ($($R.Plan.Car)) | axis move / rim centre off axis / rim tilt / needles turned at rest / cluster change | $($Ck.hub_move_cm) cm / $($Ck.rim_axis_offset_cm) cm / $($Ck.rim_tilt_deg) deg / $($Ck.needle_max_turn_at_rest_deg) deg / $($Ck.cluster_changed_percent) % (noise $($Ck.cluster_noise_percent) %) | 0.1 cm / 0.5 cm / 2 deg / 0.5 deg / info | $($Ck.result) |"
            $Numbers['cockpit'] += "| $Rel ($($R.Plan.Car)) | rim at full lock, lock to lock | $($Ck.rim_full_lock_left_deg) / $($Ck.rim_full_lock_right_deg) deg, $($Ck.lock_to_lock_turns) turns | $($Ck.expected_full_lock_deg) deg (car data) | info |"
            if ($Ck.result -ne 'PASS') { Add-Finding 'cockpit' 'P1' $R 'at rest' "$Rel\cockpit\" $Ck.reasons }
            $Frames['cockpit'] += "$Rel\cockpit\full-left.png, full-left-nowheel.png, diff-full-left.png"
        }
        $Shots = @(Get-ChildItem (Join-Path $D 'review') -Filter '*.png' -ErrorAction SilentlyContinue | Sort-Object Name)
        foreach ($S in ($Shots | Where-Object { $_.Name -notmatch 'timed|start' } | Select-Object -First 4)) { $Frames['world'] += "$Rel\review\$($S.Name)"; $Frames['picture'] += "$Rel\review\$($S.Name)" }
        if ($Shots.Count) { $Frames['world'] += "$Rel\review\$($Shots[0].Name) (start)" }
        if (Test-Path (Join-Path $D 'sheet.png')) { $Frames['picture'] += "$Rel\sheet.png" }

        $Verdict = if (-not $Pre -or $Crash -or $Hung) { 'FAIL' } elseif ($Pre.result -eq 'PASS' -and (-not $Ck -or $Ck.result -eq 'PASS') -and $Valid) { 'PASS' } else { 'FAIL' }
        $Table += "| $($R.Plan.Name) | $($R.Plan.Car) | $Verdict$(if (-not $Valid) { ' (NOT VALID FOR SIGN-OFF)' }) | $(if ($Pre) { $Pre.reasons } else { 'no result' }) | $(if ($Ck) { $Ck.result } else { '-' }) | $(if ($View) { $View.Matches[0].Groups[1].Value } else { '-' }) | $(if ($Pre) { $Pre.frame_p95_ms } else { '-' }) | $($R.Minutes) | $(if ($Visible) { 'windowed (visible)' } else { 'off screen' })$(if ($Disturb.Count) { ', DISTURBED' }) | [review]($Rel/review.md) |"
    }
    Add-CaseResults -Cases $Cases -Findings $Findings -Numbers $Numbers -Frames $Frames -Ran $Ran
    # UI: the controller menu test.
    if ($PadResult -ne 'NOT RUN') {
        $Ran['ui'] = $true
        if ($PadResult -ne 'PASS') { $Findings['ui'] += [pscustomobject]@{ Sev = 'P1'; Place = 'menus'; Car = '-'; Time = '-'; Evidence = 'pad-review\'; What = 'a control the controller cannot reach, or a focus trap (see pad-review\report.md)' } }
        $Frames['ui'] += 'pad-review\*.png'
    }

    $NotQuiet = $Load -and -not $Load.Quiet
    if ($NotQuiet) { foreach ($T in 'picture', 'ai') { $Numbers[$T] = @($Numbers[$T] | ForEach-Object { if ($_ -match 'frame|p95') { $_ -replace '\| (ok|FAIL|info) \|$', '| $1, NOT QUIET |' } else { $_ } }) } }
    $Overall = 'PASS'
    foreach ($T in $script:Teams.Keys) {
        $F = @($Findings[$T] | Sort-Object Sev)
        $V = if (-not $Ran[$T]) { 'NOT RUN' } elseif ($F.Count) { 'FAIL' } else { 'PASS' }
        if ($V -eq 'FAIL') { $Overall = 'FAIL' }
        $Md = @("# $($script:Teams[$T])", '', "Release test $(Split-Path -Leaf $RunDir), commit $Commit, seed $Seed.", '', '## Verdict', '', $V, '', '## Findings', '')
        if ($F.Count) {
            $Md += @('| Severity | Place | Car | Time | Evidence | What is wrong |', '|---|---|---|---|---|---|')
            foreach ($X in $F) { $Md += "| $($X.Sev) | $($X.Place) | $($X.Car) | $($X.Time) | $($X.Evidence) | $(([string]$X.What) -replace '\|', '/') |" }
        } else { $Md += $(if ($V -eq 'NOT RUN') { 'Not run in this release test.' } else { 'None.' }) }
        $Md += @('', '## Frames to look at', '')
        $Md += $(if ($Frames[$T].Count) { @($Frames[$T] | Select-Object -Unique | ForEach-Object { "- $_" }) } else { @('None.') })
        $Md += @('', '## Numbers vs limits', '')
        if ($Numbers[$T].Count) { $Md += @('| Run | Measure | Value | Limit | Result |', '|---|---|---|---|---|') + $Numbers[$T] } else { $Md += 'None.' }
        if ($T -eq 'picture') { $Md += @('', 'Input delay: not measured in this test (scripts\latency probe runs on its own).') }
        $Md | Set-Content -Encoding utf8 (Join-Path $RunDir "teams\$T.md")
    }
    if ($Invalid.Count) { $Overall = 'FAIL' }
    $Sum = @("# Release test $(Split-Path -Leaf $RunDir)", '', "Commit $Commit, seed $Seed, $($Rows.Count) places, $Minutes min. Verdict: **$Overall**$(if ($Invalid.Count) { " (NOT VALID FOR SIGN-OFF: $($Invalid -join ', '))" }).", '',
        '| Place | Car | Verdict | Drive judge | Cockpit | View | Frame p95 (ms) | Minutes | Display | Review |', '|---|---|---|---|---|---|---|---|---|---|') + $Table
    $Sum += @('', "Load on the PC (load.csv): $(if (-not $Load) { 'not recorded' } elseif ($Load.Quiet) { "quiet ($($Load.Samples) samples, other GPU mean $($Load.MeanOtherGpu) %)" } else { "**NOT QUIET**: other lanes $(if ($Load.Lanes) { $Load.Lanes } else { '-' }); processes $($Load.OtherProcesses), other GPU mean $($Load.MeanOtherGpu) % (max $($Load.MaxOtherGpu) %). The frame times are not valid for sign-off." })")
    $Sum += @('', '## Teams', '')
    foreach ($T in $script:Teams.Keys) {
        $C = @($Findings[$T]).Count
        $Sum += "- [$($script:Teams[$T])](teams/$T.md): $(if (-not $Ran[$T]) { 'NOT RUN' } elseif ($C) { "FAIL, $C findings" } else { 'PASS' })"
    }
    $Sum += @('', '## Test cases', '')
    foreach ($C in $Cases) { $Sum += "- $($C.Def.Name): $(if ($C.Ran) { "ran in $($C.Minutes) min, see the team reports" } else { "NOT RUN ($($C.Reason))" })" }
    $Sum += @('', "Controller menus: $PadResult.", '', 'Frame review: each place''s review\ frames and review.md; scripts\review-check.ps1 on this folder records the reviewed result.')
    $Sum | Set-Content -Encoding utf8 (Join-Path $RunDir 'summary.md')
}
