# Open Rune — Native / Snapdragon GSR comparison and Android frame pacing

See [FRAME-RATE-REFRESH.md](FRAME-RATE-REFRESH.md) for the latest independent
FPS/refresh controls and the user-requested OPPO 144 Hz policy.

## October 25 hardware-scaler experiment

The OPPO's display hardware successfully scaled a smaller Open Rune surface;
see [SYSTEM-SCALING.md](SYSTEM-SCALING.md) for direct HWC/DPU evidence and the
short comparison. Video and Enhancements now share Native/75%/50%/25% and Hardware Scaling/GSR
choices, with actual renderer dimensions. See that document for menu validation.
Fresh/reset Android defaults are now Hardware Scaling at 50%; saved choices
are retained. The sections below describe the original GSR implementation and its historical
UI/tests; the current themed Android HUD is a separate native-resolution overlay.

## Controls and behavior

The second top-right button switches live between Native and Upscaled 75%.
On this OPPO, Native renders 2520×1680; Upscaled renders 1890×1260 and uses
Snapdragon GSR v1 in the final presentation shader to reconstruct 2520×1680.
The lower scene resolution has 43.75% fewer pixels. Both retain 4× MSAA.
The first button independently switches Original / Enhanced bloom and color.
Both choices persist. Fresh installations default to Native and Original.

The overlay shows engine FPS, a 120 FPS target, and battery temperature. Engine
FPS is not a display measurement; battery temperature is not the device's surface
or GPU temperature. Actual presentation timing is independently collected with
`Tools/RuneValidation/android_present.py`. `android_thermal.py` records battery,
charging, brightness and thermal-service state without changing device settings.

The game's HUD still shares the scene resolution and is upscaled with the world.
Android controls remain at native resolution. A mode switch briefly reallocates
render targets and rebuilds scene pipelines; discard the transition/warmup from
measurements. No motion vectors, frame generation or temporal reconstruction are
used. Fine detail can differ from native; this is a spatial quality/performance
tradeoff, not a claim of identical pixels.

## Implementation

- Qualcomm's BSD-3-Clause GSR v1 shader is retained under `Thirdparty/sgsr`, with
  source revision and adaptations documented there. The filter runs after bloom,
  before existing color/gamma/output conversion, with sharpening reduced to 1.5
  and a clamp that preserves source bloom highlights above 1. It adds no extra
  image/pass. UV rotation remains in the presentation shader.
- AndroidX Games Frame Pacing **2.1.3** is fetched from official Google Maven with
  a pinned SHA-256 in `Android/FramePacing.cmake`; no binary/toolchain is committed.
  The APK includes Apache-2.0 and GSR license notices.
- Swappy requests the supported device extensions, tracks the presentation queue,
  receives SDL's native window, targets 8,333,333 ns, and uses mailbox presentation
  when available (FIFO fallback). Swappy, rather than a sleep cap, limits the
  production cadence. Explicit `--ei mailbox 0` selects FIFO for diagnosis.
  Automatic interval selection is disabled after initialization (the API only
  changes existing instances); automatic pipeline selection is on.
  Display/thermal policy still controls the achievable refresh rate. No clocks,
  thermal rules or global refresh settings are modified.
- Swappy's 2.1.3 internal semaphore pool triggered VUID 00067 on this driver's
  four-image swapchain. `androidframepacing.cpp` uses Swappy's supported function
  provider API to consume the library's semaphore in an ordered queue submission
  and pass WSI a separate semaphore per acquired image. Swappy's presentation-time
  `pNext` data is preserved. This costs one empty queue submission per presentation.
  Resources are destroyed after device-idle on swapchain replacement/teardown.
  Initial failing and final passing validation logs are retained separately.
  A FIFO + Swappy trial rendered about 116 FPS but displayed about 59 FPS;
  the same build with uncapped FIFO displayed 120 FPS. Paced mailbox subsequently
  displayed 119.993 FPS with an 8.334 ms p95 interval in a 20-second control.
  These are device observations, not a claim that FIFO is generally unsuitable.
- Desktop and standalone diagnostics retain their previous presentation behavior.
  APK pacing defaults on. Launch extra `--ei pacing 0` disables it for diagnostic
  comparisons; `--ez upscale true/false` selects quality. Avoid combining the old
  `fps` sleep-cap extra with pacing. The legacy `scale` extra remains diagnostic.

