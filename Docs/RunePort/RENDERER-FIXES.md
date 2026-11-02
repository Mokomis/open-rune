# Open Rune — Native 4x renderer work — October 24, 2026

This pass starts at `5357fc6f`. All device tests in this pass retain native
2520x1680, RGBA16F scene color, 4x MSAA, the existing package/signing identity and
120 Hz per-app rule. The user plugged the tablet into power during the work;
Android subsequently reported USB online=0 and battery Discharging, so the
actual power snapshots are retained. Sequential samples must not be presented
as controlled gains over the older results. No power, thermal, refresh or clock policy is changed.

## Correctness fixes and experiments

- `Flush(false)` now restores the dynamic viewport after submitting and starting
  a new command buffer. The new GPU fixture caught VUID 07831 and missing drawing
  after this existing texture-cache flush. The pre-fix log is retained.
- Scene depth barriers now use depth/stencil access flags rather than pipeline
  stage constants, with layouts matching the render pass. Initial frame barriers
  include prior resolve-transfer reads. The hit resolve uses a transfer stage
  constant rather than the numerically coincident access constant.
- `--render-discard-hit=1` selects compatible passes that discard unused selection
  data on ordinary frames. Actual hit-test frames keep their load/store behavior.
  Color and depth always retain STORE for continuation. Initial Hel3a
  samples: baseline 92.510 FPS, discard 91.650 FPS; this does not establish a gain.
- `--render-inpass-resolve=1` resolves color into the existing PPImage[0] inside
  the scene pass. A continuation resolves the preserved complete scene again.
  Bloom, color tuning and screenshots keep their existing input. Actual hit
  readback retains the separate integer resolve. The old color-resolve path
  remains selectable for comparison. This experiment stays disabled by default; it did not improve GPU time in the matched high-clock samples.

## GPU regression fixture

`--probe-vulkan` / Android `--ez vulkanProbe true` runs after frame 120. It requires
native resolution and verifies the actual 4x target sample count. At full device
resolution it draws opaque near/far geometry and overlapping transparency, tests
both continuation functions, reads pixels, and queries selection rectangles for
geometry drawn before/after the flush and an empty area. Ordinary frames follow
hit-test frames. It repeats with Enhanced bloom, with target recreation, and
compares in-pass resolve against the old external resolve when enabled.

The first corrected path passed 24 fixture frames. The in-pass comparison passed
48, including all-pixel RGB agreement within one 8-bit value, with Khronos core
and synchronization validation enabled and no reported warnings or errors.
The subsequent normal Hel3a frames and final gamma-corrected capture also finish.
This establishes the fixture's coverage, not every editor or campaign workload.

Run using the development APK with its packaged validation library:

```sh
python3 Tools/RuneValidation/android_renderer.py --device "$RUNE_DEVICE" \
  --resolve 1 --output /absolute/path/to/analysis/renderer-validation
```

For FPS, disable validation and the fixture. Use `android_campaign_maps.py` with
`--msaa 2 --discard-hit 0/1 --resolve 0/1`. It retains native resolution, isolated
saves, actual SurfaceFlinger presentation timestamps, hardware FPS and matching
engine/GPU profiles. Battery snapshots only annotate charging conditions; they
are not an extended heat comparison.

## Single-output gameplay path

`--render-omit-hit=1` removes the unused selection color reference and shader
export entirely for frames with no hit request. It uses matching scene, line,
point pipelines and a compatible separate framebuffer. Selection frames retain
the original two-output family and readback. The backing selection images remain
available; this is a rendering-bandwidth change, not an allocation-size claim.
The 48-frame native 4x validation/pixel suite passes with the path enabled.

The first uninterrupted three-map samples measured Hel3a 118.606, SailingShip
115.872 and HelLift 118.262 displayed FPS. The initial omitted-output run was
interrupted by New fight and ended with a surface-acquisition error; its mixed-map
FPS is excluded. A diagnostic build now includes VkResult in acquisition errors.
The subsequent HOME/resume test reproduced VK_ERROR_SURFACE_LOST_KHR; see the recovery section below.

Initial repeats at GPU maximum 1.225 GHz: omit-only 118.623 FPS, combined with
in-pass resolve 118.853 FPS. Earlier readbacks had lower available GPU clocks;
therefore comparisons against earlier 54–93 FPS runs do not isolate renderer
cost. The matching control and follow-up are recorded below. Per-run JSON
contains read-only GPU/CPU frequency samples when --oppo-clocks is specified.

The same APK's original-path control then measured 63.497 FPS while the recorded
GPU ceiling fell from 1.225 GHz to 646 MHz. A subsequent optimized repeat measured
118.204 FPS with a 1.225 GHz ceiling. These results demonstrate device-state
confounding; they are not a fixed 87% renderer speedup claim. At the higher clock,
omit-only GPU scene time was 6.090 ms versus 6.730 ms with in-pass resolve, so the
in-pass experiment has not shown an incremental benefit on this device. It should
remain diagnostic rather than be made the default solely from API expectations.

## Accepted defaults and playability fix

Android now omits the unused selection output by default. In-pass resolve and
store-discard experiments default off and remain opt-in diagnostics. Native
resolution, 4x MSAA, existing bloom toggle and selection behavior are retained.
A final 24-frame pixel/depth/hit/continuation test passes with these defaults,
including Original and Enhanced, with zero Vulkan validation warnings/errors.
Desktop compilation and the HelLift headless mechanics/particle suite also pass.

The home-screen test reproduced a fatal surface acquisition result -1000000000
(VK_ERROR_SURFACE_LOST_KHR). SDL replaces the Android native window when resuming;
recreating only the swapchain cannot repair the old Vulkan surface. Acquisition
and presentation now distinguish surface loss. After retiring outstanding GPU
and presentation work, the renderer destroys the old swapchain, recreates the
surface from the current window, then recreates the swapchain/framebuffers and
per-image presentation semaphores. Other fatal Vulkan errors remain fatal.
Three HOME/resume cycles pass with the same process, progressing frame counters,
three surface recreations and zero core/synchronization validation messages.
This validates those interruptions, not process death or every Android lifecycle.

A 76.71-second continuation sample with both experimental paths enabled averaged
101.587 displayed FPS while the GPU ceiling changed between 1.225 GHz and 646 MHz.
It had no intervals above 50 ms. This supersedes any suggestion that the short
118 FPS runs establish sustained 120 FPS. It is not the final omit-only default.
The user redirected priority to getting the game working; further clock/heat
experiments are deferred in favor of campaign and gameplay compatibility.

Evidence is retained under `evidence/renderer/`. Normal four save/backup hashes
match the immediately preceding campaign checkpoint; no historical claim is
made about earlier unexplained checkpoint changes. Refresh policy remains intact.
