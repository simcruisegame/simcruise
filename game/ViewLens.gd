extends RefCounted
## The driver's lens: how wide the in-car view is (vertical field of view, the
## camera keeps the height and widens with the window).
##
## Perspective is set by one number. A wide lens (the old 67°) makes the road
## look wider and farther away and exaggerates speed; a long lens flattens
## distance. Three choices, cycled with F6 and remembered as preference "lens":
##
##   natural  45° — about a 30 mm lens on full frame: near the field in which
##            the eye judges size and distance, so a lane, a car ahead and a
##            building keep their real proportions on an ordinary screen.
##   screen   true to the display: the angle the screen itself subtends at the
##            eye (its physical height from the OS-reported DPI, and the
##            viewing distance, preference "view_cm", default 60 cm). Things
##            then appear life-size through the screen, as sim racers set it;
##            on a laptop this is narrow (about 18° at 60 cm on 15.6").
##   wide     67° — the previous view, for awareness in tight streets.
##
## Scripted captures and checks keep 67° so photo comparisons stay repeatable.

const ORDER := ["natural", "screen", "wide"]
const NATURAL_DEG := 45.
const WIDE_DEG := 67.

static func screen_fov(view_cm: float) -> float:
 var dpi := float(DisplayServer.screen_get_dpi())
 var px := float(DisplayServer.window_get_size().y)
 if dpi < 40. or px < 100.:return NATURAL_DEG
 var height_cm := px / dpi * 2.54
 return clampf(rad_to_deg(2. * atan(height_cm * .5 / maxf(view_cm, 20.))), 10., 90.)

static func fov(mode: String, view_cm: float) -> float:
 match mode:
  "screen":return screen_fov(view_cm)
  "wide":return WIDE_DEG
 return NATURAL_DEG

static func label(mode: String, view_cm: float) -> String:
 match mode:
  "screen":return "Lens · true to screen (%.0f° at %.0f cm)" % [screen_fov(view_cm), view_cm]
  "wide":return "Lens · wide (67°)"
 return "Lens · natural (45°)"
