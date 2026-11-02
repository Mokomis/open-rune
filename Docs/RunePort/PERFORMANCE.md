# Open Rune — Native Android performance and display validation

October 28 final bounded pass: [material/post-processing audit](MATERIAL-POST-PERFORMANCE.md).
Both candidates saved less than 1% and were removed; no further optimization sweep
is planned without a new user request.

## October 26 remastered renderer follow-up

With the newer effects enabled, native 4x cost is substantially higher than the
original October 24 build below. The [lighting optimization audit](RENDER-LIGHTING-PERFORMANCE.md)
measures current reference/optimized views: about 80→89 displayed FPS in the hall
and multiplayer, 50→62 at the water wheel, and 72→77 in live combat. Earlier 120 FPS
results below are historical and must not be presented as current all-effects FPS.

## Current OPPO policy and timing controls

October 28: the user now wants 120 Hz / 120 FPS with pacing enabled. See
[the 120 Hz timing follow-up](REFRESH-120-PERFORMANCE.md); the earlier 144 Hz
policy described below is historical.


The user requested 144 Hz on October 25. Open Rune's per-app policy is now 4,
and SDM verified an actual 144 Hz panel rate. FPS target and refresh request
are separate controls with 144/165/custom choices; see
[FRAME-RATE-REFRESH.md](FRAME-RATE-REFRESH.md). Measurements below retain their
original 120 Hz test conditions and are historical evidence.


October 24, 2026. This supersedes the initial display/FPS assessment in the investigation history.

## Final observed result

The installed prototype rendered at native **2520x1680**, with **4x MSAA**, and presented **120.0028 frames/s over 37.6408 seconds** (4518 unique presentation timestamps). Median display interval was **8.333125 ms**; the 95th percentile was **8.333542 ms**. The hardware compositor reported `FPS min:120 max:120 cur:120` and an 8,333,333 ns refresh period.

The corresponding 6000-frame engine sample averaged **174.97 FPS**, 5.7151 ms/frame, with a 7.5048 ms 95th percentile. These are two different measurements: the uncapped engine produces more frames than the mailbox presents. The display measurement is the evidence for 120 visible updates/s.

Scope: DM-Hildir, scripted forward movement for the first 560 frames, then the same view. This is not a campaign, enemy-combat, input-latency or long thermal-soak guarantee. Earlier short comparisons did not fix the random seed; final tests use `seed=1`. Movement is frame-count based and follows a different elapsed-time trajectory at different FPS, so the short comparisons are diagnostic rather than identical-workload benchmarks.

Evidence: `evidence/android-performance-release.log`, `evidence/android-performance-release-display.json`, and the matching `-timestamps.txt`.

## Two separate bottlenecks

### Engine and presentation

The renderer submitted each frame and immediately waited for its fence, leaving CPU preparation and GPU rendering largely serialized. It now alternates two vertex/index/upload buffer sets, retains the submitted command buffers and temporary objects, and lets the CPU prepare the next frame while the GPU finishes the previous one. The GPU still has only one active submission: completion is required before updating shared descriptors or submitting the next frame. Cache clears, resize/reallocation, sampler replacement, readback and destruction explicitly drain pending work. The second buffer set adds approximately 148 MiB of buffer allocation capacity.

Mailbox presentation is preferred on Android when supported, with the existing FIFO behavior as fallback. The OPPO returned five mailbox images. It presents the latest completed frame without the FIFO buffer wait observed in this test. Android's default remains uncapped; the per-app display request is 120 Hz. Desktop defaults remain unchanged, and overlap/mailbox can be opted into for testing.

Short engine comparisons at native resolution and 4x MSAA:

| Configuration | Engine FPS |
| --- | ---: |
| Synchronous + FIFO, repeat A/B | 56.70 / 56.15 |
| CPU/GPU overlap + FIFO, repeat A/B | 77.47 / 77.81 |
| Overlap + mailbox, repeat A/B | 164.77 / 165.50 |
| Final mailbox build, 6000 sampled frames | 174.97 |

Disabling MSAA produced only 78.01 FPS with FIFO, so it is not the default. Half-resolution FIFO reached 92.82 FPS, but the final build uses full native resolution.

An optional sleep-based `fps=120` limiter reached 119.43 engine FPS but only 109.68 displayed FPS over 37.52 seconds. Its independent cadence missed some display deadlines. It remains diagnostic; it is **not enabled by default**. Proper Android frame pacing (for example, evaluating Swappy) is a future way to reduce excess rendering without losing visible updates.

The BSP span merger was also corrected: the previous merge/erase loop could skip an uncovered gap. A bitmap oracle checks production clipping across 100000 deterministic operations; the old code fails and the new code passes, including ASan/UBSan. Repeated named class-property lookups now use a cache with invalidation while property-vector storage/size changes during loading. Native/headless probes verify cache hits, misses and replacement.

