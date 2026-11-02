# Open Rune: frame rate target and display refresh

October 25, 2026. Video and Enhancements share **Frame rate & refresh**.

- **Frame rate target** controls the game cadence: 30, 60, 120, 144, 165 FPS,
  or Custom (whole numbers 30–1000). With frame pacing on, the target supplies
  Swappy's swap interval; with pacing off, the existing CPU limiter uses it.
  This is a target, not a guarantee of displayed FPS or a refresh-rate label.
- **Refresh rate** requests Automatic, 60, 90, 120, 144, 165 Hz, or Custom.
  Custom accepts 30–1000 Hz when Android reports a matching display mode.
  Unsupported presets are disabled, and unsupported custom entries show an
  error without saving. Higher future modes do not need a new preset.
- **Frame pacing** remains independent. The page shows the FPS target, requested
  refresh, Android's mode readout, and supported refresh rates. The readout is
  labeled **Android reports**, because vendor policy can disagree with it.

Fresh defaults remain 120 FPS / 120 Hz / pacing on. Existing FPS indices retain
30/60/120 meanings. Manual values persist separately. Native resolution and MSAA
are unaffected by timing choices. All requests are per-window/per-surface;
ordinary APK behavior does not modify global settings, root policies or clocks.

The refresh request selects a same-resolution mode using preferredDisplayModeId
and the exact reported floating-point rate, then updates Surface.setFrameRate
and Android 15 view hints. On resize/resume it reapplies the request. If a saved
mode becomes unavailable (for example after changing displays), it requests
Automatic, preserves the saved choice, and reports the fallback. A diagnostic
refresh intent override is discarded when the user edits the refresh control.

JNI and Swappy now accept 30–1000 FPS instead of silently forcing higher targets
back to 120. One numeric request crosses the engine boundary and changes cadence
between frames. Manual entries are validated before either preference is saved.
Text-editor key events are routed to the editor rather than swallowed as
background gameplay keys; Done applies the value and dismisses the keyboard.

## OPPO per-app policy

October 28 update: the user chose **120 Hz / 120 FPS / pacing on**. The old
forced-144 rule was still overriding the in-game refresh request. The latest
comparison and corrected package-only policy are documented in
[the 120 Hz timing follow-up](REFRESH-120-PERFORMANCE.md). The 144 Hz observations
below are historical.


The user explicitly requested Open Rune's refresh exception at **144 Hz**.
The existing OppoPilot mechanism uses `persist.opdrr.42800e61` for
`org.runeclassic.port`. The verified package hash had no installed-package
collision. Its prior policy was 3 (120 Hz); it is now **4 (144 Hz)**. Re-evaluation
used `service call oplusscreenmode 12 s16 org.runeclassic.port i32 4` as root.
All other `persist.opdrr.*` values matched the snapshot. No module, reboot or
global refresh change was involved.

The live hardware compositor reported `FPS min:144 max:144 cur:144` and
`Vsync period: 6944444`. This is stronger evidence than Android's mode alone.
165 Hz is not advertised by this OPPO; it remains available on devices that
report it. The APK supports the selection without claiming this tablet runs it.

Rollback for this exact rule: restore property value 3 and request policy 3 for
this package only. Do not restore or clear unrelated app rules. Device evidence
and rollback details are retained with the validation results.

## Validation scope

Host model checks cover 144/165 mappings, manual values above 165, persistence,
invalid-value rejection without writes, independent Automatic refresh, and
reset defaults. Native ARM64 and Java/APK builds pass. Device checks use an
isolated DM-Hildir session at native resolution with 4× MSAA. Results below
record UI/input behavior and short presentation samples, not a whole-campaign
or sustained high-FPS claim. Screenshots remain in local outputs/frame-refresh.

References: [Android frame-rate requests](https://developer.android.com/media/optimize/performance/frame-rate),
[Window display-mode preference](https://developer.android.com/reference/android/view/WindowManager.LayoutParams#preferredDisplayModeId).
