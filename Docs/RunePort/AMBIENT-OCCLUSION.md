# Ambient occlusion checkpoint (October 27, 2026)

Open Rune now has a depth-based ambient occlusion option under **Video → Textures, water & shadows** and **Enhancements → Textures, water & shadows**. Choices are Off, Low, Medium, and High; Off is the default. The option is live and persisted in `worldFeatures` bits 14–15. It applies to both local play and the experimental multiplayer client. The Original comparison temporarily disables it without changing saved settings.

The Vulkan presentation shader samples scene depth after the 4× MSAA scene pass. It compares reconstructed nearby distances at opposite screen offsets. Averaging each pair cancels the first-order depth slope of ordinary floors and walls, avoiding the broad darkening seen in the first version. All enabled strengths now use four depth samples from two opposing pairs; Low, Medium and High vary the strength. Separate shader variants keep the original presentation path free of AO depth reads when Off. This is still a screen-space approximation without surface normals, and remains experimental.

## Device check

OPPO OPD2515, Qualcomm system Vulkan driver, hardware display at 144 Hz. Same stationary DM-Hildir view and common enhanced world-feature base `8103`, native **2520×1680**, **4× MSAA**. Each run used isolated `ao-compare` saves and 300 profiled frames, followed by SurfaceFlinger presentation samples. These are bounded view measurements, not sustained campaign FPS.

| AO | Displayed FPS | GPU scene | GPU presentation |
| --- | ---: | ---: | ---: |
| Off | 97.35 | 8.98 ms | 0.69 ms |
| Low | Not yet retested after the slope correction | — | — |
| High | 85.06 | 8.98 ms | 2.04 ms |

The subsequent four-sample optimization was checked in the same view and settings. A repeated High run measured **90.52 engine FPS**, **86.23 displayed FPS**, **8.98 ms GPU scene**, and **1.40 ms GPU presentation**. The nearby Off run measured **98.22 engine FPS**, **93.40 displayed FPS**, **8.97 ms GPU scene**, and **0.69 ms GPU presentation**. Displayed FPS varied between repetitions, so the stable conclusion is the GPU presentation reduction from about 2.04 ms to 1.40 ms; this scene still does not sustain 120 FPS with AO enabled. The optimized image is `android-4sample-high.png` outside Git. It preserves the contact-only look in the fixed hall view.

A half-resolution AO buffer was also prototyped. It looked comparable but introduced an additional render pass and reduced this device to about **80.06 displayed FPS** with **10.90 ms GPU scene**. That implementation was removed; its screenshot and logs remain outside Git as `android-half-high.*` for comparison.

The current effect's cost is in presentation, not scene lighting. Matching full-resolution screenshots are in the external `../../../../outputs/ao-test/` directory as `android-off.png`, `android-contact-high.png`, and `android-4sample-high.png`; logs and presentation JSON are alongside them. The High view keeps the hall's overall brightness close to Off. Off remains the recommended default for maximum FPS.

The signed APK was installed as an update without removing app data. The host settings test passed; a real touch-menu check found Ambient occlusion under the shared Materials page, selected High and observed the native setting apply. It restored the complete preferences file afterward. The default 144 Hz device rule was not modified.

## Limits

This checkpoint does not include Android combat or water-scene AO benchmarks. Low and Medium were not profiled after the slope correction. Mac captures required `--capture-present-frame` because the normal frame capture omits presentation effects. The captured Mac hall view compiled and rendered at 4× MSAA, and the Android comparison confirms the on-device image and FPS in one representative view. The earlier broad-darkening captures remain in the external output directory as rejected visual evidence.