GPU clock samples taken while the app was confirmed alive rose through approximately 500–795 MHz. An earlier 195 MHz snapshot did not establish an active cap. No GPU clock or thermal policy was changed.

### OPPO hardware refresh restriction

The earlier conclusion that Rune did not need a refresh exception was incorrect. `dumpsys display` reported a 120 Hz mode, Rune's window and SurfaceView requested 120 Hz, and Android listed no Rune UID override. Nevertheless, actual SurfaceFlinger timestamps measured **60.0374 FPS**, and the SDM hardware section reported **60 Hz**.

The existing OppoPilot/Refresh Manager mechanism was then applied **only to Rune**:

- Package: `org.runeclassic.port`.
- Property: `persist.opdrr.42800e61` (Java package-name hash; checked for collisions against installed packages).
- Previous value: absent/empty. Current value: **3**, the device's 120 Hz policy ID.
- Re-evaluation: `service call oplusscreenmode 12 s16 org.runeclassic.port i32 3` as root, followed by relaunching only Rune.
- Result: **120.048 FPS** in the first independent 8.15-second sample, then **120.0028 FPS** in the final 37.64-second sample. Both hardware dumps reported 120 Hz.

The hook was already installed and active; no module install, reboot or global refresh change was needed. All other `persist.opdrr.*` rules were compared with the snapshot and are unchanged. Original DroidDeck configuration SHA-256 values also match the pre-work snapshot (four INI files). No original game assets or saves were modified.

`evidence/rune-refresh-trial-backup.json` records the previous rule. To return Rune to adaptive behavior on this exact device, select **Default (Adaptive)** for Rune in OppoPilot/Refresh Manager. The root equivalent is to restore the empty property and request policy 0 for Rune. Do not clear other packages' rules. The APK does not install or modify this root policy itself; another device must be assessed separately.

## Display and correctness checks

- SDL no longer changes its cached window size to 1024x768 while the Android surface remains native size. Vulkan drawable queries use the Vulkan API.
- Swapchain requested extent is tracked separately from pre-rotated buffer extent. The final presentation shader applies the surface transform; the scene/HUD remain upright internally. Actual device screenshots show full-screen upright world, HUD and controls.
- A request for reverse landscape was accepted by the Activity, but ColorOS kept the display at rotation 90 and Vulkan transform 2. The 270-degree rendering branch is implemented but **not device-validated**. Do not infer rotation from the intent or manifest alone.
- Khronos Android validation layer **1.4.363.0** was packaged temporarily and explicitly enabled, including synchronization validation. Its downloaded ZIP matched SHA-256 `9a3b8e32cb3330406dd4f3651594201635baee4528da8e2a5300e4f55e1a9cc7`.
- Validation identified an unsupported `OPAQUE` composite-alpha request. Swapchain creation now selects a supported mode (`INHERIT` on this Android surface). After the fix, the 1800-frame validation run and subsequent paced/control/attack runs completed without reported Vulkan warnings or errors.
- After the final benchmark, a later app exit was recorded as ColorOS `OTHER KILLS BY SYSTEM`, `o-stop(40)`, at background importance 400; no game crash was recorded in that exit entry. Background/resume and long-duration lifecycle behavior remain unvalidated. The bounded tests intentionally exit.
- The release APK excludes the validation library. Validation is never silently treated as active: an explicit request fails if the layer is unavailable.
- Desktop rendering with overlap completed and was visually inspected. Native Android and desktop SailingShip probes pass all Rune checks, including the new property cache check.
- Stool no-attack control ends with `destroyed=0`; attack ends with `destroyed=1`. On-screen attack hit the stool and its removal was visually verified. On-screen movement and drag-look visibly changed the view. These are not enemy-combat or multi-touch validations.

## Reproduction and next work

Build/install and diagnostic launch options are in `Android/README.md`. `Tools/RuneValidation/android_present.py` reads actual presentation timestamps without changing any device setting. Preserve engine FPS and displayed FPS as separate metrics.

Prioritize a real enemy encounter and save/load/map transitions. Then check more views/maps and sustained thermal behavior, opposite physical landscape orientation, and background/resume. Evaluate synchronized Android frame pacing before using the sleep limiter as a shipping 120 FPS control.

Implementation references: [Khronos CPU/GPU synchronization](https://docs.vulkan.org/samples/latest/samples/performance/wait_idle/README.html), [Android Vulkan pre-rotation](https://developer.android.com/games/optimize/vulkan-prerotation), and [Android Vulkan validation layers](https://developer.android.com/ndk/guides/graphics/validation-layer).
