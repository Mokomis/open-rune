# Open Rune — visual comparison prototype

October 24, 2026. Native ARM64, OPPO OPD2515, DM-Hildir, 2520x1680, 4x MSAA, mailbox presentation and CPU/GPU overlap. Original and Enhanced use identical resolution, anti-aliasing and pacing.

## User control

The top-right button displays the currently applied mode and switches between **Original** and **Enhanced** without restarting. It remembers the selection in the prototype's private preferences. Original leaves the existing rendering settings unchanged; neither mode edits game INIs. First installation defaults to Original.

The **Engine: … FPS** counter measures completed engine loops over roughly one second. It briefly resets on mode changes. It is not displayed FPS. Independent display numbers below use SurfaceFlinger actual-presentation timestamps.

Enhanced is deliberately restrained: component-wise highlight threshold 0.85, bloom extraction strength 0.18, blur sigma 0.9, contrast multiplied by 1.035 and saturation by 0.97. The shipped preset computes its glow from quarter-resolution images and composites that glow onto the native-resolution frame. These values are a first visual preset, not an art-direction final. Bloom and grading affect the rendered game frame, including the in-engine HUD; the Android controls are separate. True HDR output and new lighting/material systems are not added.

The original bloom implementation's BloomAmount adjusts blur width, not strength. Two push constants now separate extraction threshold and strength; their defaults preserve the previous bloom path. Existing render settings are never overwritten by the comparison switch. UI commands pass through atomics and are applied on the engine thread between frames. Vulkan synchronization validation was active during repeated live switching and reported no graphics errors/warnings. Android and desktop builds passed. The saved Enhanced preference was verified after force-stop and relaunch without an explicit mode extra. The final APK excludes the validation library.


## Final quarter-resolution bloom and upscaling diagnostic

The final preset skips the half-resolution bloom level. The game scene remains native resolution unless the separate `scale` diagnostic is explicitly supplied. Vulkan core/synchronization validation passed a bounded 900-frame Enhanced run, including framebuffer capture; Android and desktop builds passed. No validation library is present in the final APK.

The following short trials used the same seed/map and scripted 22-second right-side drag, with 1200 profiled engine frames and 20 presentation polls. They are preliminary device samples, not fixed-clock benchmarks. The Original trial ended at 120 Hz; both Enhanced trials ended at 90 Hz, so their displayed FPS must not be treated as a pure renderer-cost comparison.

| Mode | Internal resolution | Engine FPS | Presented FPS | GPU interval | Display sample |
| --- | --- | ---: | ---: | ---: | ---: |
| Original | 2520x1680 | 179.72 | 119.99 | 4.998 ms | 11.83 s |
| Enhanced | 2520x1680 | 133.36 | 90.12 | 6.804 ms | 11.83 s |
| Enhanced, 75% scale | 1890x1260 | 180.28 | 90.13 | 4.329 ms | 11.74 s |

The lower-resolution run uses the existing bilinear presentation sampler, not Arm ASR or Snapdragon GSR. It renders 56.25% as many scene pixels as native resolution. Output and Android controls remain 2520x1680; the in-engine HUD currently shares the scaled scene and can soften. This is promising evidence for reduced rendering cost, not proof of a temperature/power reduction. Current uncapped rendering can spend the saved work generating more frames. Proper synchronized frame pacing is needed to turn headroom into predictable power savings.

The user raised upscaling during this test. A practical next candidate is Qualcomm's spatial Snapdragon GSR v1: it combines upscaling and sharpening in one pass without requiring temporal history. Arm ASR and GSR2 are temporal solutions needing additional motion-vector/history integration that this renderer does not currently provide. No third-party upscaler has been integrated in this release. Preserve native UI/HUD clarity when adding one.

Primary references:
- https://github.com/SnapdragonGameStudios/snapdragon-gsr/blob/main/sgsr/v1/README.md
- https://github.com/arm/accuracy-super-resolution-generic-library/blob/main/README.md
- https://www.qualcomm.com/developer/blog/2024/10/introducing-snapdragon-game-super-resolution-2

