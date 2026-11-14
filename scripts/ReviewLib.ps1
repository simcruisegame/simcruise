<#
.SYNOPSIS
  Evidence and a review sheet for every game a script launches (dot-source this file).

.DESCRIPTION
  A test that only prints numbers can pass while the picture is wrong. Each launch keeps, in one
  run folder: the game log, the command line, the commit, the GPU and its driver, a crash's
  Saved\Crashes folder (minidump and CrashContext), and, from the pre-flight robot with
  -SimCruiseReview, full-size frames at the start, every 5 s and at each event, with a JSON
  sidecar each and close-up crops (steering wheel, instrument cluster, mirrors, windscreen).
  review.md lists a fixed set of faults. The numbers fill some verdicts; a person (or a Claude
  reviewer) looks at the frames and fills the others. A launch is reviewed only when every item
  has a verdict: PASS, FAIL or N/A.

  Functions:
    Get-ReviewRoot          Saved\Review\<yyyyMMdd-HHmm>-<commit> for this checkout
    Save-ReviewEvidence     log, command line, commit, GPU, crash folder -> the run folder
    New-ReviewCrops         close-ups from the sidecars' boxes (ffmpeg)
    Write-ReviewSheet       review.md with the fault list (numbers filled where they decide)
    Test-ReviewSheet        is every item decided? which items FAIL?
#>

$script:ReviewItems = @(
    @{ Key = 'launch';    Name = 'Launch: no crash, no hang, the game quit itself' },
    @{ Key = 'place';     Name = 'Right place, world loaded (not an empty or test-pad view)' },
    @{ Key = 'glitches';  Name = 'Glitches or flicker' },
    @{ Key = 'popin';     Name = 'Pop-in (tiles, buildings, cars appear near the car)' },
    @{ Key = 'objects';   Name = 'Missing or floating objects' },
    @{ Key = 'sky';       Name = 'White or black sky, exposure' },
    @{ Key = 'road';      Name = 'Road seams, gaps, steps' },
    @{ Key = 'gauges';    Name = 'Gauges: needles against the true speed and engine speed' },
    @{ Key = 'steering';  Name = 'Steering wheel: direction, ratio, smooth motion' },
    @{ Key = 'cockpit';   Name = 'Cockpit: gauges fixed while steering' },
    @{ Key = 'column';    Name = 'Steering wheel turns about its column' },
    @{ Key = 'car';       Name = 'Car body and wheels (clipping, wheels not turning or rolling)' },
    @{ Key = 'mirrors';   Name = 'Mirrors and windscreen view' },
    @{ Key = 'hud';       Name = 'HUD (readouts, place card, overlaps)' },
    @{ Key = 'traffic';   Name = 'Traffic (stuck, through each other, wrong lane)' },
    @{ Key = 'audio';     Name = 'Audio errors in the log' }
)

function Get-ReviewCommit([string]$Root) {
    $Commit = (& git -C $Root rev-parse --short HEAD).Trim()
    if (& git -C $Root status --porcelain --untracked-files=no -- Source Config Content) { $Commit += '-dirty' }
    return $Commit
}

function Get-ReviewRoot([string]$Root, [string]$Stamp = (Get-Date -Format 'yyyyMMdd-HHmm')) {
    return (Join-Path $Root "Saved\Review\$Stamp-$(Get-ReviewCommit $Root)")
}

function Find-Ffmpeg {
    $F = (Get-Command ffmpeg -ErrorAction SilentlyContinue).Source
    if (-not $F) {
        $Found = Get-ChildItem "$env:LOCALAPPDATA\Microsoft\WinGet\Packages" -Recurse -Filter ffmpeg.exe -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($Found) { $F = $Found.FullName }
    }
    return $F
}