## Validation and measurements

The final native run displayed **119.870 FPS over 185.193 seconds** (22,200
unique presentation timestamps); median 8.3345 ms, p95 8.3350 ms. Hardware reported
120 Hz. Engine profiling over 1,200 frames measured 120.311 FPS and a 6.078 ms GPU
interval. Over the accompanying 180-second thermal log, battery temperature rose
from **38.0 to 39.5°C**, and the vendor PhoneTemp reading from **41 to 44°C**.
The device was unplugged throughout, brightness setting 4095, Enhanced effects on,
seed 1, DM-Hildir, same stationary starting view. These are prototype scene tests,
not a full-game or long-session performance guarantee.

The GSR comparison displayed **119.878 FPS over 184.522 seconds**, median
8.3351 ms and p95 8.3357 ms, with a 120 Hz hardware readout. Battery temperature
was **37.8 → 37.7°C**; PhoneTemp was **38 → 40°C**. This followed a 333-second
cooldown. Battery start was within 0.2°C of Native, but PhoneTemp started 3°C
lower, so the temperature difference is **encouraging, not a fully controlled
heat-saving claim**. The engine's early 1,200-frame profile was 122.733 FPS with
a 5.833 ms GPU interval; this startup-window value is distinct from the longer
actual-presentation measurement. More maps and repeated, order-balanced runs are
needed before generalizing power/thermal gains.
Raw temperature and frame traces are retained; battery readings are coarse and
updated irregularly. The thermal service's cached CPU/battery fields can be stale,
so they are retained for diagnosis but are not used as current temperature claims.
Reported battery-current readings are not a calibrated power measurement.

## Sources

- https://github.com/SnapdragonGameStudios/snapdragon-gsr/tree/d926f074bcb9d714e179f1ce0fcb9ee2eeb5074e/sgsr/v1
- https://developer.android.com/games/sdk/frame-pacing/vulkan/add-functions
- https://developer.android.com/games/sdk/reference/frame-pacing/group/swappy-vk
- https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html

## Reproduce a comparison

Use the same APK, map, seed, effect setting and screen brightness; unplug USB.
For each mode, launch only Rune, let startup settle, then run the two readers in
parallel. Replace `false` with `true` for GSR and use a different output prefix:

```sh
adb -s "$RUNE_DEVICE" shell am force-stop org.runeclassic.port
adb -s "$RUNE_DEVICE" shell am start -n org.runeclassic.port/.MainActivity \
  --ei seed 1 --ei profile 1200 --ez enhanced true --ez upscale false
sleep 5
python3 Tools/RuneValidation/android_present.py --device "$RUNE_DEVICE" \
  --samples 300 --output "$RUNE_RESULT" &
python3 Tools/RuneValidation/android_thermal.py --device "$RUNE_DEVICE" \
  --seconds 180 --output "$RUNE_RESULT-thermal.json"
wait
```

Keep the game foreground and avoid input/toggles during a static comparison.
Inspect `files/native-run.log` through `run-as org.runeclassic.port`: there must
be exactly one `RUNE_UPSCALE` and one `RUNE_VISUAL` selection, no validation layer,
and `ANDROID_PACING active=1`. Collect a screenshot and `dumpsys oplusscreenmode`.
Cool between runs and compare starting battery **and** other temperature readings.
For active gameplay, repeat with a matched route rather than comparing different
views. Preserve any thermal cap; report it instead of forcing it off.

## Extended upscaled run

The game remained running for a second three-minute measurement: **119.891
displayed FPS over 184.893 seconds**, median 8.3345 ms and p95 8.3350 ms, with
the panel still at 120 Hz. Battery temperature went **37.7 → 38.1°C**, and
PhoneTemp stayed at **40°C**. Thus the upscaled mode remained near 38°C after
more than six minutes of continuous rendering in this scene. This supports the
cooler-running trend, while the unmatched initial PhoneTemp and single test
order still prevent a precise power/heat-savings claim.

The installed, layer-free APK matches SHA-256
`0b8a709ec49b0e4cfbf5e13fc74f3d6f7d97ff33533376b0259df39fa25c9be3`.
Implementation commit: `3ddabca38978b1757a131a04091373ae429fbe58`.
