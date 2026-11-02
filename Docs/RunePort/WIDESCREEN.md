# Widescreen and field of view

Video and Enhancements share **Camera & field of view**. The slider ranges from
70–120°, default 90°, expressed as horizontal FOV at the original 4:3 reference.
**Automatic** framing preserves vertical visibility and reveals more sideways
on wider screens, including 3:2. **Original** framing retains constant horizontal
FOV. Reset FOV restores 90°; Restore defaults on this page also restores Automatic.
The current rendered horizontal and vertical angles are shown separately.

`RuneView::Adjust` scales the tangent of the authored half-angle by the user's
base-FOV ratio and the viewport aspect ratio relative to 4:3. The baseline comes
from Rune's configured `DefaultFOV` (75° in the imported configuration), not the
separate SurrealEngine `MainFOV` override on live pawns. This retains zoom
magnification. It changes the rendered camera only; script-owned FovAngle,
DefaultFOV, DesiredFOV and save data are not rewritten. Rune's Scripting state,
external ViewTarget/CameraActor, fixed cameras and camera-taking zones bypass
both user and aspect adjustments to preserve their authored framing. Other
games are unaffected. Canvas projection and world rendering use the same
resulting camera angle.

Android stores `menu.fov` and `menu.wideView`, forwards them through the existing
atomic settings bridge, and restores them on launch. Original-effects comparison
does not change camera settings. Shared native code defaults to 90°/Automatic;
desktop camera settings UI is not implemented yet.

The gameplay HUD already adapts to the full viewport. The menu keeps its
centered 3:2 composition, with side margins on wider screens. Touch controls
anchor to the app viewport. This change does not redesign the menu for ultrawide.

## Validation

`Tools/RuneValidation/camera_fov_test.cpp` checks 4:3, 3:2, 16:9, 21:9 and 32:9,
constant vertical visibility, zoom magnification, cinematic bypass, invalid
inputs and legacy framing. Host preference tests cover range, degree labels,
restart persistence and reset. Android ARM64 compilation and signed packaging
pass.

For app-only aspect checks, use `--ef aspectPreview 1.7777778` or `2.3333333`
alongside `--es saveTest NAME`. This fits the entire app into a centered viewport,
including SDL rendering, menus, HUD and touch controls. It neither changes global
display settings nor persists its shape. Normal relaunch removes it. Ratios
outside 1–4 or launches without isolated saves are rejected.

Add `--ef previewInches 7` to approximate a physical 7-inch diagonal using the
device-reported x/y DPI, centered on black. The requested area is reduced only
if it cannot fit on the panel. On this OPPO, 344.129 dpi gives a 2100×1181 area
for 7-inch 16:9 (about 6.10×3.43 inches). Accuracy depends on the reported DPI;
this previews physical size and framing, not another panel's OLED/HDR/color
characteristics or performance. Normal relaunch removes the size override.

`android_camera_fov.py --device DEVICE --output OUTPUT` checks the actual touch
slider, shared Video/Enhancements state, reset, framing selection and 16:9/21:9
previews at native viewport resolution with 4× AA. It uses isolated saves and
leaves 90°/Automatic selected; restart normally to leave the diagnostic viewport.

The installed OPPO build passed that focused check, including both menu entries
and the actual touch slider. At the 90° setting, current camera readings were:

| Viewport | Horizontal FOV | Vertical FOV |
| --- | ---: | ---: |
| 3:2 | 96.7° | 73.7° |
| Simulated 16:9 | 106.2° | 73.7° |
| Simulated 21:9 | 120.5° | 73.7° |

The 120° slider endpoint produced 125.7° horizontally on 3:2. Original framing
returned 90° horizontal / 67.4° vertical at the 90° setting. Screenshots of wide
gameplay and menus were inspected; controls and menu text fit their viewports.
Evidence: [UI results](evidence/widescreen/ui-check.log) and camera/render-size
logs in the same directory. Normal 3:2 launch was restored with the camera page
open, 90°/Automatic selected. Normal saves and unrelated preferences were
preserved. No FPS benchmark was run.

Mathematical coverage and simulated viewports do not establish compatibility
with every physical widescreen device, notch/inset combination or scripted
cutscene. Zoom and cinematic safeguards were checked mathematically and in
source, not through a full campaign replay.