<#
  Keeps what a reviewer needs next to the frames. Returns @{ Crashed; CrashDirs; Hung }.
  -Log: the game log (copied in when it is outside RunDir). -Started: when the game started (crash
  folders newer than this are this run's). -TimedOut: the script stopped the game.
#>
function Save-ReviewEvidence {
    param([string]$RunDir, [string]$Root, [string]$Log, [string]$CommandLine, [datetime]$Started, [bool]$TimedOut, [int]$ExitCode = 0)
    New-Item -ItemType Directory -Force $RunDir | Out-Null
    $CommandLine | Set-Content -Encoding utf8 (Join-Path $RunDir 'cmdline.txt')
    @("commit $(Get-ReviewCommit $Root)", "branch $((& git -C $Root branch --show-current).Trim())", "root $Root", "started $($Started.ToString('s'))", "exit $ExitCode", "timed_out $TimedOut") |
        Set-Content -Encoding utf8 (Join-Path $RunDir 'run.txt')
    $Gpu = @()
    foreach ($V in (Get-CimInstance Win32_VideoController -ErrorAction SilentlyContinue)) { $Gpu += "$($V.Name), driver $($V.DriverVersion)" }
    if ($Log -and (Test-Path $Log)) {
        $Gpu += (Select-String -Path $Log -Pattern 'Adapter Name|Driver Version|RHI.*Adapter|Using Default RHI|Chosen D3D12 Adapter' | Select-Object -First 6 | ForEach-Object { ($_.Line -replace '^\[.*?\]\[.*?\]', '').Trim() })
        $Dest = Join-Path $RunDir 'game.log'
        if ((Resolve-Path $Log).Path -ne $Dest) { Copy-Item $Log $Dest -Force }
    }
    $Gpu | Set-Content -Encoding utf8 (Join-Path $RunDir 'gpu.txt')
    $CrashDirs = @()
    $Crashes = Join-Path $Root 'Saved\Crashes'
    if (Test-Path $Crashes) {
        foreach ($D in (Get-ChildItem $Crashes -Directory | Where-Object { $_.LastWriteTime -ge $Started })) {
            $To = Join-Path $RunDir "crash\$($D.Name)"
            New-Item -ItemType Directory -Force $To | Out-Null
            Copy-Item (Join-Path $D.FullName '*') $To -Recurse -Force
            $CrashDirs += $To
        }
    }
    $LogText = if ($Log -and (Test-Path $Log)) { Get-Content $Log -Raw } else { '' }
    $Crashed = $CrashDirs.Count -gt 0 -or $LogText -match 'Fatal error|Unhandled Exception|=== Critical error ==='
    return @{ Crashed = $Crashed; CrashDirs = $CrashDirs; Hung = $TimedOut }
}

<# Close-ups from each review frame's sidecar boxes, into review\crops. #>
function New-ReviewCrops {
    param([string]$RunDir, [string]$Ffmpeg = (Find-Ffmpeg))
    $Shots = Join-Path $RunDir 'review'
    if (-not $Ffmpeg -or -not (Test-Path $Shots)) { return 0 }
    $Crops = Join-Path $Shots 'crops'
    New-Item -ItemType Directory -Force $Crops | Out-Null
    $Count = 0
    foreach ($J in (Get-ChildItem $Shots -Filter '*.json')) {
        $Png = [IO.Path]::ChangeExtension($J.FullName, 'png')
        if (-not (Test-Path $Png)) { continue }
        $S = Get-Content $J.FullName -Raw | ConvertFrom-Json
        foreach ($P in $S.boxes.PSObject.Properties) {
            $B = $P.Value
            if (-not $B -or $B.Count -ne 4) { continue }
            $W = [int]($B[2] - $B[0]); $H = [int]($B[3] - $B[1])
            if ($W -lt 16 -or $H -lt 16) { continue }
            # Twice the size: a needle or a mirror is small in a 1280x720 frame.
            & $Ffmpeg -hide_banner -loglevel error -y -i $Png -vf "crop=${W}:${H}:$([int]$B[0]):$([int]$B[1]),scale=$($W * 2):-1" (Join-Path $Crops "$($J.BaseName)-$($P.Name).png")
            $Count++
        }
    }
    return $Count
}

<#
  review.md for one launch. -Kind: preflight, launch, viewcheck or robot. -Result and -Reasons: what
  the game's own checks said. The numbers decide the launch, gauges and audio items when they can;
  every other item starts as TODO for the reviewer.
#>
function Write-ReviewSheet {
    param([string]$RunDir, [string]$Kind, [string]$Title, [string]$Result, [string]$Reasons, [hashtable]$Evidence)
    $Review = $null
    $ReviewJson = Join-Path $RunDir 'review.json'
    if (Test-Path $ReviewJson) { $Review = Get-Content $ReviewJson -Raw | ConvertFrom-Json }
    $Pre = $null
    $PreJson = Join-Path $RunDir 'preflight.json'
    if (Test-Path $PreJson) { $Pre = Get-Content $PreJson -Raw | ConvertFrom-Json }
    $Log = Join-Path $RunDir 'game.log'
    $Shots = @(Get-ChildItem (Join-Path $RunDir 'review') -Filter '*.png' -ErrorAction SilentlyContinue | Sort-Object Name)
    $Auto = @{}
    $Note = @{}

    # Launch: a crash, a hang or no result line fails it.
    if ($Evidence.Crashed) { $Auto.launch = 'FAIL'; $Note.launch = "crash: $($Evidence.CrashDirs -join ', ')" }
    elseif ($Evidence.Hung) { $Auto.launch = 'FAIL'; $Note.launch = 'the game did not quit in time and was stopped' }
    elseif (-not $Result) { $Auto.launch = 'FAIL'; $Note.launch = 'no result line in the log' }
    else { $Auto.launch = 'PASS'; $Note.launch = "result $Result" }

    # Gauges from the sidecars: the needles against the true values while the speed is steady.
    if ($Review -and $Review.gauges.samples -gt 0) {
        $G = $Review.gauges
        $Bad = [int]$G.speed_wrong_samples + [int]$G.rpm_wrong_samples
        $Share = $Bad / [Math]::Max(1, 2 * [int]$G.samples)
        $Auto.gauges = if ($G.speed_unit -eq 'none') { 'TODO' } elseif ($Share -gt 0.05) { 'FAIL' } else { 'PASS' }
        $Note.gauges = "speed ($($G.speed_unit)) median error $($G.speed_median_error), p95 $($G.speed_p95_error), wrong in $($G.speed_wrong_samples) of $($G.samples) steady samples; rpm median error $($G.rpm_median_error), p95 $($G.rpm_p95_error), wrong in $($G.rpm_wrong_samples)$(if ($G.speed_unit -eq 'none') { '; no live speedometer needle: read the cluster crops' })"
    }
    if ($Review) {
        $St = $Review.steering
        $Note.steering = "MEASURED rim $($St.rim_deg_per_road_wheel_deg) deg per road-wheel deg (car data $($St.car_data_ratio)); lock to lock $($St.lock_to_lock_turns) turns (car data $($St.car_data_lock_to_lock_turns)); fastest rim $($St.max_rim_speed_dps) deg/s. Direction and smoothness: look at the wheel crops."
    }

    # The cockpit steer test (-SimCruiseCockpitTest): gauges fixed, the wheel on its column.
    $CockpitJson = Join-Path $RunDir 'cockpit\cockpit.json'
    if (Test-Path $CockpitJson) {
        $Ck = Get-Content $CockpitJson -Raw | ConvertFrom-Json
        $R = [string]$Ck.reasons
        $GaugeBad = $R -match 'gauge|cluster-pixels|holds-other-parts|dash-move'
        $ColumnBad = $R -match 'off-its-column|hub-moves|rim-tilted|wrong-way|rim-at-full-lock'
        $Auto.cockpit = if ($GaugeBad) { 'FAIL' } else { 'PASS' }
        $Note.cockpit = "cockpit test: gauges and dash move $($Ck.gauges_max_move_cm) cm / $($Ck.gauges_max_turn_deg) deg (limit $($Ck.limits.fixed_move_cm) cm / $($Ck.limits.fixed_turn_deg) deg, worst $($Ck.gauges_worst)); cluster pixels changed $($Ck.cluster_changed_percent) % with the wheel hidden (limit $($Ck.limits.cluster_changed_percent) %); wheel mesh vertices outside the rim $($Ck.outside_rim_percent) %. Frames cockpit\centre.png, full-left.png, full-right.png, *-nowheel.png, diff-full-left.png, diff-full-right.png"
        $Auto.column = if ($ColumnBad) { 'FAIL' } else { 'PASS' }
        $Note.column = "cockpit test: off-axis turn $($Ck.wheel_off_axis_deg) deg (limit $($Ck.limits.wheel_off_axis_deg)), hub moves $($Ck.hub_move_cm) cm (limit $($Ck.limits.hub_move_cm)), rim tilt $($Ck.rim_tilt_deg) deg on the column (limit $($Ck.limits.rim_tilt_deg); -1: no vertices), full lock $($Ck.rim_full_lock_left_deg) / $($Ck.rim_full_lock_right_deg) deg against $($Ck.expected_full_lock_deg) deg, ratio $($Ck.ratio) (car data $($Ck.car_data_ratio)), lock to lock $($Ck.lock_to_lock_turns) turns"
    }

    # Audio: errors and warnings the audio code logged.
    if (Test-Path $Log) {
        $Audio = @(Select-String -Path $Log -Pattern '(LogAudio\w*|LogSimCruiseAudio\w*|LogSimCruiseEngineSound\w*): (Error|Warning)')
        $Auto.audio = if ($Audio.Count -eq 0) { 'PASS' } else { 'FAIL' }
        $Note.audio = if ($Audio.Count -eq 0) { 'no audio errors or warnings in game.log' } else { "$($Audio.Count) lines, first: $(($Audio[0].Line -replace '^\[.*?\]\[.*?\]', '').Trim())" }
    } else { $Auto.audio = 'FAIL'; $Note.audio = 'no game log' }

    if ($Pre) {
        $Note.popin = "pre-flight: tile pop-in $($Pre.tile_popins), car pop-in $($Pre.car_popins), flash frames $($Pre.flash_frames)"
        $Note.road = "pre-flight: steps $($Pre.road_steps) (max $($Pre.max_step_cm) cm), gaps $($Pre.road_gaps), jolts at tile borders $($Pre.border_jolts)"
        $Note.sky = "pre-flight: white sky $($Pre.sky_white_percent) % of sky samples"
        $Note.traffic = "pre-flight: stuck traffic $($Pre.stuck_traffic)"
        $Note.place = "place `"$($Pre.place)`"; view check $(if ($Pre.viewcheck_ran) { if ($Pre.viewcheck_passed) { 'PASS' } else { "FAIL ($($Pre.viewcheck_reasons))" } } else { 'not run' }); fell back $($Pre.fell_back)"
    }

    $Q = [string][char]96
    $ResultText = if ($Result) { $Result } else { 'none' }
    $ReasonText = if ($Reasons -and $Reasons -ne 'ok') { " ($Reasons)" } else { '' }
    $CrashText = if ($Evidence.CrashDirs.Count) { "; crash: ${Q}crash\${Q}" } else { '' }
    $VideoText = if (Test-Path (Join-Path $RunDir 'video.mp4')) { ", video ${Q}video.mp4${Q}, contact sheet ${Q}sheet.png${Q}" } else { '' }
    $Lines = @("# Review: $Title", '',
        "Kind: $Kind. Game result: **$ResultText**$ReasonText.",
        "Run folder: ${Q}$RunDir${Q}. Command line: ${Q}cmdline.txt${Q}; commit and exit code: ${Q}run.txt${Q}; GPU: ${Q}gpu.txt${Q}; log: ${Q}game.log${Q}$CrashText.",
        "Frames: $($Shots.Count) full-size in ${Q}review\${Q} (start, every 5 s, events), close-ups in ${Q}review\crops\${Q}$VideoText.", '')
    if ($Review) {
        $Lines += @('## Measured', '',
            "- Steering wheel: **$($Review.steering.rim_deg_per_road_wheel_deg) rim degrees per road-wheel degree** (car data $($Review.steering.car_data_ratio)); **lock to lock $($Review.steering.lock_to_lock_turns) turns** (car data $($Review.steering.car_data_lock_to_lock_turns)) at a road-wheel full lock of $($Review.steering.full_lock_road_wheel_deg) deg; fastest rim motion $($Review.steering.max_rim_speed_dps) deg/s; largest rim angle $($Review.steering.max_rim_deg) deg.",
            "- Gauges: $($Note.gauges)",
            "- Events: $(if ($Review.events.Count) { ($Review.events | Select-Object -First 30) -join '; ' } else { 'none' })", '')
    }
    $Lines += @('## Verdicts', '',
        'Look at every frame in `review\` and the crops before you write a verdict. Each verdict is PASS, FAIL or N/A; give the frame for each FAIL. TODO means not reviewed yet.', '',
        '| Item | Verdict | Evidence (frames, numbers) |', '|---|---|---|')
    foreach ($I in $script:ReviewItems) {
        $V = if ($Auto.ContainsKey($I.Key)) { $Auto[$I.Key] } else { 'TODO' }
        $E = if ($Note.ContainsKey($I.Key)) { $Note[$I.Key] } else { '' }
        $Lines += "| $($I.Name) | $V | $($E -replace '\|', '/') |"
    }
    $Lines += @('', '## Reviewer', '', 'Reviewed by: TODO', '', 'Notes: ')
    $Lines | Set-Content -Encoding utf8 (Join-Path $RunDir 'review.md')
}

<# @{ Complete; Todo (items); Failed (items) } for one review.md. #>
function Test-ReviewSheet([string]$Path) {
    $Todo = @(); $Failed = @(); $Seen = 0
    foreach ($Line in (Get-Content $Path)) {
        if ($Line -match '^\| (.+?) \| (\S+) \|') {
            $Item = $Matches[1]; $V = $Matches[2]
            if ($Item -eq 'Item' -or $Item -match '^-+$') { continue }
            $Seen++
            if ($V -notin @('PASS', 'FAIL', 'N/A')) { $Todo += $Item }
            elseif ($V -eq 'FAIL') { $Failed += $Item }
        }
        elseif ($Line -match '^Reviewed by: TODO') { $Todo += 'Reviewed by' }
    }
    if ($Seen -lt $script:ReviewItems.Count) { $Todo += "only $Seen of $($script:ReviewItems.Count) items" }
    return @{ Complete = ($Todo.Count -eq 0); Todo = $Todo; Failed = $Failed }
}
