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
        $Teams = if ($C.Def.Case -eq 'standstill') { @('world', 'picture') } else { @('world', 'ai', 'picture') }
        $Car = 'nb2001_aaron'
        $Add = { param($Team, $Sev, $Time, $Ev, $What) $Findings[$Team] += [pscustomobject]@{ Sev = $Sev; Place = $Name; Car = $Car; Time = $Time; Evidence = $Ev; What = $What } }
        if (-not $C.Ran) { foreach ($T in $Teams) { $Numbers[$T] += "| $Name | test case | NOT RUN: $($C.Reason) | - | NOT RUN |" }; continue }
        foreach ($T in $Teams) { $Ran[$T] = $true }
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
    param([string]$RunDir, [object[]]$Rows, [string]$Commit, [int]$Seed, [string]$PadResult, [string]$PadDir, [object[]]$Cases = @(), [int]$Minutes)
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
        $View = if (Test-Path $Log) { Select-String -Path $Log -Pattern 'VIEWCHECK (PASS|FAIL) .*?reason=(\S+)' | Select-Object -Last 1 } else { $null }
        $Placed = if (Test-Path $Log) { Select-String -Path $Log -Pattern 'Car placed.*after ([\d.]+) s' | Select-Object -First 1 } else { $null }
        $Engine = if (Test-Path $Log) { @(Select-String -Path $Log -Pattern 'Ensure condition failed|Fatal error|LogSimCruise\w*: Error:') } else { @() }
        $Crash = Test-Path (Join-Path $D 'crash')
        $RunTxt = if (Test-Path (Join-Path $D 'run.txt')) { Get-Content (Join-Path $D 'run.txt') -Raw } else { '' }
        $Hung = $RunTxt -match 'timed_out True'
        foreach ($T in @('driving', 'picture', 'world', 'release')) { $Ran[$T] = $true }
        if ($Ck) { $Ran['cockpit'] = $true }

        # Release: the launch itself.
        if ($Crash) { Add-Finding 'release' 'P0' $R '-' "$Rel\crash\" 'the game crashed' }
        if ($Hung) { Add-Finding 'release' 'P0' $R '-' "$Rel\game.log" 'the game hung and was stopped' }
        if (-not $Pre) { Add-Finding 'release' 'P0' $R '-' "$Rel\game.log" 'no result: the drive did not happen' }
        if ($Engine.Count) { Add-Finding 'release' 'P1' $R '-' "$Rel\game.log" "$($Engine.Count) engine errors, first: $(($Engine[0].Line -replace '^\[.*?\]\[.*?\]', '').Trim())" }

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
            $Numbers['picture'] += "| $Rel | frame p95 / hitches | $($Pre.frame_p95_ms) ms / $($Pre.hitches_over_100ms) | 50 ms / 5 | $(if ([double]$Pre.frame_p95_ms -gt 50 -or [int]$Pre.hitches_over_100ms -gt 5) { 'FAIL' } else { 'ok' }) |"
            # World: the road, pop-in, the start.
            if ($Reasons -match 'road-steps-[^,]*|road-gaps-[^,]*|jolts-at-tile-borders-[^,]*') { Add-Finding 'world' 'P2' $R '-' "$Rel\review\" $Matches[0] }
            if ($Reasons -match 'tile-pop-in-[^,]*|car-pop-in-[^,]*') { Add-Finding 'world' 'P2' $R '-' "$Rel\review\" $Matches[0] }
            if ($Reasons -match 'stuck-traffic-[^,]*') { Add-Finding 'world' 'P2' $R '-' "$Rel\game.log" $Matches[0] }
            if ($Pre.fell_back) { Add-Finding 'world' 'P0' $R '-' "$Rel\review\000-*.png" 'the start fell back to the bundled place' }
            $Numbers['world'] += "| $Rel | road steps / gaps / tile pop-in | $($Pre.road_steps) (max $($Pre.max_step_cm) cm) / $($Pre.road_gaps) / $($Pre.tile_popins) | 0 / 0 / 0 | $(if ([int]$Pre.road_steps + [int]$Pre.road_gaps + [int]$Pre.tile_popins) { 'FAIL' } else { 'ok' }) |"
        }
        if ($View -and $View.Matches[0].Groups[1].Value -eq 'FAIL') { Add-Finding 'world' 'P1' $R '-' "$Rel\game.log" "view check: $($View.Matches[0].Groups[2].Value)" }
        if ($Placed) {
            $Load = [double]$Placed.Matches[0].Groups[1].Value
            $Numbers['world'] += "| $Rel | car placed after | $Load s | 60 s | $(if ($Load -gt 60) { 'FAIL' } else { 'ok' }) |"
            if ($Load -gt 60) { Add-Finding 'world' 'P2' $R '0 s' "$Rel\game.log" "the car was placed $Load s after the start" }
        }
        # Steering as the driver sees it (the drive), and the cockpit test at rest.
        if ($Rev) {
            $St = $Rev.steering
            $Numbers['driving'] += "| $Rel ($($R.Plan.Car)) | steering drawn: ratio, lock to lock, fastest rim | $($St.rim_deg_per_road_wheel_deg) : 1, $($St.lock_to_lock_turns) turns, $($St.max_rim_speed_dps) deg/s | car data $($St.car_data_ratio) : 1, $($St.car_data_lock_to_lock_turns) turns | info |"
            $G = $Rev.gauges
            if ([int]$G.speed_wrong_samples + [int]$G.rpm_wrong_samples -gt 0.05 * 2 * [Math]::Max(1, [int]$G.samples)) { Add-Finding 'cockpit' 'P2' $R '-' "$Rel\review\crops\" "gauges off the true value: speed wrong in $($G.speed_wrong_samples), rpm in $($G.rpm_wrong_samples) of $($G.samples) steady samples" }
        }
        if ($Ck) {
            $Numbers['cockpit'] += "| $Rel ($($R.Plan.Car)) | hub move / rim tilt / needles turned at rest / cluster change | $($Ck.hub_move_cm) cm / $($Ck.rim_tilt_deg) deg / $($Ck.needle_max_turn_at_rest_deg) deg / $($Ck.cluster_changed_percent) % (noise $($Ck.cluster_noise_percent) %) | 0.1 cm / 2 deg / 0.5 deg / noise + 1 % | $($Ck.result) |"
            $Numbers['cockpit'] += "| $Rel ($($R.Plan.Car)) | rim at full lock, lock to lock | $($Ck.rim_full_lock_left_deg) / $($Ck.rim_full_lock_right_deg) deg, $($Ck.lock_to_lock_turns) turns | $($Ck.expected_full_lock_deg) deg (car data) | info |"
            if ($Ck.result -ne 'PASS') { Add-Finding 'cockpit' 'P1' $R 'at rest' "$Rel\cockpit\" $Ck.reasons }
            $Frames['cockpit'] += "$Rel\cockpit\full-left.png, full-left-nowheel.png, diff-full-left.png"
        }
        $Shots = @(Get-ChildItem (Join-Path $D 'review') -Filter '*.png' -ErrorAction SilentlyContinue | Sort-Object Name)
        foreach ($S in ($Shots | Where-Object { $_.Name -notmatch 'timed|start' } | Select-Object -First 4)) { $Frames['world'] += "$Rel\review\$($S.Name)"; $Frames['picture'] += "$Rel\review\$($S.Name)" }
        if ($Shots.Count) { $Frames['world'] += "$Rel\review\$($Shots[0].Name) (start)" }
        if (Test-Path (Join-Path $D 'sheet.png')) { $Frames['picture'] += "$Rel\sheet.png" }

        $Verdict = if (-not $Pre -or $Crash -or $Hung) { 'FAIL' } elseif ($Pre.result -eq 'PASS' -and (-not $Ck -or $Ck.result -eq 'PASS') -and $Valid) { 'PASS' } else { 'FAIL' }
        $Table += "| $($R.Plan.Name) | $($R.Plan.Car) | $Verdict$(if (-not $Valid) { ' (NOT VALID FOR SIGN-OFF)' }) | $(if ($Pre) { $Pre.reasons } else { 'no result' }) | $(if ($Ck) { $Ck.result } else { '-' }) | $(if ($View) { $View.Matches[0].Groups[1].Value } else { '-' }) | $(if ($Pre) { $Pre.frame_p95_ms } else { '-' }) | $($R.Minutes) | [review]($Rel/review.md) |"
    }
    Add-CaseResults -Cases $Cases -Findings $Findings -Numbers $Numbers -Frames $Frames -Ran $Ran
    # UI: the controller menu test.
    if ($PadResult -ne 'NOT RUN') {
        $Ran['ui'] = $true
        if ($PadResult -ne 'PASS') { $Findings['ui'] += [pscustomobject]@{ Sev = 'P1'; Place = 'menus'; Car = '-'; Time = '-'; Evidence = 'pad-review\'; What = 'a control the controller cannot reach, or a focus trap (see pad-review\report.md)' } }
        $Frames['ui'] += 'pad-review\*.png'
    }

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
        '| Place | Car | Verdict | Drive judge | Cockpit | View | Frame p95 (ms) | Minutes | Review |', '|---|---|---|---|---|---|---|---|---|') + $Table
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
