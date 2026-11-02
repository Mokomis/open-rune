# Open Rune: Android hardware scaling investigation

October 25, 2026. Tested on OPPO OPD2515, Adreno 829, 2520×1680 panel.

## Menu integration

Video and Enhancements now open the same **Resolution & scaling** page.
Rendering resolution selects **Native (100%), 75%, or 50%**; Scaling method
selects **Hardware Scaling** or **Snapdragon GSR**. Both methods support the lower
percentages. Native bypasses scaling while retaining the preferred method for
next time. Existing native/GSR 75% choices migrate unchanged; fresh installs
remain native. Settings apply live, persist across launches, and reset together
from this page. The normal Android HUD and controls stay at full resolution.

On the OPPO fullscreen display:

| Choice | Render size | Display size |
| --- | --- | --- |
| Native | 2520×1680 | 2520×1680 |
| 75% | 1890×1260 | 2520×1680 |
| 50% | 1260×840 | 2520×1680 |

A coherent snapshot published by the Vulkan renderer after submission supplies
actual render/output dimensions and GSR shader state. The page shows the active
method and render-to-display dimensions, with “Applying selection” while the
submitted frame differs from the request. The FPS HUD reads `FPS 120 · Hardware 75% · 4× MSAA` (or `Software 75%`
for GSR and `Native 100%` without scaling). The percentage comes from the
actual rendered width relative to the display view. Full dimensions remain on
the scaling page. The app names the system path accurately; it cannot use
public Android APIs to promise DEVICE composition for every frame. It does not
claim “Device not supported” just because privileged HWC diagnostics are absent.

The SurfaceView fills its parent independently of the fixed buffer size. This
is essential: SDL's default wrap-content layout shrank the view itself during
an initial live-switch test, causing double scaling. Explicit MATCH_PARENT
sizing fixes that feedback. Native/GSR restore layout-sized buffers. SDL touch
normalization follows actual buffer/view ratios after changes.

GSR modes cross JNI as one atomic request (native/75/50); the engine applies it
between frames. System scaling uses SurfaceHolder resizing and the existing SDL
resize/swapchain lifecycle. Diagnostic intent overrides remain session-only and
never become saved preferences through unrelated setting changes.

## Original hardware investigation

The following records the initial isolated test, before menu integration.

## Confirmed on the device

Open Rune can render to an 1890×1260 output surface and let the Qualcomm display
hardware enlarge it to 2520×1680. GSR is disabled in this path. The Android HUD
and controls retain their full-resolution surface. No global display setting,
root policy, driver or firmware change was needed.

SurfaceFlinger reported the game layer as **DEVICE**, with source crop
1260×1890 and destination 1680×2520. The vendor DPU table independently showed
two SDE pipes scaling the left/right halves: source widths 630 to destination
widths 840, and source height 1890 to destination height 2520. Dimensions in
these tables are portrait because the Vulkan presentation buffer is pre-rotated.
This proves active hardware scaling for the captured composition, rather than
merely the presence of scaler configuration properties. The GSR control instead
had matching full-panel source and destination rectangles.

The SDM panel reported 120 Hz in both cases. Hardware scaling is ordinary display
scaling; this test does not establish proprietary OPPO AI super resolution or
frame generation. Android chooses composition, so this observation is not a
guarantee for every device, overlay or future composition state.

## Focused comparison

Same APK/native libraries, Dwarf1wwheel, fixed camera
`-1450,-2000,-650,52200,-3500`, seed 1, world features 8103 (Approximate water),
4× MSAA, target 120 FPS, internal scene 1890×1260 in both cases.

| Method | Game output surface | Displayed FPS | Duration | p95 interval |
| --- | --- | ---: | ---: | ---: |
| System 75% | 1890×1260 | 95.10 | 12.45 s | 16.66 ms |
| GSR 75% | 2520×1680 | 89.52 | 12.15 s | 16.66 ms |

Presentation timestamps, not the engine counter, supply these numbers. GPU clock
samples were 1.225 GHz throughout both runs; CPU clocks varied. These sequential
short samples have no order-balanced repeat. Water, particles and simulation
continue behind the fixed camera, so they are not identical-frame workloads.
The result is promising but does not establish a repeatable 6% gain, sustained
120 FPS, or power/heat savings. The system path also runs final GPU processing at
the smaller output size, so this is a comparison of complete paths, not an
isolated measurement of GSR shader cost.

Screenshots showed correct orientation, scene framing and HUD layout. A screen
capture may be recomposed rather than recording the physical DPU filter output;
it is not proof of identical perceived sharpness on the panel. Physical-screen
quality comparison remains appropriate before choosing a shipping default.

## Reproducing the diagnostic

`MainActivity` accepts `--ei surfacePercent 75` (also 50 and 100). Values below
100 require `--es saveTest NAME`, force initial GSR off, and set a smaller
SurfaceHolder buffer while leaving the view's layout size unchanged. SDL touch
coordinates are transformed into buffer coordinates before normalization. The original experiment tested only 75%; subsequent menu validation is recorded
below. Diagnostic overrides initialize the same model as the menu and can now
be changed live.

Example launch, using the connected device's current ADB identifier:

```sh
adb -s "$RUNE_DEVICE" shell am start -n org.runeclassic.port/.MainActivity \
  --es map Dwarf1wwheel.run --es saveTest system-scaling \
  --es probeView '-1450,-2000,-650,52200,-3500' \
  --ei worldFeatures 8103 --ei surfacePercent 75 --ez upscale false \
  --ei msaa 2 --ei seed 1 --ez traceCombat true --ei profile 400
```

