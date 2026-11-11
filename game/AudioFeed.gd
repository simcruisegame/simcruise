extends RefCounted
## Low-latency feed for an AudioStreamGenerator. Pushing every free frame keeps
## the ring buffer full, so a sound reaches the speakers a whole buffer
## (150 ms) after the car made it. This keeps only target_s queued: about
## 60 ms, a 60 fps frame plus the mixer's own period with room to spare.
## A long frame can still drain the queue; when the mixer reports skipped
## frames while the game was feeding it (calls under STALL_MS apart) the target
## grows by GROW_S, and it drifts back down after RELAX_AFTER_S without a skip.
## A stall longer than STALL_MS (a scene load) is not chased: no queue short of
## the old full buffer would cover it. Never more than MAX_S.
##   var frames := feed.frames_to_push(playback)   # each frame; render that many
const RATE := 22050.
const START_S := .06
const MIN_S := .05
const MAX_S := .14
const GROW_S := .02
const RELAX_S := .005
const RELAX_AFTER_S := 20.
const STALL_MS := 200
const GROW_GAP_MS := 500

var capacity := -1
var target_s := START_S
var skips := 0
var skip_events := 0
var quiet_ms := 0
var last_call_ms := 0
var grown_ms := -GROW_GAP_MS
var starved_gaps := []   # ms since the previous push, at each starved push (checks/FeelStats.gd)

# Frames to render and push now. The first call after play() measures the
# ring buffer (Godot rounds it up to a power of two) while it is still empty.
func frames_to_push(playback: AudioStreamGeneratorPlayback) -> int:
 var available := playback.get_frames_available()
 var now := Time.get_ticks_msec()
 var skipped := playback.get_skips()
 if capacity < 0:
  capacity = available;skips = skipped;quiet_ms = now;last_call_ms = now
 if skipped > skips and now-last_call_ms < STALL_MS:
  # One drain shows up over a few calls; grow once per GROW_GAP_MS.
  skip_events += 1
  if starved_gaps.size() < 64:starved_gaps.append(now-last_call_ms)
  if now-grown_ms > GROW_GAP_MS:target_s = minf(target_s+GROW_S,MAX_S);grown_ms = now
  quiet_ms = now
 elif now-quiet_ms > RELAX_AFTER_S*1000. and target_s > MIN_S:
  target_s = maxf(target_s-RELAX_S,MIN_S);quiet_ms = now
 skips = skipped;last_call_ms = now
 var queued := capacity-available
 return clampi(int(RATE*target_s)-queued,0,available)

# Forget the buffer measurement: a new playback after play() starts empty.
func reset() -> void:
 capacity = -1