Final screenshots: `images/visual-quarter-original.png`, `images/visual-quarter-enhanced.png`, and `images/visual-quarter-scaled75.png`. These are unmodified device captures; animation and timing can differ. The final installed APK SHA-256 is `1c7ec64ee3d115be7da92ea84e66625b610d1f6b02a207e3b3a51e493964a263`.

## Initial half-resolution bloom measurements (superseded preset)

Each run starts DM-Hildir with seed=1, no movement, unchanged camera, 2400 engine profile frames after 60 warm-up frames. Presentation sampling starts three seconds after rendering begins and polls 50 times. Engine and display sampling windows overlap but are not identical. Animated fire continues normally; this is not a frozen-frame benchmark.

| Run | Engine FPS | Presented FPS | Presentation duration | End panel rate |
| --- | ---: | ---: | ---: | ---: |
| Original A | 152.63 | 109.53 | 30.49 s | 120 Hz |
| Enhanced | 148.42 | 120.03 | 30.32 s | 120 Hz |
| Original B | 173.62 | 113.24 | 30.21 s | 90 Hz |

Enhanced sustained approximately 120 displayed FPS in its 30-second sample. The Original runs were confounded by panel-rate changes: OPPO explicitly reported `PRIORITY_HIGH_TEMPERATURE` requesting 90 Hz alongside the application's 120 Hz request. Battery temperature was 35.8 C when this policy was captured; that does not establish which sensor or threshold drives the policy. Do not interpret Enhanced's higher displayed FPS in this table as a speed improvement. Engine results indicate extra rendering cost, but changing clocks/thermal state prevent a precise percentage estimate. No thermal controls or clocks were modified.

Before the final overlay fix, two Original runs presented about 60 FPS despite 180+ engine FPS. The saved Rune-only rule remained `persist.opdrr.42800e61=3`; re-requesting that same policy alone did not fix the behavior. Android's updating overlay Views now request the same refresh rate as the game SurfaceView on API 35+. Hardware presentation subsequently reached 120 Hz. However, a later short stationary Original run still measured 60.02 displayed FPS against 185.16 engine FPS, so the overlay request is not proven to eliminate every OEM rate reduction. Earlier and later 60 Hz logs are retained as diagnostic evidence. Nominal 120 Hz requests must not be treated as proof of actual presentation.

Android documents independent View frame-rate voting here: https://developer.android.com/develop/ui/views/animations/adaptive-refresh-rate . The specific OPPO behavior and thermal policy were checked on the live device. All refresh properties match the preceding release, and the four original DroidDeck INI hashes match. No global display setting, other app rule, firmware or thermal control was changed. The existing Rune-only policy was re-requested without altering its saved value.

## Reproduce

Force-stop only org.runeclassic.port before each new launch. Use the same map, seed, resolution and MSAA. Launch with extras `--ei seed 1 --ei profile 2400 --ez enhanced false`, then repeat with `true`. Use `Tools/RuneValidation/android_present.py --device DEVICE --samples 50 --output PREFIX` while the app stays visible. Keep engine output from `files/native-run.log`. Record the thermal and panel state as well as the nominal Android display mode. Do not mix a live toggle into a single-mode profile interval.

This is one scene and short samples. Full campaign, enemy combat, save/load, transitions, opposite orientation and long thermal behavior remain unvalidated. See PERFORMANCE.md and CLAUDE.md for the broader port status.

A subsequent matched slow camera-pan test at elevated device temperature measured Original at 149.35 engine / 90.14 displayed FPS and the initial Enhanced preset at 103.12 engine / 90.05 displayed FPS. Both panel dumps reported 90 Hz. This motivated reducing the glow processing from half resolution to quarter resolution; the rendered game stays at 2520x1680 with 4x MSAA. These initial measurements are retained rather than represented as final-preset results.