Force-stop between samples. For the GSR control omit `surfacePercent` and set
`--ez upscale true`. Measure with `android_present.py --samples 20 --oppo-clocks`
after startup. Compare both HWC source/destination and the vendor DPU pipe table.
An engine log saying `mode=native` under the system experiment means GSR is off;
it does not mean the output surface matches the panel resolution.

Evidence: [evidence/system-scaling](evidence/system-scaling). Only relevant
composition excerpts are retained in Git; full device-wide dumps and screenshots
remain in local `outputs/system-scaling`. Normal launches omit the extra and
keep the original full-size surface. The test APK uses the existing signing key
and unchanged package ID. The historical experiment did not add production preferences; menu integration
adds `menu.scalingMethod` and extends `menu.resolution` while retaining the
legacy `upscale` migration field.

References: [Android Hardware Composer](https://source.android.com/docs/core/graphics/hwc),
[Qualcomm Adreno DPU overview](https://docs.qualcomm.com/bundle/publicresource/topics/80-70020-18/overview.html).


## Menu validation results

`MenuSettingsTest` passes legacy GSR migration, system 50% persistence despite
its legacy boolean, isolated preference changes and native defaults. Native
ARM64 builds and signed APK packaging pass. The focused installed-device test
`android_scaling_menu.py` passes all five live paths, actual resolution readouts,
shared Video/Enhancements values, system 50% restart and native round trip.
HWC and DPU excerpts confirm DEVICE hardware scaling at both system 75% and 50%,
with a full-resolution Android overlay. The HUD counter fits longer method and
resolution labels inside the existing user-sized frame.

Short presentation samples in the same stationary DM-Hildir scene, 4× MSAA,
120 FPS target, unchanged saved visual effects:

| Mode | Displayed FPS | Duration | p95 interval |
| --- | ---: | ---: | ---: |
| Native | 92.91 | 7.09 s | 16.67 ms |
| System 75% | 119.14 | 7.14 s | 8.33 ms |
| GSR 75% | 118.98 | 6.98 s | 8.34 ms |

GPU samples ranged 1.15–1.225 GHz, with system 75% at 1.15 GHz throughout.
The panel reported 120 Hz. These short sequential smoke checks are not
sustained-performance, campaign or heat claims. The 50% paths were functionally
checked without additional FPS sweeps. Initial failed live-resize evidence stays
in local outputs; retained evidence uses the MATCH_PARENT fix. No new Vulkan
validation-layer run was performed for this change.

Evidence: [evidence/scaling-menu](evidence/scaling-menu). Full UI screenshots
remain in local `outputs/scaling-menu`. The final APK additionally shortens and
fits the FPS HUD label; native libraries and scaling behavior match the live
switching test.

The final installed APK passes the real Throw/Use button test at system 50%
(1260×840, 4× MSAA): thrown axe recovered, nearby food consumed, no direct
engine Use injection. The fixture positions the character near the item; it
does not prove navigation or a complete campaign.

All ten normal/campaign save-file hashes and exact preference bytes were
unchanged after testing. The installed APK hash matches the packaged artifact.
SinkingShip2 is restored and paused at native 4×, with Resolution & scaling
open for the user. The final HUD and page were visually checked on the device.

The wording follow-up renames the system option to **Hardware Scaling** and
explains GSR as a software filter on the GPU. A separate note retains Android's
possible GPU fallback. Installed-device UI captures verify the shorter
`FPS <number> · Hardware 75%` and `FPS <number> · Software 75%` labels and that
the full explanation fits. This is a Java/UI change; native renderer libraries
are unchanged, and these captures are label checks rather than FPS benchmarks.
The user's current Hardware 50% and other saved preferences are retained for
the wording follow-up handoff, with the campaign checkpoint paused.
During this handoff, the campaign checkpoint/backup rotated and detail/depth
preferences differed from the initial snapshot. Their cause was not established;
latest values were retained, and both save archives remain in local outputs.
The other eight save hashes were unchanged. Do not claim exact preference/save
preservation for the wording follow-up; see `label-followup.json`.

## October 27 defaults and quarter-resolution option

Fresh Android installations and Reset use **Hardware Scaling at 50%**. Existing
saved selections are retained; legacy explicit upscaling preferences still
migrate to their original GSR/native choice. This tablet was explicitly set to
Hardware 50% at the user's request.

Video and Enhancements share Native/75%/50%/**25%** options for both Hardware
Scaling and GSR. The 25% option renders one quarter of each dimension (one
sixteenth of native pixel count): 630×420 on the OPPO's 2520×1680 display.
Android HUD/menu/touch overlays remain full resolution. GSR's fourth mode is
handled by both the single-player and multiplayer render loops.

Host settings checks cover fresh/reset defaults, legacy preference migration,
25% persistence and unrelated-setting isolation. The constant-bloodlust practice
session was stopped; preview launches omit the replenishment flag.

Installed-device evidence confirms 1260×840 and 630×420 Hardware Scaling
render targets with 4× MSAA, then a return to 1260×840. The live accessibility
HUD reported Hardware 25% and bloodlust 0/100. Final saved selection readback
was Hardware 50%. The user was interacting during the menu check, so the
automated menu sequence stopped; GSR 25% has built support but was not validated
in a live rendered scene this turn. No sustained FPS comparison was performed.
Captures and renderer logs are under local `outputs/scaling25`; the later
composition capture is at 50%, not 25%, despite its original filename.
